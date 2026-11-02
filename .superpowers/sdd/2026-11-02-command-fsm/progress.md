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
Task 5: complete (commit 6de314d; tests: Debug18/18).

Task6 RED: communication and translation tests failed against NotImplemented; runtime tests showed no command state after CreateWorld and IssueCommandTask was rejected. GREEN validated setup initialization, one-tick Signal per hierarchy hop, direct plus dated report contacts, order translation including Reform columns/spacing, successor choices excluding occupied officers, route-time/death checks for Courier, task/authority/side validation, external-order precedence and order deduplication. Integration reaches a regiment Attack, visible movement and Hit; NoPath raises NeedSupport. Demo now runs this battle. Full Debug19/19 and Release19/19.
Ruling: suppress repeated regiment orders while the goal shifts <=1m, then refresh on a larger move; prevents order churn without freezing target pursuit.
Task 6: complete (commit 9ede397; tests: Debug19/19, Release19/19).

Task7 RED: the first command persistence test failed as expected: Capture still wrote schema2, StateHash ignored command state, and snapshots lost CommandState. CLI 31-tick run exposed a CommanderLost report without its observation; added a focused failing regression before fixing the producer.
GREEN: schema3 snapshot/replay JSON now round-trips hierarchy, each FSM memory/schedule, tasks, reports, message variant/payload and counters. Added full command-state validation/hash, strict enum and integer checks, bounded command-free v2 migration, command JSON input, fixture and demo assertions. CommanderLost now carries a valid current observation.
Verification: Debug CTest 19/19; Release CTest 19/19. CLI continuous 31 ticks, resume after tick1, and replay after tick1 produce identical final hashes. Corrupt timer, task counter, message ID, immutable topology, missing nested field, enum and signed-unsigned cases reject; plain v2 snapshot/replay migrate, commandful v2 reject.
Scale sample (Release CLI, 25 ticks, 3 runs, wall time includes process/JSON/file I/O): 4 nodes/2 regiments/6 units median13.68ms; synthetic126 nodes/120 regiments/246 units median326.57ms. This is an execution smoke sample, not an FPS or thousands-of-units claim.
Final review: independently reviewed the runtime/persistence integration (9ede397..b511eb8). It found two actionable consistency issues: persisted message envelopes could disagree with their nested task/report identities, and initial missions could be future-dated even though restore rejects future-created tasks. Added RED regressions and rejected both cases. A further audit bounded persisted brain intervals to 1,000,000 ticks and included parent task IDs in counter validation. Focused regressions pass; Debug and Release CTest each pass19/19 after these fixes. `git diff --check` passes for the four changed BattleCore source/test files; repository-wide check reports unrelated pre-existing Unity whitespace changes, left untouched.
Task 7: implementation, verification and review fix pass complete; local commit pending.
