#!/usr/bin/env bash
# ============================================================================
# Boost Gateway -- Rollout Verification Script
#
# Verifies a gateway-server rollout completed successfully:
#   1. kubectl rollout status --timeout=300s
#   2. All Pods are Ready
#   3. Service Endpoints are populated
#   4. Health check endpoint /healthz returns 200
#
# Usage:
#   ./k8s/rollout-verify.sh                    # uses defaults
#   ./k8s/rollout-verify.sh -n my-ns -d my-deployment -p 8080
#   ./k8s/rollout-verify.sh --help
# ============================================================================

set -euo pipefail

# ─── Defaults ──────────────────────────────────────────────────────────────

NAMESPACE="boost-gateway"
DEPLOYMENT="gateway-server"
SERVICE="gateway-server"
PORT=8080
HEALTH_PATH="/healthz"
TIMEOUT="300s"

# ─── Argument parsing ─────────────────────────────────────────────────────

usage() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Verify a gateway-server rollout.

Options:
  -n NAME      Namespace (default: $NAMESPACE)
  -d NAME      Deployment name (default: $DEPLOYMENT)
  -s NAME      Service name (default: $SERVICE)
  -p PORT      Service port (default: $PORT)
  -t TIMEOUT   kubectl rollout timeout (default: $TIMEOUT)
  --help       Show this help message
EOF
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -n) NAMESPACE="$2"; shift 2 ;;
        -d) DEPLOYMENT="$2"; shift 2 ;;
        -s) SERVICE="$2"; shift 2 ;;
        -p) PORT="$2"; shift 2 ;;
        -t) TIMEOUT="$2"; shift 2 ;;
        --help) usage ;;
        *) echo "Unknown option: $1"; usage ;;
    esac
done

# ─── Helper functions ─────────────────────────────────────────────────────

pass() { echo -e "  [PASS] $1"; }
fail() { echo -e "  [FAIL] $1"; exit 1; }
info() { echo -e "[INFO] $1"; }

# ─── Main verification steps ──────────────────────────────────────────────

echo ""
echo "=== Gateway Server Rollout Verification ==="
echo "  Namespace:   $NAMESPACE"
echo "  Deployment:  $DEPLOYMENT"
echo "  Service:     $SERVICE"
echo "  Port:        $PORT"
echo "  Timeout:     $TIMEOUT"
echo ""

# ─── Step 1: Rollout status ──────────────────────────────────────────────

STEP=1
info "Step $STEP: Checking rollout status..."
if kubectl rollout status "deployment/$DEPLOYMENT" \
    -n "$NAMESPACE" --timeout="$TIMEOUT" > /dev/null 2>&1; then
    pass "Rollout completed successfully"
else
    fail "Rollout did not complete within $TIMEOUT. Check: kubectl describe deployment/$DEPLOYMENT -n $NAMESPACE"
fi
STEP=$((STEP + 1))

# ─── Step 2: Pod readiness ───────────────────────────────────────────────

info "Step $STEP: Verifying all Pods are Ready..."
TOTAL_PODS=$(kubectl get pods -n "$NAMESPACE" \
    -l "app.kubernetes.io/instance=$DEPLOYMENT" \
    -o jsonpath='{.items[*].status.containerStatuses[*].ready}' 2>/dev/null | tr ' ' '\n' | wc -l)
READY_PODS=$(kubectl get pods -n "$NAMESPACE" \
    -l "app.kubernetes.io/instance=$DEPLOYMENT" \
    -o jsonpath='{.items[*].status.containerStatuses[*].ready}' 2>/dev/null | tr ' ' '\n' | grep -c "true" || true)

if [ "$TOTAL_PODS" -eq 0 ]; then
    fail "No Pods found for deployment $DEPLOYMENT in namespace $NAMESPACE"
fi

if [ "$TOTAL_PODS" -eq "$READY_PODS" ]; then
    pass "All $READY_PODS/$TOTAL_PODS Pods are Ready"
else
    fail "Only $READY_PODS/$TOTAL_PODS Pods are Ready. Check: kubectl get pods -n $NAMESPACE"
fi
STEP=$((STEP + 1))

# ─── Step 3: Service endpoints ───────────────────────────────────────────

info "Step $STEP: Verifying Service endpoints..."
ENDPOINTS=$(kubectl get endpoints "$SERVICE" -n "$NAMESPACE" \
    -o jsonpath='{.subsets[*].addresses[*].ip}' 2>/dev/null | tr ' ' '\n' | wc -l)

if [ "$ENDPOINTS" -gt 0 ]; then
    pass "Service $SERVICE has $ENDPOINTS endpoint(s)"
else
    fail "Service $SERVICE has no endpoints. Check: kubectl describe service/$SERVICE -n $NAMESPACE"
fi
STEP=$((STEP + 1))

# ─── Step 4: Health check ────────────────────────────────────────────────

info "Step $STEP: Verifying health check endpoint..."
# Pick the first ready Pod
POD_NAME=$(kubectl get pods -n "$NAMESPACE" \
    -l "app.kubernetes.io/instance=$DEPLOYMENT" \
    -o jsonpath='{.items[0].metadata.name}' 2>/dev/null)

if [ -z "$POD_NAME" ]; then
    fail "No Pods found for health check"
fi

# Port-forward to check health (runs in background, cleaned up after)
kubectl port-forward "pod/$POD_NAME" -n "$NAMESPACE" \
    --address 127.0.0.1 "$PORT:$PORT" > /dev/null 2>&1 &
PF_PID=$!
trap 'kill $PF_PID 2>/dev/null || true' EXIT

# Wait for port-forward to be ready
sleep 2

# Perform health check
HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" \
    --connect-timeout 5 \
    --max-time 10 \
    "http://127.0.0.1:$PORT$HEALTH_PATH" 2>/dev/null || true)

# Stop port-forward
kill "$PF_PID" 2>/dev/null || true
trap - EXIT

if [ "$HTTP_CODE" = "200" ]; then
    pass "Health check endpoint $HEALTH_PATH returned 200"
else
    fail "Health check endpoint $HEALTH_PATH returned $HTTP_CODE (expected 200)"
fi

# ─── Summary ──────────────────────────────────────────────────────────────

echo ""
echo "=== Rollout Verification Complete ==="
echo "All checks passed. Gateway server is healthy and ready."
echo ""
