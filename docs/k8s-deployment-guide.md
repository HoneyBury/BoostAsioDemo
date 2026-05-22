# Boost Gateway -- Kubernetes Deployment Guide

## Overview

This guide covers containerizing the Boost Gateway game server and deploying it to Kubernetes using:

- **Docker** -- multi-stage Windows container images
- **Helm** -- chart for deploying the gateway server
- **Kopf Operator** -- Python-based Kubernetes operator for managing `GatewayServer` custom resources
- **Rollout verification** -- health checks and rollout validation

---

## Prerequisites

| Tool | Version | Purpose |
|------|---------|---------|
| Docker Desktop | 24+ (Windows container mode) | Building images |
| kind / k3s | latest | Local Kubernetes cluster |
| kubectl | 1.28+ | K8s CLI |
| Helm | 3.12+ | Package management |
| Python | 3.8+ | Operator runtime |
| CMake | 3.21+ | Building the server |

---

## 1. Building Container Images

### 1.1 Gateway Server (Windows Container)

```powershell
# Build the gateway-server image
docker build -f docker\gateway-server.Dockerfile `
    --build-arg BUILD_TYPE=Release `
    -t gateway-server:latest .
```

The Dockerfile uses a multi-stage build:
- **Build stage**: `mcr.microsoft.com/windows/servercore:ltsc2022` with MSVC Build Tools 2022 and CMake
- **Runtime stage**: `mcr.microsoft.com/windows/nanoserver:ltsc2022` with only the binaries and VC++ redistributables

### 1.2 Grafana Agent Sidecar

```powershell
# Build the grafana-agent sidecar image
docker build -f docker\grafana-agent.Dockerfile -t grafana-agent:latest .
```

### 1.3 Local Development with Docker Compose

```powershell
# Start gateway server
docker compose up -d gateway-server

# Start gateway server with monitoring
docker compose --profile monitoring up -d

# Check logs
docker compose logs -f gateway-server

# Stop everything
docker compose down -v
```

Services:
- **gateway-server**: TCP game server on port 8080, gRPC on 50051
- **grafana-agent** (profile: monitoring): Metrics collection sidecar on port 12345

---

## 2. Helm Chart

### 2.1 Chart Structure

```
k8s/helm/gateway-server/
  Chart.yaml          # Chart metadata
  values.yaml         # Default configuration values
  templates/
    _helpers.tpl      # Template helpers
    configmap.yaml    # Gateway configuration as ConfigMap
    deployment.yaml   # Deployment resource
    service.yaml      # Service resource
    serviceaccount.yaml
    hpa.yaml          # HorizontalPodAutoscaler (CPU-based)
    pdb.yaml          # PodDisruptionBudget (50% min available)
    ingress.yaml      # Ingress (optional)
```

### 2.2 Configuration (values.yaml)

Key configuration options:

| Parameter | Default | Description |
|-----------|---------|-------------|
| `replicaCount` | `2` | Number of gateway server replicas |
| `image.repository` | `gateway-server` | Container image repository |
| `image.tag` | `latest` | Container image tag |
| `service.type` | `ClusterIP` | Kubernetes service type |
| `service.port` | `8080` | Game server port |
| `config.logLevel` | `info` | Logging level |
| `config.backendHost` | `backend-service` | Backend service hostname |
| `config.backendPort` | `9090` | Backend service port |
| `config.grpcPort` | `50051` | gRPC port |
| `config.dataDir` | `/data` | Data directory |
| `autoscaling.enabled` | `true` | Enable HPA |
| `autoscaling.targetCPUUtilizationPercentage` | `70` | CPU target for HPA |
| `pdb.enabled` | `true` | Enable PDB |
| `pdb.minAvailable` | `50%` | Minimum available pods |

### 2.3 Installing via Helm

```bash
# Install the chart
helm install gateway-server k8s/helm/gateway-server/ \
    --namespace boost-gateway \
    --create-namespace

# Install with custom values
helm install gateway-server k8s/helm/gateway-server/ \
    --namespace boost-gateway \
    --create-namespace \
    --set replicaCount=3 \
    --set config.logLevel=debug \
    --set autoscaling.enabled=true

# Upgrade
helm upgrade gateway-server k8s/helm/gateway-server/ \
    --namespace boost-gateway \
    --set replicaCount=5

# Uninstall
helm uninstall gateway-server --namespace boost-gateway

# Render templates without installing
helm template gateway-server k8s/helm/gateway-server/
```

---

## 3. Operator Deployment

### 3.1 Operator Architecture

The operator (`k8s/operator/operator.py`) is built on the **kopf** framework and watches `GatewayServer` custom resources:

```
GatewayServer CR (create)
    |
    v
Operator reconcile
    |
    +--> Create Deployment (apps/v1)
    |
    +--> Create Service (v1)
    |
    +--> Update status (phase, readyReplicas, conditions)

GatewayServer CR (update)
    |
    v
Operator reconcile
    |
    +--> Patch Deployment (rolling update)
    |
    +--> Patch Service (if service config changed)

GatewayServer CR (delete)
    |
    v
Operator cleanup
    |
    +--> Delete Deployment
    |
    +--> Delete Service
```

### 3.2 CRD Schema

The `GatewayServer` CRD (`k8s/crds/gatewayservers.yaml`) defines:

```yaml
apiVersion: gateway.boost.io/v1
kind: GatewayServer
metadata:
  name: my-gateway-server
spec:
  replicaCount: 2          # Number of Pod replicas
  image: gateway-server:latest  # Container image
  service:
    type: ClusterIP        # Service type
    port: 8080             # Service port
  config:
    logLevel: info         # Log level
    backendEndpoint: "backend-service:9090"  # Backend address
```

### 3.3 Deploying the Operator

#### Using the deployment script:

```powershell
# PowerShell (Windows)
.\k8s\operator\deploy-operator.ps1

# Custom namespace and image
.\k8s\operator\deploy-operator.ps1 -Namespace my-ns -Image myregistry/operator:v1
```

The script performs:
1. Creates namespace `boost-gateway`
2. Deploys RBAC (ServiceAccount + ClusterRole + ClusterRoleBinding)
3. Applies the `GatewayServer` CRD
4. Deploys the Operator Deployment (1 replica)
5. Creates a sample `GatewayServer` custom resource

#### Manual deployment:

```bash
# Apply CRD
kubectl apply -f k8s/crds/gatewayservers.yaml

# Create namespace
kubectl create namespace boost-gateway

# Apply RBAC
kubectl apply -f - <<EOF
apiVersion: v1
kind: ServiceAccount
metadata:
  name: gateway-server-operator
  namespace: boost-gateway
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
    resources: ["services", "configmaps", "pods", "events"]
    verbs: ["get", "list", "watch", "create", "update", "patch", "delete"]
---
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRoleBinding
metadata:
  name: gateway-server-operator
subjects:
  - kind: ServiceAccount
    name: gateway-server-operator
    namespace: boost-gateway
roleRef:
  kind: ClusterRole
  name: gateway-server-operator
  apiGroup: rbac.authorization.k8s.io
EOF

# Deploy operator
kubectl apply -n boost-gateway -f - <<EOF
apiVersion: apps/v1
kind: Deployment
metadata:
  name: gateway-server-operator
  labels:
    app.kubernetes.io/name: gateway-server-operator
spec:
  replicas: 1
  selector:
    matchLabels:
      app.kubernetes.io/name: gateway-server-operator
  template:
    metadata:
      labels:
        app.kubernetes.io/name: gateway-server-operator
    spec:
      serviceAccountName: gateway-server-operator
      containers:
        - name: operator
          image: gateway-operator:latest
          command: ["python", "/app/operator.py"]
          env:
            - name: POD_NAMESPACE
              valueFrom:
                fieldRef:
                  fieldPath: metadata.namespace
EOF
```

### 3.4 Creating a Custom Resource

```bash
kubectl apply -f - <<EOF
apiVersion: gateway.boost.io/v1
kind: GatewayServer
metadata:
  name: my-gateway-server
  namespace: boost-gateway
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
```

### 3.5 Verifying Operator State

```bash
# Check CR status
kubectl get gatewayservers -n boost-gateway
kubectl describe gatewayserver my-gateway-server -n boost-gateway

# Check managed resources
kubectl get deployment -n boost-gateway
kubectl get service -n boost-gateway
kubectl get pods -n boost-gateway

# Check operator logs
kubectl logs -n boost-gateway -l app.kubernetes.io/name=gateway-server-operator -f
```

---

## 4. Grayscale Release (Canary)

### 4.1 Using PodDisruptionBudget

The PDB ensures at least 50% of pods remain available during voluntary disruptions:

```bash
# Verify PDB
kubectl get pdb -n boost-gateway
```

### 4.2 Using HorizontalPodAutoscaler

The HPA automatically scales the gateway server based on CPU utilization:

```bash
# Check HPA status
kubectl get hpa -n boost-gateway

# Describe HPA for detailed metrics
kubectl describe hpa -n boost-gateway
```

### 4.3 Rolling Update Strategy

When updating (e.g., changing `replicaCount` or `image`), the operator patches the Deployment which triggers a rolling update:

```bash
# Update the CR (operator will reconcile)
kubectl patch gatewayserver my-gateway-server -n boost-gateway \
    --type merge \
    -p '{"spec":{"replicaCount":5}}'

# Watch the rollout
kubectl rollout status deployment/my-gateway-server -n boost-gateway --timeout=300s

# Check pod update progress
kubectl get pods -n boost-gateway --watch
```

