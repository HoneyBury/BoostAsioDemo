#!/usr/bin/env python3
"""
Boost Gateway -- Kubernetes Operator for GatewayServer CRD.

Listens for GatewayServer custom resource events (create/update/delete)
and reconciles the corresponding Kubernetes Deployment and Service
resources.

Key capabilities:
  - Creates/reconciles Deployments and Services for GatewayServer CR
  - Reports status.components[] for 6 backend services
  - Reports Ready/Progressing/Degraded/TLSReady conditions
  - Periodic health assessment every 30 seconds via @kopf.timer
  - TLS secret reconciliation (auto-creates self-signed certs via @kopf.on.resume)

Usage:
  # Run locally (while connected to a K8s cluster):
  python k8s/operator/operator.py

  # Run as a K8s Deployment (see deploy-operator.ps1):
  # (container image built separately)

Framework: kopf (Kubernetes Operator Pythonic Framework)
"""

import base64
import logging
import os
import sys
import datetime
from typing import Any, Mapping, Optional

import kopf
import kubernetes
import kubernetes.client
import kubernetes.config
import yaml

try:
    from cryptography import x509
    from cryptography.x509.oid import NameOID
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import rsa
    HAS_CRYPTOGRAPHY = True
except ImportError:
    HAS_CRYPTOGRAPHY = False

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    stream=sys.stdout,
)
logger = logging.getLogger("gateway-operator")

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

OPERATOR_NAME = "gateway-server-operator"
DEFAULT_NAMESPACE = "boost-gateway"
OWNER_API_VERSION = "gateway.boost.io/v1"
OWNER_KIND = "GatewayServer"

# Six backend components monitored for health reporting
COMPONENTS = [
    "gateway-server",
    "login-backend",
    "room-backend",
    "battle-backend",
    "matchmaking-backend",
    "leaderboard-backend",
]

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _utc_now() -> str:
    """Return current UTC time as ISO 8601 string."""
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def _namespace(spec: Mapping[str, Any], **_kwargs: Any) -> str:
    """Derive the target namespace from the CR's metadata or fall back to default."""
    ns = spec.get("namespace", os.getenv("POD_NAMESPACE", DEFAULT_NAMESPACE))
    return ns


def _build_labels(instance: Mapping[str, Any]) -> dict[str, str]:
    """Build standard Kubernetes labels for a GatewayServer instance."""
    name = instance.get("metadata", {}).get("name", "gateway-server")
    return {
        "app.kubernetes.io/name": "gateway-server",
        "app.kubernetes.io/instance": name,
        "app.kubernetes.io/component": "game-server",
        "app.kubernetes.io/part-of": "boost-gateway",
        "app.kubernetes.io/managed-by": OPERATOR_NAME,
    }


