#!/usr/bin/env bash
# ============================================================================
# Boost Gateway -- Deploy GatewayServer Operator
#
# Creates RBAC, deploys the Operator, applies the CRD, and creates
# a sample GatewayServer custom resource.
#
# Usage:
#   ./k8s/operator/deploy-operator.sh
#   ./k8s/operator/deploy-operator.sh -n my-namespace -i myregistry/operator:latest
#   ./k8s/operator/deploy-operator.sh --help
# ============================================================================

set -euo pipefail

# ─── Defaults ──────────────────────────────────────────────────────────────

NAMESPACE="boost-gateway"
IMAGE="gateway-operator:latest"
KUBE_CONTEXT=""
SKIP_CRD=false

# ─── Argument parsing ─────────────────────────────────────────────────────

usage() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Deploy the GatewayServer Operator to a Kubernetes cluster.

Options:
  -n NAME      Namespace (default: $NAMESPACE)
  -i IMAGE     Operator image (default: $IMAGE)
  -c CONTEXT   kubectl context
  --skip-crd   Skip CRD installation
  --help       Show this help message
EOF
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -n) NAMESPACE="$2"; shift 2 ;;
        -i) IMAGE="$2"; shift 2 ;;
        -c) KUBE_CONTEXT="$2"; shift 2 ;;
        --skip-crd) SKIP_CRD=true; shift ;;
        --help) usage ;;
        *) echo "Unknown option: $1"; usage ;;
    esac
done

KUBECTL="kubectl"
if [ -n "$KUBE_CONTEXT" ]; then
    KUBECTL="kubectl --context=$KUBE_CONTEXT"
fi

# ─── Helper functions ─────────────────────────────────────────────────────

info()  { echo -e "\033[36m[INFO]\033[0m $1"; }
pass()  { echo -e "\033[32m  [PASS]\033[0m $1"; }
fail()  { echo -e "\033[31m  [FAIL]\033[0m $1"; exit 1; }

ensure_namespace() {
    info "[1/5] Ensuring namespace '$NAMESPACE'..."
    $KUBECTL create namespace "$NAMESPACE" --dry-run=client -o yaml | $KUBECTL apply -f - >/dev/null
    pass "Namespace ready"
}

deploy_rbac() {
    info "[2/5] Deploying RBAC..."

    cat <<EOF | $KUBECTL apply -f - >/dev/null
apiVersion: v1
kind: ServiceAccount
metadata:
  name: gateway-server-operator
  namespace: $NAMESPACE
---
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRole
metadata:
  name: gateway-server-operator
rules:
  - apiGroups: ["gateway.boost.io"]
    resources: ["gatewayservers", "gatewayservers/status"]
    verbs: ["get", "list", "watch", "create", "update", "patch", "delete"]
  - apiGroups: ["apps"]
    resources: ["deployments"]
    verbs: ["get", "list", "watch", "create", "update", "patch", "delete"]
  - apiGroups: [""]
    resources: ["services", "configmaps", "secrets", "pods", "events"]
    verbs: ["get", "list", "watch", "create", "update", "patch", "delete"]
  - apiGroups: ["autoscaling"]
    resources: ["horizontalpodautoscalers"]
    verbs: ["get", "list", "watch", "create", "update", "delete"]
  - apiGroups: ["policy"]
    resources: ["poddisruptionbudgets"]
    verbs: ["get", "list", "watch", "create", "update", "delete"]
---
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRoleBinding
metadata:
  name: gateway-server-operator
subjects:
  - kind: ServiceAccount
    name: gateway-server-operator
    namespace: $NAMESPACE
roleRef:
  kind: ClusterRole
  name: gateway-server-operator
  apiGroup: rbac.authorization.k8s.io
EOF
    pass "RBAC deployed"
}

