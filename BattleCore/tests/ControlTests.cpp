#include "doctest.h"
#include "battle/Control.h"
#include "battle/Combat.h"
#include "battle/Snapshot.h"
#include "TestSupport.h"
using namespace battle;
TEST_CASE("control.acquire_switch_preserves_state"){
 auto w=CreateWorld(test::Duel(2),42).value;auto&u=w.units[0];u.health=.75f;u.ready_tick=99;u.equipment={1,2};
 CHECK(AcquireControl(w,{1})==Status::Ok);auto next=w.next_event_id;CHECK(AcquireControl(w,{1})==Status::Ok);CHECK(w.next_event_id==next);
 CHECK(AcquireControl(w,{2})==Status::Ok);CHECK(u.control==ControlMode::Returning);CHECK(u.health==.75f);CHECK(u.ready_tick==99);CHECK(u.equipment.size()==2);CHECK(GetStats(w).living_units==4);
 CHECK(AcquireControl(w,{999})==Status::NotFound);CHECK(w.controlled.value==2);
}
TEST_CASE("control.single_movement_authority"){
 auto w=CreateWorld(test::Duel(),42).value;FlatNavigationService nav;AcquireControl(w,{1});auto start=w.units[0].pose.position;
 w.regiments[0].route={{{0,0,0},0}};Command c;c.kind=CommandKind::MoveControlledUnit;c.unit={1};c.velocity={0,200,0};CommandEnvelope e{0,1,c};REQUIRE(Step(w,std::span{&e,1},{},nav));
 CHECK(w.units[0].pose.position.x==start.x);CHECK(w.units[0].pose.position.y==doctest::Approx(.1f));
 REQUIRE(Step(w,{}, {},nav));CHECK(w.units[0].pose.position.y==doctest::Approx(.2f));
 auto actual=w.units[0].pose.position;CHECK(ReleaseControl(w,{1})==Status::Ok);CHECK(w.units[0].pose.position==actual);CHECK(w.units[0].control==ControlMode::Returning);
}
TEST_CASE("control.external_input_atomic_conflict_and_latest_sample"){
 auto w=CreateWorld(test::Duel(),42).value;AcquireControl(w,{1});FlatNavigationService nav;
 Command pose;pose.kind=CommandKind::ExternalPoseSample;pose.unit={1};pose.pose.position={10,20,0};pose.sample_tick=0;
 Command move;move.kind=CommandKind::MoveControlledUnit;move.unit={1};move.velocity={1,0,0};std::vector<CommandEnvelope> bad{{0,1,pose},{0,2,move}};auto hash=StateHash(w);
 CHECK(Step(w,bad,{},nav).status==Status::Conflict);CHECK(StateHash(w)==hash);
 auto second=pose;second.pose.position={30,40,0};std::vector<CommandEnvelope> samples{{0,1,pose},{0,2,second}};REQUIRE(Step(w,samples,{},nav));CHECK(w.units[0].pose.position.x==30);CHECK(w.units[0].pose.position.y==40);
 CHECK(SetExternalPose(w,{1},{},0)==Status::InvalidInput);
}
TEST_CASE("control.death_releases_once"){
 auto w=CreateWorld(test::Duel(),42).value;AcquireControl(w,{1});w.events.clear();BattleEvent e;e.id={100};e.kind=EventKind::AttackLaunched;e.source={2};e.target={1};e.amount=100;e.hit=true;e.projectile=true;e.due_tick=1;
 CommitCombatEvents(w,std::span{&e,1});CHECK_FALSE(w.controlled);CHECK(w.units[0].control==ControlMode::Dead);CHECK(AcquireControl(w,{1})==Status::NotFound);
 CHECK(std::count_if(w.events.begin(),w.events.end(),[](auto&e){return e.kind==EventKind::ControlChanged;})==1);
}
TEST_CASE("control.hundred_switches"){
 auto w=CreateWorld(test::Duel(2),42).value;FlatNavigationService nav;
 for(uint64_t i=0;i<100;++i){Command c;c.kind=CommandKind::AcquireControl;c.unit={i%2+1};CommandEnvelope e{w.tick,i+1,c};REQUIRE(Step(w,std::span{&e,1},{},nav));}
 CHECK(GetStats(w).living_units==4);CHECK(w.controlled.value==2);
}