def _build_deployment(
    name: str,
    namespace: str,
    spec: Mapping[str, Any],
    labels: dict[str, str],
) -> dict[str, Any]:
    """Construct a Deployment manifest from the GatewayServer spec."""
    replicas = spec.get("replicaCount", 1)
    image = spec.get("image", "gateway-server:latest")
    port = spec.get("service", {}).get("port", 8080)
    log_level = spec.get("config", {}).get("logLevel", "info")
    backend_endpoint = spec.get("config", {}).get("backendEndpoint", "backend-service:9090")

    container_name = "gateway-server"

    deployment = {
        "apiVersion": "apps/v1",
        "kind": "Deployment",
        "metadata": {
            "name": name,
            "namespace": namespace,
            "labels": labels,
        },
        "spec": {
            "replicas": replicas,
            "selector": {"matchLabels": {"app.kubernetes.io/instance": name}},
            "template": {
                "metadata": {"labels": labels},
                "spec": {
                    "containers": [
                        {
                            "name": container_name,
                            "image": image,
                            "imagePullPolicy": "IfNotPresent",
                            "ports": [
                                {"containerPort": port, "name": "game-port", "protocol": "TCP"},
                                {"containerPort": 50051, "name": "grpc-port", "protocol": "TCP"},
                            ],
                            "env": [
                                {"name": "BOOST_LOG_LEVEL", "value": log_level},
                                {"name": "BACKEND_ENDPOINT", "value": backend_endpoint},
                                {"name": "POD_NAME", "valueFrom": {"fieldRef": {"fieldPath": "metadata.name"}}},
                                {"name": "POD_NAMESPACE", "valueFrom": {"fieldRef": {"fieldPath": "metadata.namespace"}}},
                            ],
                            "livenessProbe": {
                                "httpGet": {"path": "/healthz", "port": port},
                                "initialDelaySeconds": 10,
                                "periodSeconds": 15,
                            },
                            "readinessProbe": {
                                "httpGet": {"path": "/healthz", "port": port},
                                "initialDelaySeconds": 5,
                                "periodSeconds": 10,
                            },
                            "resources": {
                                "requests": {"cpu": "250m", "memory": "128Mi"},
                                "limits": {"cpu": "1", "memory": "512Mi"},
                            },
                        }
                    ],
                    "terminationGracePeriodSeconds": 30,
                },
            },
            "strategy": {
                "type": "RollingUpdate",
                "rollingUpdate": {
                    "maxUnavailable": 1,
                    "maxSurge": 1,
                },
            },
        },
    }
    return deployment


def _build_service(
    name: str,
    namespace: str,
    spec: Mapping[str, Any],
    labels: dict[str, str],
) -> dict[str, Any]:
    """Construct a Service manifest from the GatewayServer spec."""
    port = spec.get("service", {}).get("port", 8080)
    service_type = spec.get("service", {}).get("type", "ClusterIP")

    service = {
        "apiVersion": "v1",
        "kind": "Service",
        "metadata": {
            "name": name,
            "namespace": namespace,
            "labels": labels,
        },
        "spec": {
            "type": service_type,
            "selector": {"app.kubernetes.io/instance": name},
            "ports": [
                {
                    "name": "game-port",
                    "port": port,
                    "targetPort": "game-port",
                    "protocol": "TCP",
                },
                {
                    "name": "grpc-port",
                    "port": 50051,
                    "targetPort": "grpc-port",
                    "protocol": "TCP",
                },
            ],
        },
    }
    return service


# ---------------------------------------------------------------------------
# Health / Status helpers
# ---------------------------------------------------------------------------


def _get_component_status(
    name: str,
    namespace: str,
    apps_v1: kubernetes.client.AppsV1Api,
) -> dict[str, Any]:
    """Query a Deployment and return its component status dict.

    Returns a dict with keys: name, kind, ready, replicas, available, message.
    """
    try:
        dep = apps_v1.read_namespaced_deployment(name=name, namespace=namespace)
        desired = dep.spec.replicas or 0
        available = dep.status.available_replicas or 0
        ready_replicas = dep.status.ready_replicas or 0
        message = None
        if available < desired:
            message = f"Waiting for rollout: {available}/{desired} replicas available"
        return {
            "name": name,
            "kind": "Deployment",
            "ready": ready_replicas > 0 and available >= desired,
            "replicas": desired,
            "available": available,
            "message": message,
        }
    except kubernetes.client.ApiException as e:
        if e.status == 404:
            logger.info("Component Deployment not found: %s/%s", namespace, name)
            return {
                "name": name,
                "kind": "Deployment",
                "ready": False,
                "replicas": 0,
                "available": 0,
                "message": "Not found",
            }
        logger.error("Failed to query component %s/%s: %s", namespace, name, e)
        return {
            "name": name,
            "kind": "Deployment",
            "ready": False,
            "replicas": 0,
            "available": 0,
            "message": f"API error: {e.reason}",
        }


