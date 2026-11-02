#include "battle/World.h"
#include "battle/Formation.h"
#include <set>
namespace battle {
Result<Scenario> ValidateScenario(Scenario s){
    auto bad=[&](std::string message){return Result<Scenario>{Status::InvalidInput,{},{{"scenario",0,std::move(message)}}};};
    if(s.schema_version!=1)return bad("unsupported scenario version");
    std::set<uint64_t> regs,units,offs,off_units;
    for(const auto&r:s.regiments){if(!r.id||!regs.insert(r.id.value).second||!Finite(r.pose)||!Finite(r.rally_point)||r.formation.columns==0||!std::isfinite(r.formation.spacing_m)||r.formation.spacing_m<=0||!std::isfinite(r.formation.body_radius_m)||r.formation.body_radius_m<=0||!std::isfinite(r.speed)||r.speed<0||!std::isfinite(r.turn_rate)||r.turn_rate<=0)return bad("invalid or duplicate regiment");}
    for(const auto&u:s.units){if(!u.id||!units.insert(u.id.value).second||!regs.contains(u.regiment.value)||!Finite(u.pose)||!std::isfinite(u.health)||u.health<=0||!std::isfinite(u.speed)||u.speed<0||!std::isfinite(u.armour)||u.armour<0||!std::isfinite(u.archer_skill)||u.archer_skill<0||u.archer_skill>1||!std::isfinite(u.weapon.range)||u.weapon.range<=0||!std::isfinite(u.weapon.damage)||u.weapon.damage<0||!std::isfinite(u.weapon.base_accuracy)||u.weapon.base_accuracy<0||u.weapon.base_accuracy>1||!std::isfinite(u.weapon.projectile_speed_mps)||u.weapon.projectile_speed_mps<=0||u.weapon.cooldown_ticks==0)return bad("invalid or duplicate unit");}
    for(const auto&o:s.officers){if(!o.id||!offs.insert(o.id.value).second||!units.contains(o.unit.value)||!off_units.insert(o.unit.value).second||o.role<1||o.role>4||!std::isfinite(o.charisma)||!std::isfinite(o.tactic_mind)||!std::isfinite(o.lead_skill)||!std::isfinite(o.lead_dist))return bad("invalid officer reference");}
    for(auto o:s.obstacles)if(!std::isfinite(o.min_x)||!std::isfinite(o.min_y)||!std::isfinite(o.max_x)||!std::isfinite(o.max_y)||o.min_x>=o.max_x||o.min_y>=o.max_y)return bad("invalid obstacle");
    const auto&r=s.rules;if(!std::isfinite(r.panic_fraction)||r.panic_fraction<0||r.panic_fraction>1||r.calm_ticks==0||!std::isfinite(r.rally_tolerance_m)||r.rally_tolerance_m<0||!std::isfinite(r.rally_safe_radius_m)||r.rally_safe_radius_m<0)return bad("invalid morale rules");
    return {Status::Ok,std::move(s),{}};
}
Result<World> CreateWorld(const Scenario&s,uint64_t seed){
    auto valid=ValidateScenario(s);if(!valid)return {valid.status,{},valid.diagnostics};World w;w.scenario=std::move(valid.value);w.seed=seed;
    auto canonical=[](auto&a,auto&b){return a.id<b.id;};std::sort(w.scenario.units.begin(),w.scenario.units.end(),canonical);std::sort(w.scenario.regiments.begin(),w.scenario.regiments.end(),canonical);std::sort(w.scenario.officers.begin(),w.scenario.officers.end(),canonical);
    for(const auto&r:w.scenario.regiments){RegimentState x;static_cast<RegimentSpec&>(x)=r;w.regiments.push_back(x);}
    for(const auto&u:w.scenario.units){UnitState x;static_cast<UnitSpec&>(x)=u;x.slot=u.pose;w.units.push_back(x);}
    std::sort(w.units.begin(),w.units.end(),[](auto&a,auto&b){return a.id<b.id;});
    std::sort(w.regiments.begin(),w.regiments.end(),[](auto&a,auto&b){return a.id<b.id;});w.officers=w.scenario.officers;
    for(const auto&u:w.units)FindRegiment(w,u.regiment)->units.push_back(u.id);
    for(auto&r:w.regiments)r.initial_count=r.units.size();
    InitializeFormations(w);
    return {Status::Ok,std::move(w),{}};
}
UnitState* FindUnit(World&w,UnitId id){auto i=std::lower_bound(w.units.begin(),w.units.end(),id,[](const auto&u,UnitId x){return u.id<x;});return i!=w.units.end()&&i->id==id?&*i:nullptr;}
const UnitState* FindUnit(const World&w,UnitId id){return FindUnit(const_cast<World&>(w),id);}
RegimentState* FindRegiment(World&w,RegimentId id){auto i=std::lower_bound(w.regiments.begin(),w.regiments.end(),id,[](const auto&r,RegimentId x){return r.id<x;});return i!=w.regiments.end()&&i->id==id?&*i:nullptr;}
const RegimentState* FindRegiment(const World&w,RegimentId id){return FindRegiment(const_cast<World&>(w),id);}
std::optional<UnitState> GetUnit(const World&w,UnitId id){auto u=FindUnit(w,id);if(u)return *u;return {};}
WorldStats GetStats(const World&w){WorldStats s;s.regiment_count=w.regiments.size();for(const auto&u:w.units){if(!u.alive){++s.dead_units;continue;}++s.living_units;bool officer=std::any_of(w.officers.begin(),w.officers.end(),[&](auto&o){return o.unit==u.id;});if(officer)++s.officer_role_count;else ++s.rank_and_file_count;}return s;}
}
