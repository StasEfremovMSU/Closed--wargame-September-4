#include "JsonBindings.h"
#include "ReplayIo.h"
#include "TestSupport.h"
#include "doctest.h"

TEST_CASE("world.regiment_tactics_rules_round_trip") {
    const auto legacy = nlohmann::json::object().get<battle::Rules>();
    CHECK_FALSE(legacy.regiment_tactics);
    CHECK_FALSE(legacy.risky_volley);
    auto configured = legacy;
    configured.regiment_tactics = true;
    configured.risky_volley = true;
    const auto restored = nlohmann::json(configured).get<battle::Rules>();
    CHECK(restored.regiment_tactics);
    CHECK(restored.risky_volley);
}

TEST_CASE("world.regiment_tactics_state_round_trip") {
    battle::RegimentState regiment;
    regiment.morale = .52f;
    regiment.cohesion = .37f;
    regiment.fatigue = .66f;
    regiment.disordered = true;
    regiment.deployment_ticks = 21;
    regiment.initial_fatigue = .24f;

    const auto restored = nlohmann::json(regiment).get<battle::RegimentState>();
    CHECK(restored.morale == regiment.morale);
    CHECK(restored.cohesion == regiment.cohesion);
    CHECK(restored.fatigue == regiment.fatigue);
    CHECK(restored.disordered);
    CHECK(restored.deployment_ticks == regiment.deployment_ticks);
    CHECK(restored.initial_fatigue == regiment.initial_fatigue);

    battle::BattleEvent strike;
    strike.aspect = battle::AttackAspect::Rear;
    CHECK(nlohmann::json(strike).get<battle::BattleEvent>().aspect == strike.aspect);
    battle::BattleEvent volley;
    volley.high_arc = true;
    CHECK(nlohmann::json(volley).get<battle::BattleEvent>().high_arc);
}

TEST_CASE("replay.pre_tactics_files_keep_strict_validation_with_old_fields") {
    using nlohmann::json;
    const auto scenario = test::Duel();
    const auto created = battle::CreateWorld(scenario, 42);
    REQUIRE(created);
    const test::TempDir temp;
    const auto snapshot_path = temp.path / "snapshot.json";
    const auto replay_path = temp.path / "replay.json";
    const auto strip_deployment = [](json &regiment) {
        regiment.erase("deployment_ticks");
        regiment.erase("initial_fatigue");
    };

    json snapshot = battle::Capture(created.value);
    snapshot["state"]["events"].push_back(json(battle::BattleEvent{}));
    snapshot["state"]["events"][0].erase("aspect");
    snapshot["state"]["events"][0].erase("high_arc");
    for (auto &regiment : snapshot["state"]["scenario"]["regiments"])
        strip_deployment(regiment);
    for (auto &regiment : snapshot["state"]["regiments"]) {
        strip_deployment(regiment);
        for (const auto *key : {"morale", "cohesion", "fatigue", "disordered"})
            regiment.erase(key);
    }
    test::Write(snapshot_path, snapshot.dump());
    const auto loaded = battle::io::LoadSnapshot(snapshot_path);
    REQUIRE(loaded);
    CHECK(battle::StateHash(loaded.value.state) == battle::StateHash(created.value));
    CHECK_FALSE(loaded.value.state.scenario.rules.regiment_tactics);

    auto corrupt = snapshot;
    corrupt["state"]["regiments"][0].erase("speed");
    test::Write(snapshot_path, corrupt.dump());
    CHECK_FALSE(battle::io::LoadSnapshot(snapshot_path));
    corrupt = snapshot;
    corrupt["state"]["regiments"][0]["unknown_field"] = true;
    test::Write(snapshot_path, corrupt.dump());
    CHECK_FALSE(battle::io::LoadSnapshot(snapshot_path));
    snapshot["state"]["scenario"]["rules"]["regiment_tactics"] = true;
    test::Write(snapshot_path, snapshot.dump());
    CHECK_FALSE(battle::io::LoadSnapshot(snapshot_path));

    json replay{{"schema_version", 3}, {"scenario", scenario}, {"seed", 42u},
                {"commands", json::array()}};
    for (auto &regiment : replay["scenario"]["regiments"])
        strip_deployment(regiment);
    test::Write(replay_path, replay.dump());
    CHECK(battle::io::LoadReplay(replay_path));
}