def _assess_health(
    components: list[dict],
    desired_replicas: int,
    old_status: Optional[dict] = None,
) -> tuple[list[dict], int]:
    """Assess overall health and build status conditions.

    Evaluates component readiness, detects rollouts in progress, and
    tracks consecutive health-check failures for degradation detection.

    Returns (conditions, consecutive_failures).
    """
    failed_components = [c for c in components if not c["ready"]]
    failed_count = len(failed_components)

    # Track consecutive failures from the CR's persisted status
    prev_failures = (old_status or {}).get("failedHealthChecks", 0)
    consecutive_failures = prev_failures + 1 if failed_count > 0 else 0

    all_ready = failed_count == 0
    any_progressing = any(
        c["available"] < c["replicas"]
        for c in components
        if c["replicas"] > 0
    )
    degraded = consecutive_failures >= 3

    now = _utc_now()
    conditions = []

    # ── Ready condition ──────────────────────────────────────────────
    if all_ready:
        conditions.append({
            "type": "Ready",
            "status": "True",
            "lastTransitionTime": now,
            "reason": "AllComponentsReady",
            "message": "All 6 backend components are healthy and fully available",
        })
    else:
        failed_names = [c["name"] for c in failed_components]
        conditions.append({
            "type": "Ready",
            "status": "False",
            "lastTransitionTime": now,
            "reason": "ComponentsNotReady",
            "message": f"Components not ready: {', '.join(failed_names)}",
        })

    # ── Progressing condition ─────────────────────────────────────────
    if any_progressing:
        progressing_names = [
            c["name"] for c in components
            if c["replicas"] > 0 and c["available"] < c["replicas"]
        ]
        conditions.append({
            "type": "Progressing",
            "status": "True",
            "lastTransitionTime": now,
            "reason": "RollingUpdateInProgress",
            "message": f"Rolling update in progress for: {', '.join(progressing_names)}",
        })
    else:
        conditions.append({
            "type": "Progressing",
            "status": "False",
            "lastTransitionTime": now,
            "reason": "NoRolloutInProgress",
            "message": "All components are stable",
        })

    # ── Degraded condition ────────────────────────────────────────────
    if degraded:
        conditions.append({
            "type": "Degraded",
            "status": "True",
            "lastTransitionTime": now,
            "reason": "ConsecutiveHealthCheckFailures",
            "message": f"{consecutive_failures} consecutive health check failures detected",
        })
    else:
        conditions.append({
            "type": "Degraded",
            "status": "False",
            "lastTransitionTime": now,
            "reason": "NormalOperation",
            "message": "No degradation detected",
        })

    return conditions, consecutive_failures


def _check_tls_ready(namespace: str, spec: Mapping[str, Any]) -> dict:
    """Check TLS readiness and return the TLSReady condition dict."""
    tls_config = spec.get("tls", {})
    secret_name = tls_config.get("secretName")

    if not secret_name:
        logger.info("TLS not configured for namespace %s", namespace)
        return {
            "type": "TLSReady",
            "status": "True",
            "lastTransitionTime": _utc_now(),
            "reason": "TLSNotConfigured",
            "message": "TLS is not configured; skipping",
        }

    core_v1 = kubernetes.client.CoreV1Api()
    try:
        secret = core_v1.read_namespaced_secret(name=secret_name, namespace=namespace)
        logger.info(
            "TLS secret '%s/%s' found (type: %s)",
            namespace, secret_name, secret.type,
        )
        return {
            "type": "TLSReady",
            "status": "True",
            "lastTransitionTime": _utc_now(),
            "reason": "SecretFound",
            "message": f"TLS secret '{secret_name}' exists and is valid",
        }
    except kubernetes.client.ApiException as e:
        if e.status == 404:
            logger.warning("TLS secret '%s/%s' not found", namespace, secret_name)
            return {
                "type": "TLSReady",
                "status": "False",
                "lastTransitionTime": _utc_now(),
                "reason": "SecretNotFound",
                "message": f"TLS secret '{secret_name}' not found",
            }
        logger.error("Failed to check TLS secret '%s/%s': %s", namespace, secret_name, e)
        return {
            "type": "TLSReady",
            "status": "Unknown",
            "lastTransitionTime": _utc_now(),
            "reason": "CheckError",
            "message": f"Error checking TLS secret: {e.reason}",
        }


