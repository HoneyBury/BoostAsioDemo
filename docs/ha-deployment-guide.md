# High-Availability Deployment Guide

**Version**: 3.4.0

## Overview

This guide covers deploying Boost Gateway services with high availability using Raft consensus for stateful services, multi-instance gateway load balancing, and proper failover configuration.

## Architecture

```
                    +----------------+
                    |  Load          |
                    |  Balancer      |
                    |  (nginx/HA)    |
                    +--------+-------+
              +-------------+------------+
              v             v            v
        +----------+ +----------+ +----------+
        | Gateway  | | Gateway  | | Gateway  |
        | Instance1| | Instance2| | Instance3|
        +----+-----+ +----+-----+ +----+-----+
             |            |            |
        +----+------------+------------+----+
        |  Backend Services (active-active) |
        |  Login | Room | Battle           |
        +-----------------------------------+
             |            |            |
        +----+------------+------------+----+
        |  Raft Cluster (leader-follower)   |
        |  Matchmaking (3 nodes)            |
        |  Leaderboard (3 nodes)            |
        +-----------------------------------+
```

## Prerequisites

- 3+ machines or Kubernetes nodes
- Network connectivity between all Raft nodes (low latency recommended)
- Persistent storage for Raft logs
- (Optional) Redis for leaderboard offloading

## Configuration

### Raft Cluster Setup

Each stateful service (matchmaking, leaderboard) requires 3 nodes for fault tolerance.

#### Node 1 (config/environments/ha/matchmaking-node1.json)
```json
{
    "port": 9104,
    "raft": {
        "node_id": "mm-node1",
        "peers": [
            {"id": "mm-node1", "host": "10.0.0.1", "port": 9104},
            {"id": "mm-node2", "host": "10.0.0.2", "port": 9104},
            {"id": "mm-node3", "host": "10.0.0.3", "port": 9104}
        ],
        "election_timeout_min_ms": 150,
        "election_timeout_max_ms": 300,
        "heartbeat_interval_ms": 50
    }
}
```

Node 2 and 3 follow the same pattern with their own IP.

#### Gateway Configuration
```json
{
    "port": 9201,
    "backends": {
        "login": {"host": "10.0.0.1", "port": 9101},
        "room": {"host": "10.0.0.1", "port": 9102},
        "battle": {"host": "10.0.0.1", "port": 9103},
        "matchmaking": {"host": "10.0.0.1", "port": 9104},
        "leaderboard": {"host": "10.0.0.1", "port": 9105}
    }
}
```

## Failover Behavior

### Raft Leader Election
- If leader fails, election completes within 150-300ms
- During election, the service returns `not_raft_leader` with `leader_hint`
- Client SDK retries with the leader hint
- New leader resumes state from replicated log

### Gateway Failover
- Gateways are stateless and sit behind a load balancer
- Session state is lost on gateway failure (clients must reconnect)
- Use sticky sessions (optional) for better client experience

### Backend Failover
- Stateless backends (login, room, battle) can be restarted independently
- ClusterRouter detects unhealthy backends via TCP health checks (5s interval)
- Gateway automatically routes to healthy instances

## Deployment Steps

### Linux (systemd)
Example service files are in `config/environments/production/`.

### Kubernetes
See `k8s/helm/gateway-server/` for the full Helm chart.

The operator (`k8s/operator/operator.py`) manages:
- GatewayServer CRD reconciliation
- Backend Deployment scaling
- TLS certificate rotation
- Health check monitoring

## Verification

```bash
# Check Raft leader
curl http://localhost:9104/raft/status

# Check cluster health
curl http://localhost:9201/health
curl http://localhost:9201/ready

# Verify Raft replication
curl http://localhost:9105/raft/status

# Run Raft E2E test
ctest -R raft_e2e -C Release
```

## See Also
- [Service Discovery Guide](service-discovery-guide.md)
- [Redis-Raft HA Runbook](redis-raft-ha-runbook.md)
- [K8s Deployment Guide](k8s-deployment-guide.md)
