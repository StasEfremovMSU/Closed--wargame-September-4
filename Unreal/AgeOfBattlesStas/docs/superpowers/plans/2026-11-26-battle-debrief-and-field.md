# Battle debrief, officer view and larger fields

- [x] Add a battle debrief with result, starting and remaining strength, confirmed losses and eliminations by regiment. Keep a local overview of completed battles and make it available from the main menu.
- [x] Translate the approved `02-in-formation-officer.jpg` prototype into a distinct first-person officer mode with usable commands, while retaining the third-person option.
- [x] Give Sengoku troops readable silhouettes at both render distances. Distinguish samurai armor, ashigaru and ranged troop equipment. Reuse instancing and the current rig.
- [x] Increase field area by roughly twofold with authored deployment still concentrated near the historical action. Make the extra space available for flanking and improve ground and landmark readability.
- [x] Support roughly twice as many individually commanded battalions in early-modern and Napoleonic battles at comparable figure counts. Preserve older saved rosters and stable scenario behavior.
- [x] Verify native and Unreal tests, then launch an updated Windows build for inspection.

The 25 BattleCore and 93 Unreal tests passed. The Windows build displayed the battle debrief, accumulated results across two launches in a separate test profile, and supported Tab orders and both camera views in a live Sengoku battle.