# ---------------------------------------------------------------------------
# TLS Secret coordination
# ---------------------------------------------------------------------------


def _ensure_tls_secret(namespace: str, secret_name: str, cr_name: str) -> bool:
    """Create a self-signed TLS certificate if the secret doesn't exist.

    Uses the ``cryptography`` library to generate a 2048-bit RSA key
    and a self-signed X.509 certificate valid for 365 days.

    Returns True if a new secret was created, False otherwise.
    """
    core_v1 = kubernetes.client.CoreV1Api()

    # Check if secret already exists
    try:
        core_v1.read_namespaced_secret(name=secret_name, namespace=namespace)
        logger.info("TLS secret already exists: %s/%s", namespace, secret_name)
        return False
    except kubernetes.client.ApiException as e:
        if e.status != 404:
            logger.warning(
                "Unexpected error checking TLS secret %s/%s: %s",
                namespace, secret_name, e,
            )
            return False

    if not HAS_CRYPTOGRAPHY:
        logger.error(
            "Cannot create TLS secret '%s/%s': cryptography library not available. "
            "Install with: pip install cryptography",
            namespace, secret_name,
        )
        return False

    logger.info("Creating self-signed TLS certificate secret: %s/%s", namespace, secret_name)

    try:
        key = rsa.generate_private_key(public_exponent=65537, key_size=2048)

        subject = issuer = x509.Name([
            x509.NameAttribute(
                NameOID.COMMON_NAME,
                f"{secret_name}.{namespace}.svc.cluster.local",
            ),
            x509.NameAttribute(NameOID.ORGANIZATION_NAME, "BoostGateway"),
        ])

        now_dt = datetime.datetime.utcnow()
        cert = (
            x509.CertificateBuilder()
            .subject_name(subject)
            .issuer_name(issuer)
            .public_key(key.public_key())
            .serial_number(x509.random_serial_number())
            .not_valid_before(now_dt)
            .not_valid_after(now_dt + datetime.timedelta(days=365))
            .add_extension(
                x509.SubjectAlternativeName([
                    x509.DNSName(secret_name),
                    x509.DNSName(f"{secret_name}.{namespace}"),
                    x509.DNSName(f"{secret_name}.{namespace}.svc.cluster.local"),
                ]),
                critical=False,
            )
            .sign(key, hashes.SHA256())
        )

        cert_pem = cert.public_bytes(serialization.Encoding.PEM).decode("utf-8")
        key_pem = key.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.TraditionalOpenSSL,
            serialization.NoEncryption(),
        ).decode("utf-8")

        secret_body = {
            "apiVersion": "v1",
            "kind": "Secret",
            "metadata": {
                "name": secret_name,
                "namespace": namespace,
                "labels": {
                    "app.kubernetes.io/managed-by": OPERATOR_NAME,
                    "app.kubernetes.io/part-of": "boost-gateway",
                },
                "annotations": {
                    "gateway.boost.io/certificate-type": "self-signed",
                    "gateway.boost.io/owner-cr": cr_name,
                },
            },
            "type": "kubernetes.io/tls",
            "data": {
                "tls.crt": base64.b64encode(cert_pem.encode("utf-8")).decode("ascii"),
                "tls.key": base64.b64encode(key_pem.encode("utf-8")).decode("ascii"),
            },
        }

        core_v1.create_namespaced_secret(namespace=namespace, body=secret_body)
        logger.info("Self-signed TLS secret created: %s/%s", namespace, secret_name)
        return True
    except Exception as e:
        logger.error(
            "Failed to create TLS secret '%s/%s': %s",
            namespace, secret_name, e,
        )
        return False


# ---------------------------------------------------------------------------
# Kopf handlers
# ---------------------------------------------------------------------------


