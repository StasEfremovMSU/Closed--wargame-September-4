#include "doctest.h"
#include "battle/Spatial.h"
#include "battle/Formation.h"
#include "TestSupport.h"
#include "JsonBindings.h"
using namespace battle;
TEST_CASE("navigation.spatial_updates_and_edges"){
 SpatialIndex index;std::vector<UnitPosition> p{{{9},{10,10,0}},{{2},{13,14,0}},{{7},{-100,-100,0}}};index.Rebuild(p);
 auto ids=index.QueryRadius({10,10,0},5.01f);REQUIRE(ids.size()==2);CHECK(ids[0].value==2);CHECK(ids[1].value==9);CHECK(Distance(p[0].position,p[1].position)==5);
 CHECK(index.QueryRadius({-100,-100,0},1).size()==1);p[0].position={50,50,0};index.Rebuild(p);CHECK(index.QueryRadius({10,10,0},1).empty());
}
TEST_CASE("navigation.footprint_corridor"){
 std::vector<Obstacle> wall{{-1,-100,1,-1},{-1,1,1,100}};
 GridNavigationService nav(wall,1,40,40,{-20,-20,0});
 auto column=nav.FindPath({{{-8,0,0},0},{8,0,0},{.6f,6}});CHECK(column.status==Status::Ok);CHECK(!column.waypoints.empty());
 auto line=nav.FindPath({{{-8,0,0},0},{8,0,0},{4,1}});CHECK(line.status==Status::NoPath);
 CHECK(nav.FindPath({{{-100,0,0},0},{8,0,0},{.6f,1}}).status==Status::NoPath);
 CHECK(nav.FindPath({{{-8,0,0},0},{0,3,0},{.6f,1}}).status==Status::NoPath);
}
TEST_CASE("navigation.motion_does_not_cross_wall"){
 auto s=test::Duel();s.regiments.resize(1);s.units.resize(1);s.regiments[0].pose.position.x=-3;s.regiments[0].speed=100;s.units[0].pose=s.regiments[0].pose;s.units[0].speed=100;s.obstacles={{-.1f,-2,.1f,2}};
 auto w=CreateWorld(s,42).value;w.regiments[0].route={{{3,0,0},0}};AdvanceFormations(w,.1f);CHECK(w.regiments[0].pose.position.x<=-.399f);
}
TEST_CASE("navigation.rotation_sweep"){
 GridNavigationService blocked({{1,2,2,3}},1,30,30,{-15,-15,0});
 CHECK_FALSE(blocked.CanTraverse({{},0},{{},Pi/2},{.6f,10}));
 GridNavigationService open({},1,30,30,{-15,-15,0});CHECK(open.CanTraverse({{},0},{{},Pi/2},{.6f,10}));
}
TEST_CASE("navigation.bottleneck_complete_motion"){
 Scenario s;RegimentSpec r;r.id={1};r.pose.position={-10,0,0};r.formation={1,1,.3f};s.regiments.push_back(r);s.obstacles={{-1,-100,1,-1},{-1,1,1,100}};
 for(unsigned i=0;i<10;++i){UnitSpec u;u.id={i+1};u.regiment={1};u.pose=r.pose;s.units.push_back(u);}
 auto w=CreateWorld(s,42).value;GridNavigationService nav(s.obstacles,1,50,50,{-25,-25,0});auto path=nav.FindPath({r.pose,{10,0,0},Footprint(w.regiments[0])});REQUIRE(path.status==Status::Ok);w.regiments[0].route=path.waypoints;
 const auto dir=std::filesystem::path(BATTLE_SOURCE_DIR)/"out/artifacts/navigation";std::filesystem::create_directories(dir);
 for(unsigned tick=0;tick<600;++tick){if(tick%200==0){std::ofstream f(dir/(std::to_string(tick)+".json"));f<<nlohmann::json(Capture(w)).dump(2);}AdvanceFormations(w,.05f);++w.tick;}
 CHECK(w.regiments[0].pose.position.x==doctest::Approx(10));
}
