#include "LegacyImport.h"
#include "ScenarioJson.h"
#include "TestSupport.h"
#include "doctest.h"
using namespace battle;
namespace fs = std::filesystem;
TEST_CASE("import.legacy_backup") {
    auto r =
        io::ImportLegacy(fs::path(BATTLE_SOURCE_DIR) / "fixtures/legacy", {"map-backup.txt", 1});
    REQUIRE(r);
    auto w = CreateWorld(r.value, 42);
    REQUIRE(w);
    auto stats = GetStats(w.value);
    CHECK(stats.regiment_count == 7);
    CHECK(stats.rank_and_file_count == 200);
    CHECK(stats.officer_role_count == 3);
    CHECK(stats.living_units == 203);
    REQUIRE(r.value.officers.size() == 3);
    CHECK(r.value.officers[0].role == 2);
    CHECK(r.value.officers[1].role == 1);
    CHECK(r.value.officers[2].role == 4);
    CHECK(r.value.officers[0].id != r.value.officers[1].id);
    CHECK(r.diagnostics.size() >= 1);
    CHECK_FALSE(io::ImportLegacy(fs::path(BATTLE_SOURCE_DIR) / "fixtures/legacy", {"map1.txt", 1}));
    CHECK_FALSE(
        io::ImportLegacy(fs::path(BATTLE_SOURCE_DIR) / "fixtures/legacy", {"../map1.txt", 1}));
}
TEST_CASE("import.geometry_and_lines") {
    test::TempDir temp;
    fs::copy(fs::path(BATTLE_SOURCE_DIR) / "fixtures/legacy", temp.path,
             fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    test::Write(temp.path / "officer.txt", "");
    test::Write(temp.path / "map.txt", "\n! comment\n1,0,10,20,30,1.57079632679,1,1,0,1\n\n");
    auto r = io::ImportLegacy(temp.path, {"map.txt", 1});
    REQUIRE(r);
    REQUIRE(r.value.regiments.size() == 1);
    auto p = r.value.regiments[0].pose;
    CHECK(p.position.x == 30);
    CHECK(p.position.y == 10);
    CHECK(p.position.z == 20);
    CHECK(p.yaw == doctest::Approx(Pi / 2));
    CHECK(Forward(p.yaw).y == doctest::Approx(1));
    CHECK(r.value.units[0].id.value != 0);
    test::Write(temp.path / "map.txt", "1,0,0,0,0,0,1,1,0\n");
    r = io::ImportLegacy(temp.path, {"map.txt", 1});
    CHECK_FALSE(r);
    REQUIRE(!r.diagnostics.empty());
    CHECK(r.diagnostics[0].line == 1);
    test::Write(temp.path / "map.txt", "1,0,0,0,0,0,1,1,999,1\n");
    CHECK_FALSE(io::ImportLegacy(temp.path, {"map.txt", 1}));
}
TEST_CASE("import.json_validation") {
    test::TempDir t;
    auto s = test::Duel();
    REQUIRE(io::SaveScenario(t.path / "s.json", s));
    auto r = io::LoadScenario(t.path / "s.json");
    REQUIRE(r);
    CHECK(r.value.units.size() == 2);
    s.units.push_back(s.units[0]);
    REQUIRE(io::SaveScenario(t.path / "duplicate.json", s));
    CHECK_FALSE(io::LoadScenario(t.path / "duplicate.json"));
    test::Write(t.path / "bad.json", "{bad");
    CHECK_FALSE(io::LoadScenario(t.path / "bad.json"));
}
TEST_CASE("import.ranged_rule_json_requires_integer") {
    test::TempDir dir;
    auto file = dir.path / "ranged.json";
    for (const std::string value :
         {"4.5", "-4294967292", "4294967300", "\"4\"", "null", "0", "65"}) {
        CAPTURE(value);
        test::Write(
            file,
            "{\"schema_version\":1,\"units\":[],\"regiments\":[],\"rules\":{\"ranged_cohorts\":" +
                value + "}}");
        CHECK_FALSE(io::LoadScenario(file));
    }
    for (const std::string value : {"1", "4", "64"}) {
        test::Write(
            file,
            "{\"schema_version\":1,\"units\":[],\"regiments\":[],\"rules\":{\"ranged_cohorts\":" +
                value + "}}");
        auto loaded = io::LoadScenario(file);
        REQUIRE(loaded);
        CHECK(loaded.value.rules.ranged_cohorts == std::stoul(value));
    }
}
