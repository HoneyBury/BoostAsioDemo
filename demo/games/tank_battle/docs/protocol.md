# Tank Battle — Protocol

## Framework Envelope

All tank messages travel inside the framework `realtime_instance` envelope.
The framework routes by `instance_id` and `payload_type`; it does not
interpret the tank payload.

```json
{
  "domain": "realtime_instance",
  "operation": "input",
  "instance_id": "room_001:battle_001",
  "payload_type": "tank.input",
  "payload": "{...}"
}
```

## Tank Input Payload

Sent by the client each tick (or on action):

```json
{
  "seq": 42,
  "actions": [
    {"type": "move", "dx": 0, "dy": -1},
    {"type": "fire", "direction": 0}
  ]
}
```

### Actions

| type | fields | description |
|------|--------|-------------|
| `move` | `dx`, `dy` | Move tank by (dx, dy). Each must be -1, 0, or 1. |
| `fire` | `direction` | Fire bullet in direction (0=up, 90=right, 180=down, 270=left). |
| `stop` | none | Stop movement this tick. |

## Tank Snapshot Payload (push)

Pushed to all players each tick:

```json
{
  "frame": 150,
  "tanks": [
    {"user_id": "alice", "x": 5, "y": 3, "hp": 100, "direction": 0, "alive": true},
    {"user_id": "bob",   "x": 12,"y": 8, "hp": 50,  "direction": 180, "alive": true}
  ],
  "bullets": [
    {"id": "b_17", "x": 7, "y": 4, "dx": 0, "dy": -1}
  ],
  "events": [
    {"type": "bullet_hit", "bullet_id": "b_17", "target": "bob", "damage": 25}
  ],
  "finished": false
}
```

## Settlement Payload

Sent by the tank plugin when the battle ends:

```json
{
  "battle_id": "battle_001",
  "room_id": "room_001",
  "reason": "last_standing",
  "players": [
    {
      "user_id": "alice",
      "display_name": "Alice",
      "kills": 3,
      "deaths": 1,
      "damage": 1200,
      "score": 1350,
      "win": true
    }
  ]
}
```

## Room Protocol

Room operations use the framework `backend_envelope` protocol. Operations are
routed by `message_type`; the framework does not interpret the room payload.

### room_list

List rooms with optional filters and pagination:

```json
{
  "message_type": "room_list",
  "payload": {
    "visibility": "public",
    "status": "waiting",
    "page": 1,
    "page_size": 20
  }
}
```

Response:

```json
{
  "status": "ok",
  "rooms": [{"room_id": "...", "member_count": 2, ...}],
  "total": 10,
  "page": 1,
  "page_size": 20,
  "total_pages": 1
}
```

### room_detail

Get full room details including members:

```json
{
  "message_type": "room_detail",
  "payload": {"room_id": "room_001"}
}
```

### room_kick

Owner removes a member from the room:

```json
{
  "message_type": "room_kick",
  "payload": {"user_id": "alice", "room_id": "room_001", "target_user_id": "bob"}
}
```

### transfer_owner

Transfer room ownership to another member:

```json
{
  "message_type": "transfer_owner",
  "payload": {"user_id": "alice", "room_id": "room_001", "new_owner_id": "bob"}
}
```

### Settlement

The battle settlement is delivered as a `settlement` push after the instance
finishes. The settlement payload structure is defined in the settlement section below.

## Error Codes

| code | meaning |
|------|---------|
| 4001 | invalid action |
| 4002 | move out of bounds |
| 4003 | fire cooldown active |
| 4004 | tank not found |
| 4005 | battle not found |
| 4006 | battle already finished |
