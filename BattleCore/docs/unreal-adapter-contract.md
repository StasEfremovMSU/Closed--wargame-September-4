# Unreal / Mass adapter contract

This is a tested, engine-independent exchange contract. No Unreal module has been compiled, and no Mass performance or API compatibility is claimed. Select an installed UE version and its supported MSVC/SDK before implementing the host. Rebuild BattleCore with that toolchain; the current MinGW `.a` cannot be linked into a UE MSVC module.

## Authority and lifetime

BattleCore owns life, health, equipment, cooldowns, membership, orders, projectiles and the result of combat. Initially Mass owns presentation only. A render entity, Actor, pooled mesh or Pawn disappearing cannot delete the corresponding core unit or cause damage. A fallen core unit can remain in the frame as an `alive=false` record; the consumer chooses corpse representation.

Each Mass representation carries a `CoreUnitId` fragment (64-bit value), and optionally `CoreRegimentId`. Keep a map from CoreUnitId to a checked, generation-aware `FMassEntityHandle`; never equate the unit ID with a dense array index or a pooled Actor address. Validate handles before use. LOD changes and pooled object reuse preserve CoreUnitId; clear old associations on reuse. Destroy/create presentation through deferred command buffers after processing, at the engine-approved synchronization point. A pool slot reused for another soldier must acquire a new handle generation.

## Tick exchange

1. The host accumulates elapsed time and advances fixed 50 ms steps. Handle backlog explicitly; frame-rate-dependent variable timesteps are prohibited. Do not discard queued commands when catching up.
2. Collect commands into `CommandEnvelope{tick,sequence,command}`. Sequence IDs are unique within the run; save external inputs for replay. A malformed packet rejects the entire step before mutation. A valid gameplay command may be rejected with an event while others proceed.
3. Run `Step`, complete all writes and damage commit, then `Capture` and `BuildPresentation`. No simultaneous reads of a mutating World.
4. Move an owned `PresentationFrame` to the presentation consumer. Apply poses and tags to existing handles, schedule missing/dead representations, then flush deferred operations at the chosen safe point. Do not call core damage from visual attack/hit/death events.
5. Interpolate completed poses for display. A repeated frame/EventId is idempotent. Reject older frame ticks so a delayed frame cannot resurrect a presentation. Event IDs support deduplication; bounded retention requires a delivery acknowledgement/window. Do not trim IDs arbitrarily while old events can still arrive. Missing visual events can be recovered from the event log; pose frames are not a lossless event transport.

`PresentationFrame` is a value with its own vectors and copied event strings. It survives destruction or mutation of the source snapshot/world. Borrowed pointers/spans into World or Snapshot expire at the next mutation/destruction and must never be stored in UE objects across steps. A reusable frame pool may transfer ownership only after the previous consumer releases it. Copy cost includes all units, including dead records; an eventual delta stream needs a separate tested contract.

## Coordinates and possession

Core coordinates are metres, X forward, Y right, Z up; yaw is radians. Adapter position = core position *100, velocity likewise, and yaw degrees = radians *180/pi. Convert once on the boundary and invert once for external samples. Verify +X, +Y, +Z, 0 and pi/2 yaw in a real UE test. Do not apply Unity's axis conversion again to already imported core data.

Acquire/release/switch control only through recorded Step commands. Only one local controlled soldier exists. Possession preserves health, equipment, cooldown and regiment membership. For PlayerControlled, the host chooses either core velocity commands or external Pawn pose samples for each tick; mixing them for one soldier/tick is a Conflict. Formation does not move PlayerControlled. After release, Returning begins at the actual last pose. A visual physics impact can request an attack, but cannot directly change core health. Detailed physical melee requires a later authority design.

## Verified boundary and next engine checks

`battle.integration` tests owned frames, stable IDs, LOD-style despawn/reappearance, pool generation changes, repeated events and old frames, while the core hash/life remain unchanged. It also measures Capture -> frame -> mock-consumer for 10k units; results are generated at `out/artifacts/presentation/exchange.txt`. The mock performs no rendering, skeletal animation, networking, UE allocation or thread synchronization. Initial pool population and steady updates differ; the reported average includes initial population.

Actual Task 12 must test FMass fragments/processors, deferred creation/destruction, UBT/UHT, package build, cm conversion, 100 units=100 entities, Pawn movement ownership, LOD and event deduplication in the selected engine version. Only then profile 1k/5k/10k with overhead and possessed cameras. Moving pure core batch functions into Mass processors is a later measured migration; there must remain one source of gameplay state.

## Official references

Accessed 2026-11-02. The online documentation version is not a commitment to the future installed host version.
- [MassEntity overview: fragments, processors, deferred command buffers](https://dev.epicgames.com/documentation/unreal-engine/overview-of-mass-entity-in-unreal-engine?lang=en-US)
- [MassEntity API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/MassEntity)
- [UE coordinate system](https://dev.epicgames.com/documentation/unreal-engine/coordinate-system-and-spaces-in-unreal-engine)
- [UE units of measurement](https://dev.epicgames.com/documentation/unreal-engine/units-of-measurement-in-unreal-engine?lang=en-US)
## Measured exchange on this machine

Release, Ryzen 7 3700X, 10k units, 30 repetitions: owned frame 400,056 bytes (40 bytes/unit); mean Capture 0.698 ms, BuildPresentation 0.131 ms, mock consumer 0.831 ms including initial pool population. This is a diagnostic mock measurement, not UE cost. The initial scenario/seed replay bundle (no commands) was 6,705,136 bytes and took 93.5 ms to serialize/write once; no durability flush or compression was measured. The integration test regenerates the measurements.