@kopf.on.create("gateway.boost.io", "v1", "gatewayservers")
def gatewayserver_create(
    spec: Mapping[str, Any],
    meta: Mapping[str, Any],
    **_kwargs: Any,
) -> dict[str, Any]:
    """Handle creation of a GatewayServer CR.

    Creates a Deployment and a Service based on the CR spec, then
    reports initial component status and health conditions.
    """
    name = meta.get("name", "gateway-server")
    namespace = _namespace(spec)

    logger.info("GatewayServer created: %s/%s", namespace, name)

    labels = _build_labels({"metadata": meta})
    apps_v1 = kubernetes.client.AppsV1Api()
    core_v1 = kubernetes.client.CoreV1Api()

    # Create Deployment
    deployment = _build_deployment(name, namespace, spec, labels)
    try:
        apps_v1.create_namespaced_deployment(namespace=namespace, body=deployment)
        logger.info("Deployment created: %s/%s", namespace, name)
    except kubernetes.client.ApiException as exc:
        if exc.status == 409:
            logger.warning("Deployment already exists: %s/%s, patching", namespace, name)
            apps_v1.patch_namespaced_deployment(name=name, namespace=namespace, body=deployment)
        else:
            logger.error("Failed to create Deployment: %s", exc)
            raise

    # Create Service
    service = _build_service(name, namespace, spec, labels)
    try:
        core_v1.create_namespaced_service(namespace=namespace, body=service)
        logger.info("Service created: %s/%s", namespace, name)
    except kubernetes.client.ApiException as exc:
        if exc.status == 409:
            logger.warning("Service already exists: %s/%s, patching", namespace, name)
            core_v1.patch_namespaced_service(name=name, namespace=namespace, body=service)
        else:
            logger.error("Failed to create Service: %s", exc)
            raise

    # Query initial component status
    components = [_get_component_status(c, namespace, apps_v1) for c in COMPONENTS]
    desired_replicas = spec.get("replicaCount", 1)
    conditions, failures = _assess_health(components, desired_replicas)

    # Add TLSReady condition
    tls_condition = _check_tls_ready(namespace, spec)
    conditions.append(tls_condition)

    ready_replicas = sum(c.get("available", 0) for c in components)

    logger.info(
        "GatewayServer reconciliation complete: %s/%s "
        "(desiredReplicas=%d, components=%d)",
        namespace, name, desired_replicas, len(components),
    )

    return {
        "phase": "Running",
        "readyReplicas": ready_replicas,
        "desiredReplicas": desired_replicas,
        "components": components,
        "conditions": conditions,
        "failedHealthChecks": failures,
    }


@kopf.on.update("gateway.boost.io", "v1", "gatewayservers")
def gatewayserver_update(
    spec: Mapping[str, Any],
    old: Mapping[str, Any],
    meta: Mapping[str, Any],
    **_kwargs: Any,
) -> Optional[dict[str, Any]]:
    """Handle update of a GatewayServer CR.

    Applies rolling update by patching the Deployment and Service,
    then reports updated component status and conditions.
    Only triggers when meaningful spec fields change.
    """
    name = meta.get("name", "gateway-server")
    namespace = _namespace(spec)

    old_spec = old.get("spec", {})

    # Compute a simple diff to skip no-op updates
    changed_fields = []
    for key in ("replicaCount", "image", "config", "service"):
        if spec.get(key) != old_spec.get(key):
            changed_fields.append(key)

    if not changed_fields:
        logger.info("No meaningful changes for %s/%s, skipping update", namespace, name)
        return None

    logger.info(
        "GatewayServer updated: %s/%s, changed fields: %s",
        namespace, name, changed_fields,
    )

    labels = _build_labels({"metadata": meta})
    apps_v1 = kubernetes.client.AppsV1Api()
    core_v1 = kubernetes.client.CoreV1Api()

    # Patch Deployment
    deployment = _build_deployment(name, namespace, spec, labels)
    try:
        apps_v1.patch_namespaced_deployment(name=name, namespace=namespace, body=deployment)
        logger.info("Deployment patched: %s/%s", namespace, name)
    except kubernetes.client.ApiException as exc:
        logger.error("Failed to patch Deployment: %s", exc)
        raise

    # Patch Service if service config changed
    if "service" in changed_fields:
        service = _build_service(name, namespace, spec, labels)
        try:
            core_v1.patch_namespaced_service(name=name, namespace=namespace, body=service)
            logger.info("Service patched: %s/%s", namespace, name)
        except kubernetes.client.ApiException as exc:
            logger.error("Failed to patch Service: %s", exc)
            raise

    # Query updated component status
    components = [_get_component_status(c, namespace, apps_v1) for c in COMPONENTS]
    desired_replicas = spec.get("replicaCount", 1)
    conditions, failures = _assess_health(components, desired_replicas)

    # Add TLSReady condition
    tls_condition = _check_tls_ready(namespace, spec)
    conditions.append(tls_condition)

    ready_replicas = sum(c.get("available", 0) for c in components)

    logger.info(
        "GatewayServer update reconciliation complete: %s/%s",
        namespace, name,
    )

    return {
        "phase": "Running",
        "readyReplicas": ready_replicas,
        "desiredReplicas": desired_replicas,
        "components": components,
        "conditions": conditions,
        "failedHealthChecks": failures,
    }