deploy_crd() {
    info "[3/5] Applying GatewayServer CRD..."

    local crd_path
    crd_path="$(cd "$(dirname "$0")/../crds" && pwd)/gatewayservers.yaml"

    if [ -f "$crd_path" ]; then
        $KUBECTL apply -f "$crd_path" >/dev/null
    else
        info "CRD file not found at $crd_path, creating inline..."
        cat <<EOF | $KUBECTL apply -f - >/dev/null
apiVersion: apiextensions.k8s.io/v1
kind: CustomResourceDefinition
metadata:
  name: gatewayservers.gateway.boost.io
spec:
  group: gateway.boost.io
  names:
    kind: GatewayServer
    listKind: GatewayServerList
    plural: gatewayservers
    singular: gatewayserver
    shortNames: [gwsrv]
  scope: Namespaced
  versions:
    - name: v1
      served: true
      storage: true
      schema:
        openAPIV3Schema:
          type: object
          properties:
            spec:
              type: object
              properties:
                replicaCount:
                  type: integer
                  minimum: 0
                  default: 1
                image:
                  type: string
                  default: "gateway-server:latest"
                service:
                  type: object
                  properties:
                    type:
                      type: string
                      enum: [ClusterIP, NodePort, LoadBalancer]
                      default: ClusterIP
                    port:
                      type: integer
                      minimum: 1
                      maximum: 65535
                      default: 8080
                config:
                  type: object
                  properties:
                    logLevel:
                      type: string
                      default: info
                    backendEndpoint:
                      type: string
                      default: "backend-service:9090"
            status:
              type: object
              properties:
                phase:
                  type: string
                  enum: [Pending, Running, Draining, Stopped]
                readyReplicas:
                  type: integer
                conditions:
                  type: array
                  items:
                    type: object
                    properties:
                      type:
                        type: string
                      status:
                        type: string
                      lastTransitionTime:
                        type: string
                        format: date-time
      subresources:
        status: {}
      additionalPrinterColumns:
        - name: Replicas
          type: integer
          jsonPath: .spec.replicaCount
        - name: Phase
          type: string
          jsonPath: .status.phase
        - name: Age
          type: date
          jsonPath: .metadata.creationTimestamp
EOF
    fi

    info "Waiting for CRD to be established..."
    $KUBECTL wait --for=condition=established --timeout=60s crd/gatewayservers.gateway.boost.io >/dev/null 2>&1
    pass "CRD ready"
}

deploy_operator() {
    info "[4/5] Deploying Operator Deployment..."

    cat <<EOF | $KUBECTL apply -f - >/dev/null
apiVersion: apps/v1
kind: Deployment
metadata:
  name: gateway-server-operator
  namespace: $NAMESPACE
  labels:
    app.kubernetes.io/name: gateway-server-operator
    app.kubernetes.io/component: operator
    app.kubernetes.io/part-of: boost-gateway
spec:
  replicas: 1
  selector:
    matchLabels:
      app.kubernetes.io/name: gateway-server-operator
  template:
    metadata:
      labels:
        app.kubernetes.io/name: gateway-server-operator
        app.kubernetes.io/component: operator
        app.kubernetes.io/part-of: boost-gateway
    spec:
      serviceAccountName: gateway-server-operator
      containers:
        - name: operator
          image: $IMAGE
          imagePullPolicy: IfNotPresent
          command: ["python", "/app/operator.py"]
          env:
            - name: POD_NAMESPACE
              valueFrom:
                fieldRef:
                  fieldPath: metadata.namespace
            - name: PYTHONUNBUFFERED
              value: "1"
          resources:
            requests:
              cpu: 100m
              memory: 64Mi
            limits:
              cpu: 500m
              memory: 256Mi
EOF

    info "Waiting for operator Pod to become ready..."
    $KUBECTL wait --for=condition=available --timeout=120s -n "$NAMESPACE" deployment/gateway-server-operator >/dev/null 2>&1
    pass "Operator ready"
}

deploy_sample_cr() {
    info "[5/5] Creating sample GatewayServer Custom Resource..."

    cat <<EOF | $KUBECTL apply -f - >/dev/null
apiVersion: gateway.boost.io/v1
kind: GatewayServer
metadata:
  name: my-gateway-server
  namespace: $NAMESPACE
spec:
  replicaCount: 2
  image: gateway-server:latest
  service:
    type: ClusterIP
    port: 8080
  config:
    logLevel: info
    backendEndpoint: "backend-service:9090"
EOF

    pass "Sample CR applied"
    echo ""
    echo "  Check status with:"
    echo "    $KUBECTL get gatewayservers -n $NAMESPACE"
    echo ""
    echo "  Watch operator logs with:"
    echo "    $KUBECTL logs -n $NAMESPACE -l app.kubernetes.io/name=gateway-server-operator -f"
}

# ─── Main ──────────────────────────────────────────────────────────────────

echo ""
echo "=== Boost Gateway Operator Deployment ==="
echo "  Namespace: $NAMESPACE"
echo "  Image:     $IMAGE"
echo ""

ensure_namespace
deploy_rbac
if [ "$SKIP_CRD" = false ]; then
    deploy_crd
fi
deploy_operator
deploy_sample_cr

echo ""
echo -e "\033[36m=== Deployment Complete ===\033[0m"
echo "All resources deployed successfully in namespace '$NAMESPACE'."
echo ""
