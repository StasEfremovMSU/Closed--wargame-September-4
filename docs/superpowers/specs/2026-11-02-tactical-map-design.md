# Tactical Map Format for BattleCore and Unreal

**Date:** 2026-11-02. Revised 2026-11-23.

**Status:** Proposed design for review.

**Scope:** Tactical terrain, officer queries, observation knowledge, and command message delivery.

## Intent

Create a new engine-independent tactical map format that supplies only the environmental data needed by officers and simulation queries. Unreal may consume the same stable map features for presentation, while its meshes, materials, actors, and Mass entities remain in a separate visual layer.

The immediate goal is to support deterministic movement, terrain-aware visibility, tactical position evaluation, and communication availability/delay. The map must be authorable before Unreal Editor is installed and usable by the current C++ BattleCore.

## Current State

- `Scenario` contains only axis-aligned rectangular `Obstacle` records for terrain. There is no map identity, extent, terrain classification, elevation layer, communication environment, or authored region hierarchy.
- `GridNavigationService` creates an implicit grid with a 2 m cell size and defaults to 256 x 256 cells. Its movement cost is uniform; the CLI selects the flat route service when there are no obstacles.
- Officer perception checks distance and 2D line of sight between regiment centres. It ignores elevation and returns one terrain assessment at the own-force centre with binary passability.
- Communication payloads follow the command hierarchy. Ordinary messages currently use a fixed one-tick delay per hierarchy edge. Courier delivery computes a route and travel duration, but does not animate a body. Voice and Signal do not yet have distinct transport behavior.
- The legacy `map.txt` importer describes regiment placements and composition; it is not a terrain-map source.
- The Unreal adapter contract assigns simulation authority to BattleCore and presentation to Mass. Core coordinates are metres, X forward, Y right, Z up. Core IDs are stable values, not Unreal entity handles.

## User Decisions

1. The project will have separate visual and tactical map layers; the tactical layer is the current scope.
2. Static terrain is known to all commanders. Unit visibility and dynamic events are local and may reach other officers by reports.
3. Command authority remains hierarchical. A task or report moves along the permitted command edge; its physical channel controls availability and delivery delay.
4. Channel behavior is defined by scenario profiles. The initial channel set includes voice, visual flags, acoustic signals, and courier.
5. Tactical terrain uses a hybrid representation: authored areas/lines/points are the source data, and deterministic tiled query layers are derived from them.

## Approaches Considered

### A. Cell grid as the authored source

Each map cell directly stores terrain and query values. This gives predictable query speed and straightforward Unreal import, but is awkward to edit by hand, makes broad features repetitive, and ties authors to a chosen resolution.

### B. Feature geometry only

Roads, rivers, forests, structures, and heights are authored as geometric features and queried directly. This stays compact and editable but repeats geometry tests during pathfinding and visibility, especially when many regiments query the same map.

### C. Authored features plus deterministic tile compilation — selected

The versioned source stores stable-ID geometric features and typed properties. BattleCore compiles them into immutable, chunked, structure-of-arrays query layers at load. Unreal can read the authored features and map their IDs to presentation assets. The compiled tile data is a cache, not a second source of truth.

## Proposed Data Model

### Tactical map asset

Use a text JSON authoring file for the first version, tentatively `*.tmap.json`. JSON is the proposed default because Unreal Editor is not currently available, existing BattleCore tools already use JSON, and maps need to remain inspectable in source control. Binary compression is deferred until map-load and memory measurements justify it.

The asset has its own `format_version`, separate from scenario, snapshot, and replay versions:

```json
{
  "format_version": 1,
  "map_id": "battlefield.example",
  "name": "Example Battlefield",
  "coordinate_system": "battlecore_m_xyz_yaw_rad",
  "origin_m": { "x": -512, "y": -512, "z": 0 },
  "extent_m": { "x": 1024, "y": 1024 },
  "cell_size_m": 2,
  "chunk_cells": 64,
  "default_terrain": "open_ground",
  "features": []
}
```

The explicit coordinate-system tag prevents accidental reapplication of Unity or Unreal axis conversions. Positions and heights use metres; orientation, if required by a feature, uses radians. `origin_m`, extents, cell size, and all feature geometry are validated against finite values and map bounds.

### Feature geometry

Every feature has a stable nonzero `feature_id`, a semantic `kind`, optional author-facing name/tags, and geometry in core coordinates. Initial geometry types are point, polyline, and closed polygon. A line that changes a terrain layer has a positive `width_m` defining a strip around it. Points mark named locations. Candidate kinds include terrain region, road, river, crossing/bridge, wall, building, forest, ridge, objective, and deployment area.