@kopf.on.delete("gateway.boost.io", "v1", "gatewayservers")
def gatewayserver_delete(
    meta: Mapping[str, Any],
    spec: Mapping[str, Any],
    **_kwargs: Any,
) -> None:
    """Handle deletion of a GatewayServer CR.

    Cleans up the associated Deployment and Service.
    Kopf handles finalizers automatically -- this handler runs before
    the CR is removed from the API server.
    """
    name = meta.get("name", "gateway-server")
    namespace = _namespace(spec)

    logger.info("GatewayServer deleted: %s/%s", namespace, name)

    apps_v1 = kubernetes.client.AppsV1Api()
    core_v1 = kubernetes.client.CoreV1Api()

    # Delete Deployment
    try:
        apps_v1.delete_namespaced_deployment(name=name, namespace=namespace)
        logger.info("Deployment deleted: %s/%s", namespace, name)
    except kubernetes.client.ApiException as exc:
        if exc.status == 404:
            logger.info("Deployment not found (already deleted): %s/%s", namespace, name)
        else:
            logger.error("Failed to delete Deployment: %s", exc)
            raise

    # Delete Service
    try:
        core_v1.delete_namespaced_service(name=name, namespace=namespace)
        logger.info("Service deleted: %s/%s", namespace, name)
    except kubernetes.client.ApiException as exc:
        if exc.status == 404:
            logger.info("Service not found (already deleted): %s/%s", namespace, name)
        else:
            logger.error("Failed to delete Service: %s", exc)
            raise

    logger.info("GatewayServer cleanup complete: %s/%s", namespace, name)


# ---------------------------------------------------------------------------
# Resume handler -- TLS + initial health check on operator restart
# ---------------------------------------------------------------------------


@kopf.on.resume("gateway.boost.io", "v1", "gatewayservers")
def gatewayserver_resume(
    spec: Mapping[str, Any],
    meta: Mapping[str, Any],
    status: Optional[dict] = None,
    **_kwargs: Any,
) -> dict[str, Any]:
    """Handle operator restart / resume for existing GatewayServer CRs.

    Reconciles the TLS secret (creates a self-signed cert if missing)
    and reports the initial component status and health conditions.
    """
    name = meta.get("name", "gateway-server")
    namespace = _namespace(spec)

    logger.info("GatewayServer resumed: %s/%s", namespace, name)

    # Reconcile TLS secret
    tls_config = spec.get("tls", {})
    secret_name = tls_config.get("secretName")
    if secret_name:
        _ensure_tls_secret(namespace, secret_name, name)

    # Query initial component status
    apps_v1 = kubernetes.client.AppsV1Api()
    components = [_get_component_status(c, namespace, apps_v1) for c in COMPONENTS]
    desired_replicas = spec.get("replicaCount", 1)
    conditions, failures = _assess_health(components, desired_replicas, old_status=status)

    # Add TLSReady condition
    tls_condition = _check_tls_ready(namespace, spec)
    conditions.append(tls_condition)

    ready_replicas = sum(c.get("available", 0) for c in components)

    logger.info(
        "GatewayServer resume complete: %s/%s "
        "(desiredReplicas=%d, components_ready=%d/%d, failures=%d)",
        namespace, name,
        desired_replicas,
        sum(1 for c in components if c["ready"]),
        len(components),
        failures,
    )

    return {
        "phase": "Running",
        "readyReplicas": ready_replicas,
        "desiredReplicas": desired_replicas,
        "components": components,
        "conditions": conditions,
        "failedHealthChecks": failures,
    }


