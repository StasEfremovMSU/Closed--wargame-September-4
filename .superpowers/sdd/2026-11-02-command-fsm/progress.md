# SDD ledger — plan: docs/superpowers/plans/2026-11-02-command-fsm.md

Spec: docs/superpowers/specs/2026-11-02-command-fsm-design.md
Base:6ad0eaf; plan commit810b80f. Builder check: nothing important left. Baseline Debug14/14.
Ruling: Continue native in the selected D copy/current branch and preserve dirty Unity files — prior preference — checkout is not clean.
Ruling: Strict explicit orders unless authorized initiative, with mandatory panic retreat — refusal policy unanswered — autonomous sacrifice avoidance is a later balance choice.
Ruling: Opt-in hierarchy and authoritative state in World — preserves old fixtures — select a hierarchy scenario to use it.
Ruling: Courier transport is routed/timed/death-sensitive but abstract — headless scope — its Unit is not detached from formation.
Pre-flight:1->2 supplies validated situations;2->3->4->5 share pure task/memory helpers;6 owns all their values;7 persists6. Scenario setup only depends on Messages/Hierarchy; World runtime may include Brains after Orders. No unresolved interface conflict.
Task1: tests first. Retire prior assertions that services must remain inactive; replace with behaviour contracts.

Task1 RED: three service tests failed against NotImplemented; GREEN all14 Debug CTest. Existing demo first failed because it expected inactive services, then adapted to assert Ready for implemented services. No algorithm failure concealed.
Task 1: complete (commits 810b80f..9f3d2cf, tests: 'S:/Virtual Life/VirtualLife/out/tools/cmake-3.31.6-windows-x86_64/bin/ctest.exe' --test-dir BattleCore/out/build/debug --output-on-failure → Total Test time (real) =   1.70 sec)

Task2 RED: missing FSM memory/current_formation API (task-2-red.log); GREEN5 regiment cases90 assertions, full Debug15/15. Added received_tick to tasks for exact Delivered duration origin; pure brains never mutate World.
Task 2: complete (commits 9f3d2cf..2794fee, tests: 'S:/Virtual Life/VirtualLife/out/tools/cmake-3.31.6-windows-x86_64/bin/ctest.exe' --test-dir BattleCore/out/build/debug --output-on-failure → Total Test time (real) =   1.71 sec)

Task3 RED: three group cases failed against stub. GREEN coordinated deployment/attack/support/reserve/withdraw; reversed children stable. Two author-found bugs reproduced RED: commitment applied to memory but not child tasks; any child panic infected whole army. Fixed child plan from committed state and weighted strict majority panic. Full Debug16/16.
Ruling: Aggregate panic requires more than half living force, individual child panic instead triggers support/reserve — avoids a single panicked regiment routing an army — tactical threshold is an initial balance choice.
Task 3: complete (commits 2794fee..61f5f76, tests: 'S:/Virtual Life/VirtualLife/out/tools/cmake-3.31.6-windows-x86_64/bin/ctest.exe' --test-dir BattleCore/out/build/debug --output-on-failure → Total Test time (real) =   1.80 sec)

Task4 RED: Part tests first failed against the existing stub. GREEN stable frontage by child width, threat-driven reserve release and NeedSupport report, expiry cancellation, withdrawal fan-out, empty/dead subtree behavior. Full Debug17/17; focused Group/Part2/2. The Group frontage test was extended to catch loss of the superior's assigned sector and loss of Flank intent during deployment.
Task 4: complete (commit 66bf3a1; tests: 'S:/Virtual Life/VirtualLife/out/tools/cmake-3.31.6-windows-x86_64/bin/ctest.exe' --test-dir BattleCore/out/build/debug --output-on-failure → 17/17).

Task5 RED: army mission/balance/lost-target and reserve tests first failed against the Army stub. GREEN explicit Hold obedience, deterministic rule-based autonomous attack only with a visible worthwhile target, safe target loss, 25% loss/panic reserve release, reserve hold and empty-force handling. Full Debug18/18.
Task 5: complete (pending local commit; tests: 'S:/Virtual Life/VirtualLife/out/tools/cmake-3.31.6-windows-x86_64/bin/ctest.exe' --test-dir BattleCore/out/build/debug --output-on-failure → 18/18).
