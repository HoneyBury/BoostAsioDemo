<#
.SYNOPSIS
    Deploy the GatewayServer Operator to a Kubernetes cluster.

.DESCRIPTION
    This script creates the RBAC resources (ServiceAccount, ClusterRole,
    ClusterRoleBinding), deploys the Operator as a Kubernetes Deployment,
    applies the GatewayServer CRD, and creates a sample Custom Resource.

    Prerequisites:
      - kubectl installed and configured
      - Access to a Kubernetes cluster (kind, k3s, or production)

    Usage:
      .\k8s\operator\deploy-operator.ps1
      .\k8s\operator\deploy-operator.ps1 -Namespace custom-ns -Image ghcr.io/org/operator:latest
#>

param(
    [Parameter(Mandatory = $false)]
    [string]$Namespace = "boost-gateway",

    [Parameter(Mandatory = $false)]
    [string]$Image = "gateway-operator:latest",

    [Parameter(Mandatory = $false)]
    [string]$KubeContext = "",

    [Parameter(Mandatory = $false)]
    [switch]$SkipCrd
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Resolve-Path "$PSScriptRoot\..\.."
$OperatorDir = Resolve-Path "$PSScriptRoot"

Write-Host "=== Boost Gateway Operator Deployment ===" -ForegroundColor Cyan
Write-Host "Namespace: $Namespace"
Write-Host "Image:     $Image"
Write-Host ""

function Ensure-Namespace {
    Write-Host "[1/5] Ensuring namespace '$Namespace'..." -ForegroundColor Yellow
    $kubeArgs = @("create", "namespace", $Namespace, "--dry-run=client", "-o", "yaml")
    if ($KubeContext) { $kubeArgs += "--context=$KubeContext" }
    $nsYaml = & kubectl $kubeArgs 2>$null
    if ($LASTEXITCODE -eq 0) {
        $kubeApplyArgs = @("apply", "-f", "-")
        if ($KubeContext) { $kubeApplyArgs += "--context=$KubeContext" }
        $nsYaml | kubectl $kubeApplyArgs
    }
    Write-Host "  Namespace ready." -ForegroundColor Green
}

function Deploy-RBAC {
    Write-Host "[2/5] Deploying RBAC (ServiceAccount, ClusterRole, ClusterRoleBinding)..." -ForegroundColor Yellow

    $serviceAccount = @"
apiVersion: v1
kind: ServiceAccount
metadata:
  name: gateway-server-operator
  namespace: $Namespace
"@

    $clusterRole = @"
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
"@

    $clusterRoleBinding = @"
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRoleBinding
metadata:
  name: gateway-server-operator
subjects:
  - kind: ServiceAccount
    name: gateway-server-operator
    namespace: $Namespace
roleRef:
  kind: ClusterRole
  name: gateway-server-operator
  apiGroup: rbac.authorization.k8s.io
"@

    $kubeArgs = @("apply", "-f", "-")
    if ($KubeContext) { $kubeArgs += "--context=$KubeContext" }

    $serviceAccount, $clusterRole, "--", $clusterRoleBinding | ForEach-Object {
        $_ | kubectl $kubeArgs
        if ($LASTEXITCODE -ne 0) { throw "Failed to apply RBAC resource" }
    }
    Write-Host "  RBAC deployed." -ForegroundColor Green
}

function Deploy-CRD {
    Write-Host "[3/5] Applying GatewayServer CRD..." -ForegroundColor Yellow
    $crdPath = Join-Path $OperatorDir "..\..\k8s\crds\gatewayservers.yaml"
    if (-not (Test-Path $crdPath)) {
        Write-Host "  CRD file not found at $crdPath, creating inline CRD..." -ForegroundColor Yellow

        $crdYaml = @"
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
    shortNames: [gs]
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
"@
        $kubeArgs = @("apply", "-f", "-")
        if ($KubeContext) { $kubeArgs += "--context=$KubeContext" }
        $crdYaml | kubectl $kubeArgs
    } else {
        $kubeArgs = @("apply", "-f", $crdPath)
        if ($KubeContext) { $kubeArgs += "--context=$KubeContext" }
        & kubectl $kubeArgs
    }
    if ($LASTEXITCODE -ne 0) { throw "Failed to apply CRD" }

    Write-Host "  Waiting for CRD to be established..." -ForegroundColor Yellow
    $waitArgs = @("wait", "--for=condition=established", "--timeout=60s", "crd/gatewayservers.gateway.boost.io")
    if ($KubeContext) { $waitArgs += "--context=$KubeContext" }
    & kubectl $waitArgs
    Write-Host "  CRD ready." -ForegroundColor Green
}

function Deploy-Operator {
    Write-Host "[4/5] Deploying Operator Deployment..." -ForegroundColor Yellow

    $operatorDeployment = @"
apiVersion: apps/v1
kind: Deployment
metadata:
  name: gateway-server-operator
  namespace: $Namespace
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
          image: $Image
          imagePullPolicy: IfNotPresent
          args:
            - python
            - /app/operator.py
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
          securityContext:
            runAsNonRoot: true
            runAsUser: 1000
"@

    $kubeArgs = @("apply", "-f", "-")
    if ($KubeContext) { $kubeArgs += "--context=$KubeContext" }
    $operatorDeployment | kubectl $kubeArgs

    Write-Host "  Waiting for operator Pod to become ready..." -ForegroundColor Yellow
    $waitArgs = @("wait", "--for=condition=available", "--timeout=120s", "-n", $Namespace, "deployment/gateway-server-operator")
    if ($KubeContext) { $waitArgs += "--context=$KubeContext" }
    & kubectl $waitArgs
    Write-Host "  Operator ready." -ForegroundColor Green
}

function Deploy-SampleCR {
    Write-Host "[5/5] Creating sample GatewayServer Custom Resource..." -ForegroundColor Yellow

    $sampleCR = @"
apiVersion: gateway.boost.io/v1
kind: GatewayServer
metadata:
  name: my-gateway-server
  namespace: $Namespace
spec:
  replicaCount: 2
  image: gateway-server:latest
  service:
    type: ClusterIP
    port: 8080
  config:
    logLevel: info
    backendEndpoint: "backend-service:9090"
"@

    $kubeArgs = @("apply", "-f", "-")
    if ($KubeContext) { $kubeArgs += "--context=$KubeContext" }
    $sampleCR | kubectl $kubeArgs

    Write-Host "  Sample CR applied. Check status with:" -ForegroundColor Green
    Write-Host "    kubectl get gatewayservers -n $Namespace"
    Write-Host ""
    Write-Host "  Watch operator logs with:" -ForegroundColor Green
    Write-Host "    kubectl logs -n $Namespace -l app.kubernetes.io/name=gateway-server-operator"
}

# ---- Main Execution ----

try {
    Ensure-Namespace
    Deploy-RBAC
    if (-not $SkipCrd) {
        Deploy-CRD
    }
    Deploy-Operator
    Deploy-SampleCR

    Write-Host ""
    Write-Host "=== Deployment Complete ===" -ForegroundColor Cyan
    Write-Host "All resources deployed successfully in namespace '$Namespace'."
} catch {
    Write-Host ""
    Write-Host "ERROR: $_" -ForegroundColor Red
    exit 1
}
