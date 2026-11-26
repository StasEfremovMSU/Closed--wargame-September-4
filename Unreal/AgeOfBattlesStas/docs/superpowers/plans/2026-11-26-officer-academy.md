# Officer academy implementation plan

The four lessons use the battle simulation and the same controls as historical scenarios. Advance lessons from observed player actions and events. The Windows release follows verification of the staged build.

- [x] Add an Academy entry and four runnable presets with separate lesson progress, objective feedback and appropriate troop sizes.
- [x] Verify infantry movement, following, crossing in column, firing in line and orders against the second wave.
- [x] Verify map possession, the flank attack order and run-up, rear attack, withdrawal, loss of the cavalry, and reserve infantry formation and fire.
- [x] Reuse BattleCore line-of-fire, ammunition and impact for a manual field-gun shot ordered by a possessed officer.
- [x] Verify transitions between army and group command and their actual profile and order panels.
- [x] Let friendly soldiers give a moving officer room and recover their line once the officer passes.
- [x] Compare the same Windows executable with grass enabled and disabled in a scripted officer view.
- [ ] Verify BattleCore, Unreal automation and the staged Windows installer. Publish only after these checks and record the source commit, release hash and public download verification.

Grass uses a 40 m view distance. In four 320-tick trials at 1152 × 740 on the RTX 3070, using the 10,000-figure Cannae workload and alternating the grass toggle, mean of the run medians was 7.014 ms without grass and 7.059 ms with it. Mean p95 was 17.815 ms without grass and 17.709 ms with it. All four runs produced state hash `cff6bf8c9f549cb3`.

The final Editor build finished all 99 Unreal tests and BattleCore finished all 25 CTest groups. The Windows Shipping build passed. The artillery lesson was also played through the Windows UI, where a manual shot hit the target formation and opened the report.