Features carry tactical properties, not rendering details. Initial properties should be composable and typed:

- ground elevation or a reference to an elevation surface;
- obstacle/occluder height and visual opacity;
- movement passability and positive movement cost for walkable terrain, by movement profile;
- concealment and cover estimates;
- sound attenuation;
- signal attenuation for visual/acoustic channel calculations where applicable.

The map initializes cells from `default_terrain` and from its base height surface when supplied. The flat default uses `origin_m.z`. A terrain preset expands to a documented set of properties. An explicitly authored property overrides the same property from the feature's preset. The loader validates ranges and units.

### Overlapping features

The proposed v1 rule resolves each property independently. A road can change movement cost through a forest while the forest still affects cover, visibility, and sound.

| Property | Composition rule |
|---|---|
| Surface height, terrain label, movement cost and soft passability per movement profile | Use the value from the covering feature with the highest `priority`. The default priority is 0. Ties go to the lower `feature_id`. An omitted property has no effect. |
| Solid blockers | Combine by union. A bridge may explicitly override a water restriction for its allowed movement profiles. A wall intersecting that bridge still blocks movement. |
| Cover and acoustic attenuation | Use the largest authored value of each property per cell. Accumulate acoustic attenuation by the distance traveled inside each cell. |
| Optical occlusion | Index occluders per cell and retain each feature's height interval. A fully opaque feature blocks sight where the ray intersects it. For overlapping partial occluders, accumulate the strongest applicable attenuation per unit of ray length. |
| Tactical tags | Form a deduplicated, sorted set. |

Ground is single surface in v1. A bridge supplies the deck height and a walkable strip over the flagged water for allowed profiles. Its usable width must fit the formation footprint. For safety, the final segment check uses the indexed source geometry for narrow crossings and solid blockers. Adjacent cells and tile seams follow the same coverage rules. Results depend on feature data and stable IDs, independent of file or tile processing order.

### Compiled tactical tiles

On load, rasterize features into deterministic square chunks. Initial proposal: 64 x 64 cells per chunk and 2 m cells by default, with map-specific cell size in the header. The design keeps resolution configurable; it does not promise that 2 m is sufficient for every battlefield.

Runtime query layers are stored as separate contiguous arrays per chunk rather than one padded object per cell. Minimum v1 layers:

| Layer | Query purpose |
|---|---|
| Ground height and obstruction height | Sight lines, elevation advantage, slope checks |
| Terrain/preset ID | Resolve movement, vision, cover, and sound defaults |
| Traversal flags and movement costs | Path cost and footprint passability |
| Visual opacity/cover | Visibility and concealment estimates |
| Acoustic attenuation | Voice/drum/trumpet propagation estimates |
| Visual-signal attenuation | Flags/beacons; normally requires optical visibility |

Compilation must be deterministic for the same source map and compiler version. Chunk coordinates and cell ordering are canonical. Cache data can be rebuilt; authored features remain the source of truth.

## Tactical Query Interface

Expose one engine-independent `TacticalMapQuery` service so officers, navigation, and communication use the same map representation:

- `SampleTerrain(position)` -> terrain preset, height, slope, cover, passability, and tactical tags;
- `TraceVisibility(observer, target, observer_profile)` -> visible/blocked plus confidence or an obstruction reason;
- `EvaluateAcousticPath(source, receiver, channel_profile)` -> reachability and deterministic attenuation;
- `EvaluateVisualSignal(source, receiver, channel_profile)` -> line of sight and signal quality;
- `MovementCost(from, to, movement_profile, formation_footprint)` and `CanTraverse(...)`;
- `FindPath(request)` -> stable waypoint sequence and failure reason;
- `FindTacticalPositions(area, constraints)` -> deterministic candidate positions with explainable scores.

`TraceVisibility` is a terrain ray query using observer/target height, ground elevation, opaque feature volumes, and the configured observer range. Start with a heightfield plus simple opaque feature geometry: this is 2.5D tactical visibility, not voxel simulation or Unreal NavMesh. FOV, eye height, and sensor range belong to an observer profile, not to terrain data or the existing command radius.

Pathfinding uses movement cost, profile passability and the formation footprint. Detailed individual soldier collision remains in the physical movement layer.

## Knowledge and Dynamic Effects

Separate physical truth from what an officer knows:

