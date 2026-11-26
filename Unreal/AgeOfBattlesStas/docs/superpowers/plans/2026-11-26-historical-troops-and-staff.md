# Historical troops and army staff

Use selectable regimental profiles saved with mission presets and represented in the same runtime as existing troops. Preserve legacy presets and the instance budgets. Period equipment and staff are stylized for the game.

- [x] Add historical profiles and activate examples in relevant scenarios. The editor selects a faction-compatible profile. Existing copies load without a profile and retain their previous composition. The profile identity reaches rendering by regiment.
- [x] Add recognizable ancient and Napoleonic figures in the normal and simplified render paths. Cover Roman infantry and cavalry, Numidian horsemen, Macedonian infantry and cavalry, Greek hoplites, Persian infantry and archers, and the named French, British and Russian infantry profiles.
- [x] Add Roman pila and standalone loose-order bow, sling and javelin regiments. Reuse existing BattleCore combat and ammunition, distinguish loose troops from formation detachments, and retreat from approaching formed melee combatants.
- [x] Add a small guard and adjutant staff to each army commander. Keep bodyguards attached and make them provide limited protection with visible casualties. Route suitable orders from army to group through an actual available adjutant and record delivery in the existing command runtime.
- [x] Run native and Unreal regressions, check scenario and legacy-preset compatibility, inspect real in-game silhouettes and deliver a Windows build.

Verification passed 339 BattleCore cases and 89 Unreal automation tests. The staged Shipping run of Cannae completed 400 ticks with 5,000 figures and registered its first melee hit on tick 60. The Windows installer is version `2026.11.26.6`.
