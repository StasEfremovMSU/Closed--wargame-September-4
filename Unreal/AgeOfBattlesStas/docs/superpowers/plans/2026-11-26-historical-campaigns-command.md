# Historical campaigns and command

> The controller orchestrates bounded implementation and review. Root owns artillery, redoubts, shared builds and integration verification.

- [x] Campaigns. Add three historically sourced routes and a separate campaign entry in the existing atlas. Each route has a fixed playable side and chronological stages keyed by mission ID. Record only victory from a running campaign in persistent local progression, unlock its next stage, and retain replay access. Tests cover defeat, victory, saving, and independence across campaigns and free play.
- [x] Officer control. Offer living own commanders at regiment, group and army levels in a paused battlefield view with role/name bubbles. Cancel should restore the prior pause and input state. An active group/army commander directs subordinate formations using its actual command scope, with visible targets and outcomes.
- [x] Commander profiles. Use a deterministic profile per officer with personal effects, command effects and rank effects, including advantages and disadvantages. Give the named historical commanders fixed identities, game traits and styled portraits. Give other officers names from their era/faction and show their default rank portraits/icons. Test stable seeds, bounds, faction coverage and actual effects.
- [x] Artillery. Keep gun crews on one frontage, widening spacing as needed and preserving crew assignments. Enforce the invariant in world creation and during formation changes.
- [x] Redoubts. Use the same fortification data to render the object and simulate interior defence, deliberate occupation, and slowing a frontal approach across its ditch and ramp. Keep a practical rear entrance and a walkable interior. Add earthworks to Borodino, Poltava and Narva.
- [x] Ship locally. Rebuild BattleCore against its tests, then Unreal Editor and Windows Shipping. Check the new paths through the actual game and create a distinct installer with runtime. The existing online build remains outside this task.

All 14 campaign stages target 5,000 figures. Chaeronea and Marengo were launched at that scale through the campaign atlas. The live check covered commander possession, rank-scoped orders and the pause state. The Windows build launched Borodino with the defending battery inside the central earthworks.

Verification passed 320 native test cases, 25 CTest entries and 81 Unreal automation tests. Windows Shipping includes all 44 historical preset files. The Windows x64 installer is in `Distribution/AgeOfBattles-Windows-x64-Setup-HistoricalCampaigns-2026-11-26.exe`. Its version is 2026.11.26.4 and the SHA-256 matches the companion checksum.
