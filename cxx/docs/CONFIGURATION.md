# PriceRiot Store Configuration (`store.yaml`)

This document describes the core pieces of the `store.yaml` configuration used by
the C++ engine (see also `cxx/environment/environment.cpp`, `shelf.cpp`,
`store_init.cpp`, and `checkout_queue.cpp`).

At a high level:

- The store is a **graph** of `nodes` and `edges`.
- Each edge is discretised into **cells** which may host shelves and traffic.
- A **planogram** assigns SKUs to shelf sides on particular edges.
- Optional `checkout_queues` sections configure FIFO queues at registers.

## Top-level structure

```yaml
nodes:        # Required: list of store nodes (entrances, registers, etc.)
  - ...
edges:        # Required: list of edges (aisles) between nodes
  - ...
planogram:    # Required: per-edge shelf layout and SKUs
  edges:
    "1": ...
left_side:    # Optional: fallback shelf definition for edges not in planogram.edges
right_side:   # Optional: fallback shelf definition for edges not in planogram.edges
checkout_queues:  # Optional: register queue configuration
  - ...
```

## Nodes

`nodes` define logical areas in the store: entrances, exits, junctions,
registers, etc. Example:

```yaml
nodes:
  - id: 1
    type: Entrance        # Entrance | Exit | Junction | Register | Stockroom
    x: 0.0
    z: 0.0
    length: 4.0
    width: 3.0
    shelf_left: 0.0
    shelf_right: 0.0
    blocked: 0.0
    personal_space: 1.0
    jam_density: 3.5
    dwell_s: 0.0
    service_rate: 0.0
    agents: 0
    entry_rate: 1.0
    exit_rate: 0.0
```

- `id`: Unique integer node ID.
- `type`: Logical role; influences agent spawning and routing.
- `(x, z)`: World-space centre in metres.
- `length`, `width`: Physical footprint used for layout and navmesh.
- `personal_space`, `jam_density`: Traffic model parameters.
- `entry_rate`, `exit_rate`: Control spawn/exit behaviour at entrances/exits.

## Edges (aisles)

`edges` connect nodes and represent walkable aisles with optional shelves:

```yaml
edges:
  - id: 2
    from: 2
    to: 3
    length: 10.0
    width: 3.0
    free_speed: 1.2
    jam_density: 3.5
    blocked: 0.0
    flow: bi         # uni | bi
    orientation: fwd # fwd | rev (visualisation hint)
    shelf_left: 0.5  # shelf protrusion on left in metres
    shelf_right: 0.5 # shelf protrusion on right in metres
```

- `id`: Unique integer edge ID.
- `from`, `to`: Node IDs forming the graph.
- `length`, `width`: Physical aisle geometry.
- `shelf_left`, `shelf_right`: Shelf protrusion depths used to generate
  stalls, navmesh obstacles, and capacity.

## Planogram

The `planogram` section assigns SKUs to shelf bays on particular edges. For
each edge ID (as a string), you specify `left_side` and `right_side`:

```yaml
planogram:
  edges:
    "2":
      left_side:
        bay_count: 1
        planogram:
          - bay: 0
            face: 0
            slot: 0
            sku: 1
            on_shelf_qty: 10
      right_side:
        bay_count: 1
        planogram:
          - bay: 0
            face: 0
            slot: 0
            sku: 2
            on_shelf_qty: 10
```

Each entry describes how many bays exist on that side and which SKUs occupy
which `(bay, face, slot)` positions, along with initial on-shelf quantities.

`left_side` and `right_side` sections at the top level act as **fallbacks**
for any edges not explicitly listed in `planogram.edges`.

## Checkout queues

Checkout queues are configured in a `checkout_queues` array, each entry
describing one lane bound to a register node:

```yaml
checkout_queues:
  - register_id: 7      # Node ID of the Register
    processing_time: 5.0
    waypoints:
      - { x: 30.0, z: 0.0 }   # Position 0: at counter
      - { x: 28.5, z: 0.0 }   # Position 1: first in queue
      - { x: 27.0, z: 0.0 }   # Position 2, etc.
```

- `register_id`: Must match the `id` of a `Register` node.
- `processing_time`: Average checkout time in seconds for that lane.
- `waypoints`: World-space points where queued customers stand, in order.

The engine uses these definitions via `CheckoutQueueManager` to:

- Allow customers to select a lane based on distance + queue length.
- Place customers at the correct waypoint for their position.
- Advance queues when checkout completes.

## Example scenarios

Package multiple layouts under `examples/` for easy scenario switching, e.g.:

- `examples/store_tiny.yaml` – minimal store with a single aisle and register.
- `examples/store_bottleneck.yaml` – layout with a clear narrow bottleneck.
- `examples/store_wide.yaml` – more spacious aisles for lower congestion.

You can load any of these by passing the path to the `Simulator` constructor
in Python or via the visualiser (see `sim.cpp`), e.g.:

```bash
./simulator examples/store_bottleneck.yaml
```