# ---------------------------------------------------------------------------
# Timer-based health check (every 30 seconds)
# ---------------------------------------------------------------------------


@kopf.timer("gateway.boost.io", "v1", "gatewayservers", interval=30.0)
def gatewayserver_healthcheck(
    spec: Mapping[str, Any],
    meta: Mapping[str, Any],
    status: Optional[dict] = None,
    **_kwargs: Any,
) -> Optional[dict[str, Any]]:
    """Periodic health assessment of all backend components.

    Runs every 30 seconds via @kopf.timer. Queries the Kubernetes API
    for each component Deployment, evaluates health conditions, and
    updates the CR status with the latest Ready/Progressing/Degraded/
    TLSReady conditions.
    """
    name = meta.get("name", "gateway-server")
    namespace = _namespace(spec)

    logger.debug("Running health check for %s/%s", namespace, name)

    apps_v1 = kubernetes.client.AppsV1Api()

    try:
        components = [_get_component_status(c, namespace, apps_v1) for c in COMPONENTS]
    except Exception as e:
        logger.error("Health check query failed for %s/%s: %s", namespace, name, e)
        return None

    desired_replicas = spec.get("replicaCount", 1)
    conditions, failures = _assess_health(components, desired_replicas, old_status=status)

    # Add TLSReady condition
    tls_condition = _check_tls_ready(namespace, spec)
    conditions.append(tls_condition)

    ready_replicas = sum(c.get("available", 0) for c in components)

    ready_count = sum(1 for c in components if c["ready"])
    logger.info(
        "Health check complete for %s/%s: %d/%d components ready, "
        "%d consecutive failures",
        namespace, name,
        ready_count, len(components),
        failures,
    )

    return {
        "desiredReplicas": desired_replicas,
        "components": components,
        "conditions": conditions,
        "failedHealthChecks": failures,
        "readyReplicas": ready_replicas,
    }


# ---------------------------------------------------------------------------
# Startup / Health
# ---------------------------------------------------------------------------


@kopf.on.startup()
def configure(settings: kopf.OperatorSettings, **_kwargs: Any) -> None:
    """Configure operator settings on startup."""
    settings.persistence.diffang_errors = True
    settings.persistence.finalizer = f"{OPERATOR_NAME}/finalizer"
    settings.posting.level = logging.INFO
    settings.watching.server_timeout = 300

    logger.info("GatewayServer operator started. Watching for GatewayServer CR events.")


# ---------------------------------------------------------------------------
# Entrypoint
# ---------------------------------------------------------------------------

def main() -> None:
    """Initialize Kubernetes client and run the operator."""
    # Try in-cluster config first, fall back to kubeconfig
    try:
        kubernetes.config.load_incluster_config()
        logger.info("Using in-cluster Kubernetes configuration")
    except kubernetes.config.ConfigException:
        try:
            kubernetes.config.load_kube_config()
            logger.info("Using kubeconfig file for Kubernetes configuration")
        except kubernetes.config.ConfigException as exc:
            logger.error(
                "Cannot load Kubernetes configuration. "
                "Set KUBECONFIG or run inside a K8s cluster. Error: %s",
                exc,
            )
            sys.exit(1)

    # Run the kopf operator
    kopf.run()


if __name__ == "__main__":
    main()
