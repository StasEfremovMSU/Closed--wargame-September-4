#include "doctest.h"
#include "battle/Officers.h"
#include "TestSupport.h"
using namespace battle;
TEST_CASE("officers.no_enemy_and_role_selection"){
 auto s=test::Duel(3);s.regiments.resize(1);s.units.resize(3);s.officers={{{1},{1},1},{{2},{2},2},{{3},{3},4}};auto w=CreateWorld(s,42).value;
 CHECK(Commander(w,{1}).value==2);SpatialIndex index;auto p=Observe(w,{2},index);CHECK(Evaluate(p,{},nullptr).action==OrderType::Hold);
 FindUnit(w,{2})->alive=false;CHECK(Commander(w,{1}).value==1);CHECK(GetStats(w).dead_units==1);
}
TEST_CASE("officers.delivery_priority_and_no_commander"){
 auto w=CreateWorld(test::Duel(),42).value;FlatNavigationService nav;
 Order a;a.id={1};a.recipient={1};a.type=OrderType::MoveTo;a.point={-5,0,0};a.delivery_tick=5;a.priority=10;
 CHECK(SubmitOrder(w,a)==Status::Ok);for(int i=0;i<5;++i){REQUIRE(Step(w,{}, {},nav));}CHECK(w.regiments[0].pose.position.x==-10);
 REQUIRE(Step(w,{}, {},nav));CHECK(w.regiments[0].pose.position.x>-10);
 Order b=a;b.id={2};b.type=OrderType::Hold;b.delivery_tick=5;b.priority=0;CHECK(SubmitOrder(w,b)==Status::Ok);REQUIRE(Step(w,{}, {},nav));REQUIRE(w.regiments[0].order);CHECK(w.regiments[0].order->id.value==1);
 CHECK_FALSE(Commander(w,{1}));CHECK(w.regiments[1].route.empty());
}
TEST_CASE("officers.dead_target_and_no_path"){
 auto w=CreateWorld(test::Duel(),42).value;FindUnit(w,{2})->alive=false;
 Order a;a.recipient={1};a.type=OrderType::Attack;a.target_unit={2};CHECK(SubmitOrder(w,a)==Status::NotFound);REQUIRE(w.regiments[0].order);CHECK(w.regiments[0].order->status==OrderStatus::Failed);
 a={};a.recipient={1};a.type=OrderType::MoveTo;a.point={200,0,0};SubmitOrder(w,a);GridNavigationService nav({},1,30,30,{-15,-15,0});REQUIRE(Step(w,{}, {},nav));CHECK(w.regiments[0].order->status==OrderStatus::Failed);
}
TEST_CASE("officers.reevaluation_and_single_mover"){
 auto s=test::Duel(3);s.officers={{{1},{1},1},{{2},{2},3},{{3},{3},4}};s.officers[0].tactic_mind=1;auto w=CreateWorld(s,42).value;FlatNavigationService nav;
 REQUIRE(Step(w,{}, {},nav));CHECK(w.regiments[0].decision_tick==10);CHECK(w.regiments[0].decision_until==20);
 auto before=w.regiments[0].pose.position;CHECK(before.x>-10);REQUIRE(Step(w,{}, {},nav));CHECK(Distance(before,w.regiments[0].pose.position)<=.1001f);CHECK(w.regiments[0].decision_tick==10);
 for(auto&u:w.units)if(u.regiment==RegimentId{2})u.alive=false;w.regiments[1].units.clear();REQUIRE(Step(w,{}, {},nav));CHECK(w.regiments[0].ai_action==OrderType::Hold);
}
