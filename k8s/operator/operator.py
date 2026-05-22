#!/usr/bin/env python3
"""
Boost Gateway -- Kubernetes Operator for GatewayServer CRD.

Listens for GatewayServer custom resource events (create/update/delete)
and reconciles the corresponding Kubernetes Deployment and Service
resources.

Usage:
  # Run locally (while connected to a K8s cluster):
  python k8s/operator/operator.py

  # Run as a K8s Deployment (see deploy-operator.ps1):
  # (container image built separately)

Framework: kopf (Kubernetes Operator Pythonic Framework)
"""

import logging
import os
import sys
from typing import Any, Mapping, Optional

import kopf
import kubernetes
import kubernetes.client
import kubernetes.config
import yaml

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

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


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
# Kopf handlers
# ---------------------------------------------------------------------------


@kopf.on.create("gateway.boost.io", "v1", "gatewayservers")
def gatewayserver_create(
    spec: Mapping[str, Any],
    meta: Mapping[str, Any],
    **_kwargs: Any,
) -> dict[str, Any]:
    """Handle creation of a GatewayServer CR.

    Creates a Deployment and a Service based on the CR spec.
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

    logger.info("GatewayServer reconciliation complete: %s/%s", namespace, name)

    return {
        "phase": "Running",
        "readyReplicas": 0,
        "conditions": [
            {
                "type": "Provisioned",
                "status": "True",
                "lastTransitionTime": kopf.Present.FieldValue(),
            }
        ],
    }


@kopf.on.update("gateway.boost.io", "v1", "gatewayservers")
def gatewayserver_update(
    spec: Mapping[str, Any],
    old: Mapping[str, Any],
    meta: Mapping[str, Any],
    **_kwargs: Any,
) -> Optional[dict[str, Any]]:
    """Handle update of a GatewayServer CR.

    Applies rolling update by patching the Deployment and Service.
    Only triggers an update when meaningful spec fields change.
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

    logger.info("GatewayServer update reconciliation complete: %s/%s", namespace, name)

    return {
        "phase": "Running",
        "conditions": [
            {
                "type": "Updated",
                "status": "True",
                "lastTransitionTime": kopf.Present.FieldValue(),
            }
        ],
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
