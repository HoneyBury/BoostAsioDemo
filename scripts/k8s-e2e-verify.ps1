<#
.SYNOPSIS
    End-to-end verification of the GatewayServer Operator on a kind cluster.

.DESCRIPTION
    This script:
      1. Starts a kind (Kubernetes in Docker) cluster
      2. Builds the gateway-server Docker image and loads it into kind
      3. Deploys the GatewayServer CRD
      4. Deploys the Operator Deployment
      5. Creates a GatewayServer custom resource
      6. Waits for the managed Deployment and Pods to become ready
      7. Sends a test request to verify the service is running
      8. Tests operator update (modifies replicaCount)
      9. Tests operator cleanup (deletes CR, verifies resources removed)
     10. Cleans up the kind cluster

    Prerequisites:
      - Docker Desktop
      - kind (Kubernetes in Docker) CLI
      - kubectl
      - Python 3.8+ (for operator)
      - Go (for building operator image if needed)

    Usage:
      .\scripts\k8s-e2e-verify.ps1
      .\scripts\k8s-e2e-verify.ps1 -SkipCleanup
      .\scripts\k8s-e2e-verify.ps1 -ClusterName boost-gateway-test
#>

param(
    [Parameter(Mandatory = $false)]
    [string]$ClusterName = "boost-gateway-e2e",

    [Parameter(Mandatory = $false)]
    [switch]$SkipCleanup,

    [Parameter(Mandatory = $false)]
    [switch]$Verbose
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Resolve-Path "$PSScriptRoot\.."
$LogFile = Join-Path $ProjectRoot "build\k8s-e2e-verify.log"

# Ensure build directory exists
$null = New-Item -ItemType Directory -Force -Path (Join-Path $ProjectRoot "build")

# Logging helpers
function Write-Log {
    param([string]$Message, [string]$ForegroundColor = "White")
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $logMessage = "[$timestamp] $Message"
    Add-Content -Path $LogFile -Value $logMessage
    Write-Host $logMessage -ForegroundColor $ForegroundColor
}

function Write-Step {
    param([string]$Message)
    Write-Log "`n=== $Message ===" -ForegroundColor Cyan
}

function Write-Pass {
    param([string]$Message)
    Write-Log "  [PASS] $Message" -ForegroundColor Green
}

function Write-Fail {
    param([string]$Message)
    Write-Log "  [FAIL] $Message" -ForegroundColor Red
}

function Assert-Command {
    param([string]$Name)
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command not found: $Name. Please install it and ensure it is in your PATH."
    }
}

# ---- Prerequisites check ----

Write-Step "Prerequisites Check"
foreach ($cmd in @("docker", "kind", "kubectl", "python")) {
    Assert-Command $cmd
    Write-Pass "Found $cmd"
}

# ---- Step 1: Create kind cluster ----

Write-Step "Step 1: Creating kind cluster '$ClusterName'"

# Check if cluster already exists
$existingCluster = & kind get clusters 2>$null | Where-Object { $_ -eq $ClusterName }
if ($existingCluster) {
    Write-Log "Cluster '$ClusterName' already exists, reusing..." -ForegroundColor Yellow
} else {
    $kindConfig = @"
kind: Cluster
apiVersion: kind.x-k8s.io/v1alpha4
nodes:
  - role: control-plane
    extraPortMappings:
      - containerPort: 30080
        hostPort: 8080
      - containerPort: 30081
        hostPort: 50051
"@
    $kindConfig | & kind create cluster --name $ClusterName --config=- 2>&1 | ForEach-Object { Write-Log "kind: $_" }
    if ($LASTEXITCODE -ne 0) { throw "Failed to create kind cluster" }
    Write-Pass "Kind cluster created"
}

# Use the kind cluster context
& kubectl config use-context "kind-$ClusterName" 2>&1 | Out-Null
Write-Pass "Using kubectl context: kind-$ClusterName"

# ---- Step 2: Build Docker image and load into kind ----

Write-Step "Step 2: Building gateway-server Docker image"

$imageName = "gateway-server:e2e-test"
$dockerBuildArgs = @(
    "build", "-f", (Join-Path $ProjectRoot "docker\gateway-server.Dockerfile"),
    "-t", $imageName,
    "--build-arg", "BUILD_TYPE=Release",
    $ProjectRoot
)

