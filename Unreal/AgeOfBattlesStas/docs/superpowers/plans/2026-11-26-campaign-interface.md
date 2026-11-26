# Campaign catalogue, battle HUD, and surroundings

> Implementation follows the three reviewed HTML mockups in Prototypes/2026-11-26 and the approved combination of the campaign atlas, formation officer HUD, and field table.

**Goal** Put the reviewed UI in the live Windows game and expand its historical battle catalogue and visual surroundings.

**Architecture** Keep the same native Slate menus, setup flow, runtime commands, BattleCore terrain queries, and external preset schema. Place visual terrain outside the playable bounds and define scenario additions as packaged preset data.

- [x] Expand the built-in catalogue by two historical missions for each of the seven active eras. Give each preset authentic context, a playable objective and deployment, a source, and 5,000 figures at standard scale. Check all new IDs and deployments through the existing scenario pipeline.
- [x] Add an atlas to the Scenario setup step. Show active eras on the left, searchable historical and saved missions in the centre, and live scenario briefing, figures, field dimensions, goal, and sources on the right. Preserve existing draft, copy, custom mission, and launch behaviour. Cover search and era switching regressions in the setup flow.
- [x] Use a compact right-hand officer command sheet that remains closed until Tab or its existing input, with unobstructed reticle and readable health and parry availability. Reuse real order bindings and HUD values. Put live regiment strength and selection in the spectator overview.
- [x] Extend the view with bounded, instanced countryside beyond the playable field and restrained lighting and haze. Keep geometry visual-only and preserve terrain inside the map.
- [x] Compile the editor and run catalogue, scenario and battle automation. Run the game in a visible window and check catalogue search/selection and HUD placement. Stage a fresh local Windows build after verification if the installed toolchain and storage permit.

**Regression checks** Changing eras must not leak search state or show missions from a different era. Searching must preserve the draft and its saved copies. Valid selection must work from keyboard focus. The smaller officer sheet must preserve all existing orders and input bindings. Environmental props must not alter pathing, collision, or simulation.

**Delivery** Local project and test build. The approved HTML remains available for visual comparison.

## Result

- The built-in catalogue has 35 historical episodes across seven active eras. The scenarios document reuse of existing terrain templates and troop visuals.
- The officer HUD exposes health and parry availability. The simulation currently tracks fatigue for the regiment, so the personal stamina from the prototype was not added as a gameplay rule.
- The editor build passed and all 50 Unreal tests passed, including PlayableHistory, ScenarioFlow, and BattlefieldSurroundings.
- The Windows stage launched via its bootstrap executable. Waterloo displayed both army counts, and the officer sheet opened, accepted Follow, and closed via Tab. Screenshots are in Distribution/CampaignAtlas-Screenshots.
- Distribution contains the 2026.11.26.3 installer and its verified SHA-256. The installer includes the Microsoft-signed Visual C++ x64 runtime.
- One paired Waterloo benchmark at 5,000 figures and 180 steps preserved the simulation hash in both views. Frame-time p95 was 19.599 to 17.634 ms in overview and 23.706 to 21.112 ms as an officer. Reports are in Saved/Reports/CampaignInterface-Waterloo-Baseline and CampaignInterface-Waterloo-Shipping.
