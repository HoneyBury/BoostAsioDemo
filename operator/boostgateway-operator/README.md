# BoostGateway Operator

This directory contains the first runnable scaffold for a `BoostGatewayCluster`
operator built with `controller-runtime`.

## Scope

- `BoostGatewayCluster` root custom resource
- Reconciles `Deployment` + `Service` pairs for:
  - `gateway`
  - `login`
  - `room`
  - `battle`
- Reconciles `StatefulSet` + headless `Service` pairs for:
  - `match`
  - `leaderboard`
- Reconciles per-service `ConfigMap` objects and injects them via `envFrom`
- Reconciles a placeholder TLS `Secret` when `.spec.tls.enabled=true`
- Injects Raft peer environment variables for stateful components:
  - `RAFT_NODE_ID`
  - `RAFT_PEERS`
  - `RAFT_ELECTION_TIMEOUT_MIN_MS`
  - `RAFT_ELECTION_TIMEOUT_MAX_MS`
  - `RAFT_HEARTBEAT_INTERVAL_MS`
- Updates `.status.phase`, `.status.readyReplicas`, and a basic `Ready` condition
- Designed for local development on `kind`

## Layout

- `api/v1alpha1/`: API types and scheme registration
- `internal/controller/`: reconcile loop
- `config/`: install manifests
- `hack/kind-config.yaml`: local cluster bootstrap

## Local workflow

```bash
kind create cluster --config hack/kind-config.yaml
kubectl create namespace boost-gateway
kubectl apply -f config/crd/bases/gateway.boost.io_boostgatewayclusters.yaml
kubectl apply -k config/default
kubectl apply -f config/samples/gateway_v1alpha1_boostgatewaycluster.yaml
```

## Next steps

- Replace fake-client controller tests with `envtest`
- Wire TLS and cert-manager objects
- Reconcile `status.conditions` from actual pod readiness and rollout state