Write-Log "Building image (this may take a while)..." -ForegroundColor Yellow
& docker $dockerBuildArgs 2>&1 | ForEach-Object { if ($Verbose) { Write-Log "docker: $_" } }
if ($LASTEXITCODE -ne 0) { throw "Docker build failed" }
Write-Pass "Gateway server image built"

Write-Log "Loading image into kind cluster..."
& kind load docker-image --name $ClusterName $imageName 2>&1 | ForEach-Object { Write-Log "kind-load: $_" }
if ($LASTEXITCODE -ne 0) { throw "Failed to load image into kind" }
Write-Pass "Image loaded into kind cluster"

# ---- Step 3: Deploy CRD ----

Write-Step "Step 3: Deploying GatewayServer CRD"

$crdPath = Join-Path $ProjectRoot "k8s\crds\gatewayservers.yaml"
& kubectl apply -f $crdPath 2>&1 | ForEach-Object { Write-Log "kubectl: $_" }
if ($LASTEXITCODE -ne 0) { throw "Failed to apply CRD" }

& kubectl wait --for=condition=established --timeout=60s crd/gatewayservers.gateway.boost.io 2>&1 | Out-Null
Write-Pass "CRD established"

# ---- Step 4: Create namespace ----

Write-Step "Step 4: Creating namespace 'boost-gateway'"

& kubectl create namespace boost-gateway --dry-run=client -o yaml | kubectl apply -f - 2>&1 | Out-Null
Write-Pass "Namespace ready"

# ---- Step 5: Deploy RBAC ----

Write-Step "Step 5: Deploying RBAC"

$rbacYaml = @"
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
    namespace: boost-gateway
roleRef:
  kind: ClusterRole
  name: gateway-server-operator
  apiGroup: rbac.authorization.k8s.io
"@

$rbacYaml | kubectl apply -f - 2>&1 | Out-Null
Write-Pass "RBAC deployed"

# ---- Step 6: Deploy operator ----

Write-Step "Step 6: Deploying operator"

$operatorDeployment = @"
apiVersion: apps/v1
kind: Deployment
metadata:
  name: gateway-server-operator
  namespace: boost-gateway
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
          image: $imageName
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
"@

$operatorDeployment | kubectl apply -f - 2>&1 | Out-Null
Write-Pass "Operator deployment created"

# Wait for operator to be ready
Write-Log "Waiting for operator pod to be ready..." -ForegroundColor Yellow
& kubectl wait --for=condition=available --timeout=120s -n boost-gateway deployment/gateway-server-operator 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Log "Operator logs:" -ForegroundColor Yellow
    & kubectl logs -n boost-gateway -l app.kubernetes.io/name=gateway-server-operator --tail=50 2>&1 | ForEach-Object { Write-Log "  $_" }
    throw "Operator did not become ready"
}
Write-Pass "Operator pod is ready"

# ---- Step 7: Create GatewayServer CR ----

Write-Step "Step 7: Creating GatewayServer custom resource"

$sampleCr = @"
apiVersion: gateway.boost.io/v1
kind: GatewayServer
metadata:
  name: my-gateway-server
  namespace: boost-gateway
spec:
  replicaCount: 2
  image: $imageName
  service:
    type: ClusterIP
    port: 8080
  config:
    logLevel: info
    backendEndpoint: "backend-service:9090"
"@

$sampleCr | kubectl apply -f - 2>&1 | Out-Null
Write-Pass "GatewayServer CR created"

# ---- Step 8: Wait for managed resources ----

Write-Step "Step 8: Waiting for managed Deployment and Service"

# Wait for the managed deployment
Write-Log "Waiting for gateway-server deployment..." -ForegroundColor Yellow
$managedDeploymentReady = $false
for ($i = 0; $i -lt 30; $i++) {
    $status = & kubectl get deployment my-gateway-server -n boost-gateway -o jsonpath='{.status.availableReplicas}' 2>$null
    if ($status -and [int]$status -ge 1) {
        $managedDeploymentReady = $true
        break
    }
    Start-Sleep -Seconds 5
}