- immutable static terrain is globally known, per the user decision;
- dynamic world effects (smoke, fire, temporary obstruction, a destroyed crossing) live in a runtime overlay with tick/revision and affect actual queries;
- each command node's information state records only dynamic effects and contacts it directly observed or received in reports, with source and observation tick;
- an officer may plan using globally known terrain and stale/uncertain local reports, but may not read hidden enemy `World` records through map queries.

V1 defines the overlay/query boundary and knowledge records. It does not add full smoke, fire, weather, destruction, or concealment simulation. An omitted dynamic layer means no dynamic effect; it is not an unknown static terrain value.

## Communication Profiles and Delivery

The map provides environmental propagation and route queries. Scenario data defines channel policy, profile values and endpoint capabilities. The communication runtime validates command authority and uses the environment to calculate delivery.

Each profile has a stable `channel_id`, modality (`voice`, `visual`, `acoustic`, or `courier`), availability/range rules, minimum delay, and relevant environment parameters. Flags require an optical link. Voice and acoustic signals use range and acoustic attenuation. Courier travel time uses the route and the living messenger's movement speed.

Scenario v1 assigns send and receive capability lists to command positions by `NodeId`. The defaults apply to every position without an override. A listed position replaces both lists. Its capabilities remain attached to the position when a successor takes command. A living commander must occupy the sending and receiving positions for dispatch. A courier profile also requires a living messenger assigned to the sending position. The messenger must be available for dispatch. The availability and identity of an actual person are runtime facts.

An illustrative scenario fragment uses the following structure:

```json
{
  "communications": {
    "channel_profiles": [
      { "channel_id": "voice", "modality": "voice", "priority": 10,
        "max_range_m": 40, "min_delay_ticks": 2 },
      { "channel_id": "flags", "modality": "visual", "priority": 20,
        "max_range_m": 300, "min_delay_ticks": 4 },
      { "channel_id": "horn", "modality": "acoustic", "priority": 30,
        "max_range_m": 180, "min_delay_ticks": 6 },
      { "channel_id": "runner", "modality": "courier", "priority": 40,
        "min_delay_ticks": 1, "arrival_tolerance_m": 6 }
    ],
    "default_endpoints": { "send": ["voice"], "receive": ["voice"] },
    "endpoint_overrides": [
      { "node_id": 3, "send": ["voice", "flags", "horn", "runner"],
        "receive": ["voice", "flags", "horn", "runner"], "courier_unit_id": 42 },
      { "node_id": 4, "send": ["voice", "flags"],
        "receive": ["voice", "flags", "horn", "runner"] }
    ],
    "retry_interval_ticks": 5,
    "default_message_ttl_ticks": 200
  }
}
```

Numbers in the example are illustrative. With a 50 ms tick, a minimum delay of four ticks is 200 ms.

1. Check the existing authority rule for the adjacent command edge. Take the intersection of the sender's `send` and receiver's `receive` lists.
2. Try eligible profiles in ascending `(priority, channel_id)` order. Voice and acoustic profiles check range and sound travel through the terrain. Flags check range and an optical trace including smoke when present. A courier checks the current map route for its movement profile and computes arrival from travel time. One messenger handles one in-flight envelope at a time.
3. If all channels are unavailable, retry at the configured interval. The message expires at the earlier of its default lifetime and any explicit deadline. Expiry at the same tick as delivery wins, consistent with the existing queue.
4. Fix the selected profile and arrival tick on dispatch. For voice, flags and acoustic signals, evaluate the link at send time. At delivery, check that the recipient position has a living commander. Courier transit is abstract in v1, using a recorded route, destination and arrival estimate. It fails if the messenger dies, the route is blocked before arrival, or the recipient has moved outside the profile's arrival tolerance.

Persist the selected profile, dispatch tick, next retry or arrival tick, terminal status, and courier transit data where present. Scheduling ties use the stable message ID. A delayed report retains the tick of its original observation.

V1 does not model radio, jamming, interception, encryption, probabilistic failure, or message distortion. Channel profiles are deterministic; any future randomness must be keyed by stable message/channel/tick identifiers and recorded for replay.

## Scenario, Save, Replay, and Unreal Boundary

The scenario references a tactical map by `map_id`, format version, and content hash. Store the immutable map once as an asset, not as a copy in every `World` snapshot. Dynamic overlay and officer knowledge are runtime state and belong in snapshots and `StateHash`.

For portability, replay/output bundles must include or resolve the exact map asset by content hash; a path alone is not identity. The initial CLI should accept a relative asset path, validate that the resolved map remains within the configured scenario/bundle root, and record its hash. A later asset registry may replace path lookup without changing map IDs or query semantics.