---

## 5. Rollback

### 5.1 Rollback the CR (operator-driven)

```bash
# Revert replicaCount
kubectl patch gatewayserver my-gateway-server -n boost-gateway \
    --type merge \
    -p '{"spec":{"replicaCount":2}}'

# Revert image (use previous image tag)
kubectl patch gatewayserver my-gateway-server -n boost-gateway \
    --type merge \
    -p '{"spec":{"image":"gateway-server:previous-tag"}}'
```

### 5.2 Rollback the Deployment (direct)

```bash
# Check rollout history
kubectl rollout history deployment/my-gateway-server -n boost-gateway

# Rollback to previous revision
kubectl rollout undo deployment/my-gateway-server -n boost-gateway

# Rollback to specific revision
kubectl rollout undo deployment/my-gateway-server -n boost-gateway --to-revision=2
```

### 5.3 Verify Rollback

```bash
# Run the rollout verification script
./k8s/rollout-verify.sh -n boost-gateway -d my-gateway-server
```

---

## 6. Rollout Verification

The `k8s/rollout-verify.sh` script performs comprehensive verification:

```bash
# Default verification
./k8s/rollout-verify.sh

# Custom namespace and deployment
./k8s/rollout-verify.sh -n boost-gateway -d my-gateway-server -p 8080

# Custom timeout
./k8s/rollout-verify.sh -t 600s
```

Verification steps:
1. `kubectl rollout status` -- checks deployment rollout completed
2. Pod readiness -- all pods report `Ready`
3. Service endpoints -- at least one endpoint registered
4. Health check -- `/healthz` returns HTTP 200

---

## 7. Local E2E Verification

The `scripts/k8s-e2e-verify.ps1` script runs a complete end-to-end test:

```powershell
# Full E2E test (creates kind cluster, tests, cleans up)
.\scripts\k8s-e2e-verify.ps1

# Keep cluster running for debugging
.\scripts\k8s-e2e-verify.ps1 -SkipCleanup

# Custom cluster name
.\scripts\k8s-e2e-verify.ps1 -ClusterName my-test-cluster
```

Test sequence:
1. Creates a kind cluster
2. Builds the gateway-server Docker image and loads it into kind
3. Deploys CRD + Operator + RBAC
4. Creates a `GatewayServer` CR
5. Verifies the operator creates Deployment and Service
6. Tests operator update (scale up replicas)
7. Tests operator cleanup (delete CR, verify resource removal)
8. Cleans up the kind cluster

---

## 8. Architecture Diagrams

### 8.1 Component Overview

```
+-------------------+       +-------------------+
|   Kubernetes API  |<----->|  Kopf Operator    |
|   Server          |       |  (operator.py)    |
+-------------------+       +-------------------+
        |                           |
        | watches                   | manages
        v                           v
+-------------------+       +-------------------+
| GatewayServer CR  |       | Deployment        |
| (custom resource) |       | (gateway-server)  |
+-------------------+       +-------------------+
                                    |
                                    v
                            +-------------------+
                            | Service           |
                            | (ClusterIP:8080)  |
                            +-------------------+
                                    |
                                    v
                            +-------------------+
                            | Pod(s)            |
                            | (gateway-server)  |
                            +-------------------+
```

### 8.2 Data Flow

```
Client ---> Service (:8080) ---> Pod (gateway-server)
                                        |
                                        +--> /healthz (liveness/readiness)
                                        +--> /metrics (Prometheus)
                                        +--> gRPC (:50051)
                                        |
                                        +--> Backend (configurable endpoint)
```

---

## 9. Troubleshooting

### Operator not reconciling

```bash
# Check operator logs
kubectl logs -n boost-gateway -l app.kubernetes.io/name=gateway-server-operator

# Verify CRD is installed
kubectl get crd gatewayservers.gateway.boost.io

# Check RBAC permissions
kubectl auth can-i -n boost-gateway --list \
    --as=system:serviceaccount:boost-gateway:gateway-server-operator
```

### Pods not starting

```bash
# Describe the deployment
kubectl describe deployment my-gateway-server -n boost-gateway

# Check pod events
kubectl describe pod -n boost-gateway -l app.kubernetes.io/instance=my-gateway-server

# Check pod logs
kubectl logs -n boost-gateway -l app.kubernetes.io/instance=my-gateway-server
```

### Health check failing

```bash
# Port-forward to a pod directly
kubectl port-forward -n boost-gateway pod/my-gateway-server-xxx 8080:8080

# Test health check
curl http://localhost:8080/healthz

# Check if the service endpoints exist
kubectl get endpoints -n boost-gateway
```