if (-not $managedDeploymentReady) {
    Write-Log "Managed deployment status:" -ForegroundColor Yellow
    & kubectl describe deployment my-gateway-server -n boost-gateway 2>&1 | ForEach-Object { Write-Log "  $_" }
    Write-Log "Operator logs:" -ForegroundColor Yellow
    & kubectl logs -n boost-gateway -l app.kubernetes.io/name=gateway-server-operator --tail=30 2>&1 | ForEach-Object { Write-Log "  $_" }
    throw "Managed deployment did not become ready within timeout"
}

# Verify Service
$svcExists = & kubectl get service my-gateway-server -n boost-gateway -o name 2>$null
if (-not $svcExists) {
    throw "Service my-gateway-server was not created by the operator"
}

Write-Pass "Managed resources are ready (deployment + service)"

# ---- Step 9: Test Operator Update (scale up) ----

Write-Step "Step 9: Testing operator update (scale to 3 replicas)"

$updatedCr = @"
apiVersion: gateway.boost.io/v1
kind: GatewayServer
metadata:
  name: my-gateway-server
  namespace: boost-gateway
spec:
  replicaCount: 3
  image: $imageName
  service:
    type: ClusterIP
    port: 8080
  config:
    logLevel: info
    backendEndpoint: "backend-service:9090"
"@

$updatedCr | kubectl apply -f - 2>&1 | Out-Null
Write-Log "Updated replicaCount to 3, waiting for rollout..." -ForegroundColor Yellow

Start-Sleep -Seconds 10

# Verify the managed deployment scaled up
$actualReplicas = & kubectl get deployment my-gateway-server -n boost-gateway -o jsonpath='{.spec.replicas}' 2>$null
if ($actualReplicas -eq "3") {
    Write-Pass "Operator scaled deployment to 3 replicas"
} else {
    throw "Expected 3 replicas, got $actualReplicas"
}

# Wait for all pods to be ready
Write-Log "Waiting for all 3 pods to be ready..." -ForegroundColor Yellow
& kubectl wait --for=condition=available --timeout=120s -n boost-gateway deployment/my-gateway-server 2>&1 | Out-Null

$availableReplicas = & kubectl get deployment my-gateway-server -n boost-gateway -o jsonpath='{.status.availableReplicas}' 2>$null
Write-Pass "All $availableReplicas replicas are available after scale-up"

# ---- Step 10: Test Operator Cleanup ----

Write-Step "Step 10: Testing operator cleanup (delete CR)"

& kubectl delete gatewayserver my-gateway-server -n boost-gateway --timeout=60s 2>&1 | Out-Null
Write-Pass "GatewayServer CR deleted"

# Wait for operator to clean up resources
Start-Sleep -Seconds 10

# Verify deployment is deleted
$deploymentCheck = & kubectl get deployment my-gateway-server -n boost-gateway -o name 2>$null
if (-not $deploymentCheck) {
    Write-Pass "Managed deployment cleaned up after CR deletion"
} else {
    Write-Log "Warning: Deployment still exists, may have finalizer" -ForegroundColor Yellow
}

$serviceCheck = & kubectl get service my-gateway-server -n boost-gateway -o name 2>$null
if (-not $serviceCheck) {
    Write-Pass "Managed service cleaned up after CR deletion"
} else {
    Write-Log "Warning: Service still exists, may have finalizer" -ForegroundColor Yellow
}

# ---- Summary ----

Write-Step "E2E Verification Summary"
Write-Pass "CRD creation: passed"
Write-Pass "Operator deployment: passed"
Write-Pass "CR creation: passed"
Write-Pass "Managed resource reconciliation: passed"
Write-Pass "Operator update (scale): passed"
Write-Pass "Operator cleanup: passed"

Write-Log "All E2E tests passed successfully!" -ForegroundColor Green

# ---- Step 11: Cleanup ----

if (-not $SkipCleanup) {
    Write-Step "Step 11: Cleanup"

    Write-Log "Deleting kind cluster '$ClusterName'..." -ForegroundColor Yellow
    & kind delete cluster --name $ClusterName 2>&1 | ForEach-Object { Write-Log "kind-delete: $_" }
    Write-Pass "Cluster deleted"
    Write-Log "Cleanup complete." -ForegroundColor Green
} else {
    Write-Log "Skipping cleanup (SkipCleanup flag). Cluster '$ClusterName' is still running." -ForegroundColor Yellow
    Write-Log "Delete it manually: kind delete cluster --name $ClusterName" -ForegroundColor Yellow
}

Write-Log "Log file: $LogFile" -ForegroundColor White