Unreal imports stable feature IDs and may associate them with meshes/materials in a separate visual map. No `UObject`, `FVector`, `FMassEntityHandle`, or actor/pool identity is serialized into the tactical asset. Coordinate conversion occurs at the adapter boundary once. Mass remains presentation-only until the separately planned UE integration proves a different authority model.

Compatibility path: existing `Scenario.obstacles` become obstacle polygon/rectangle features during migration. The legacy placement `map.txt` importer remains separate. Existing scenarios without a tactical map keep the current flat/obstacle behavior during a migration window; the map-aware service becomes opt-in until fixtures and replay migration are complete.

## Validation and Determinism

Reject unsupported map versions, duplicate or zero feature IDs, malformed or self-intersecting polygons, unclosed polygon rings, invalid line strings, out-of-bounds geometry, NaN/Infinity, nonpositive traversal costs on walkable surfaces, invalid opacity/range values, unknown preset/profile IDs, and inconsistent chunk dimensions. Reject duplicate channel IDs and endpoint entries, unknown command or courier IDs, and couriers assigned to an endpoint of a different team. Diagnostics identify the input file, record, property, and location where available.

The map content identity is the hash of the normalized authoring data. Reordering keys or stable-ID features leaves it unchanged. Derived caches are keyed by map content identity and compiler version. Stable serialization and compilation are independent of hash-map iteration, worker scheduling, and Unreal object creation order. `StateHash` includes map content identity and dynamic map state.

## Acceptance Criteria

1. A text map can describe bounded terrain with stable features, elevation/occlusion, movement costs, and communication environment.
2. Identical map assets compile to identical tile content and hashes.
3. A hill blocks optical visibility while allowing configured acoustic communication. A forest can provide cover, affect concealment, and attenuate sound. A road crossing that forest changes movement cost while preserving the other effects. Reordering features leaves the resulting queries identical.
4. Impassable water blocks a formation except at a crossing compatible with its movement profile and footprint. A solid wall still blocks a crossing where the two intersect.
5. Officer situation includes terrain candidates around the force, not only one passability sample at its centre, while enemy contacts remain visibility-limited and timestamped.
6. Orders and reports follow adjacent command edges. Tests cover all four channels, a missing receiving capability, an unavailable link that recovers before expiry, inheritance of position capabilities after succession, and the expiry boundary.
7. Snapshot/resume/replay resolves the exact map hash and preserves dynamic overlay/known information deterministically.
8. Map query and path benchmarks report map dimensions, cell size, feature count, channels, and p50/p95/p99 query cost; no performance scale is claimed before measurement.
9. BattleCore builds and tests without Unreal installed. Unreal import tests later validate stable feature IDs and coordinates without moving simulation authority.

## Non-Goals

- Rendering, terrain materials, foliage instancing, landscape LOD, or map editing UI.
- Unreal NavMesh or collision replacing core pathfinding.
- Fully 3D voxel terrain or physical sound-wave simulation.
- Full fog-of-war on static terrain; static terrain is globally known.
- Neural officer decisions, probabilistic communications, or multi-player networking.
- Full simulation of weather, smoke, fire, structural destruction, supply, and camouflage in this first format task.

## Proposed Implementation Slices

1. Introduce map schema, strict loader/validator, stable IDs, content hash, and legacy obstacle conversion.
2. Compile features into immutable chunk layers; add elevation, line-of-sight, terrain samples, and deterministic tests.
3. Add movement profiles/costs and map-aware pathfinding; preserve footprint safety and `NoPath` semantics.
4. Add scenario-defined communication profiles and deterministic channel selection over adjacent command edges; use terrain queries for visual/acoustic channel conditions and map routes for couriers.
5. Add dynamic overlay/observer knowledge persistence, tactical position candidates, and CLI/replay bundling.
6. Add separate Unreal import validation and map query/performance benchmark after a compatible engine toolchain exists.

These slices are a proposed order, not an implementation plan. Implementation starts only after this design is approved.

## Open Calls for User Review

- JSON is the proposed source format while Unreal Editor is unavailable. A packed runtime asset can be added later without changing authored semantics.
- Initial 2 m cell/default and 64-cell chunk are proposed starting values, not performance conclusions.
- The first model is 2.5D heightfield plus explicit opaque feature geometry.
- Dynamic effects have a persisted data boundary but are not all simulated in v1.
- Channel selection uses scenario priority and stable ID. Position capabilities inherit through succession. Unavailable channels are retried within the message lifetime.
- Map assets are external, content-addressed inputs; portable replay bundles include or resolve the exact asset.
