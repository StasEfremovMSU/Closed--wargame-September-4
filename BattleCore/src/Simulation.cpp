#include "battle/Simulation.h"
#include "battle/Formation.h"
#include "battle/Officers.h"
namespace battle {
Result<TickResult> Step(World&w,std::span<const CommandEnvelope> input,const SimulationOptions&o,const NavigationService&nav){
    (void)nav;
    if(o.tick_ms!=50||o.worker_count==0)return {Status::InvalidInput,{},{{"step",0,"tick_ms must be 50 and worker_count positive"}}};
    for(const auto&e:input)if(!e.sequence||!Finite(e.command.pose)||!Finite(e.command.velocity)||!Finite(e.command.order.point)||e.tick<w.tick)return {Status::InvalidInput,{},{{"commands",0,"invalid command packet"}}};
    w.events.clear();
    for(auto e:input){auto i=std::lower_bound(w.accepted_sequences.begin(),w.accepted_sequences.end(),e.sequence);if(i!=w.accepted_sequences.end()&&*i==e.sequence)continue;w.accepted_sequences.insert(i,e.sequence);w.commands.push_back(e);}
    std::sort(w.commands.begin(),w.commands.end(),[](auto&a,auto&b){return a.tick!=b.tick?a.tick<b.tick:a.sequence<b.sequence;});
    std::vector<CommandEnvelope> future;
    for(const auto&e:w.commands){if(e.tick>w.tick){future.push_back(e);continue;}Status status=Status::NotFound;if(e.command.kind==CommandKind::IssueOrder)status=SubmitOrder(w,e.command.order);else if(e.command.kind==CommandKind::ReformRegiment)status=ReformRegiment(w,e.command.regiment,e.command.formation);if(status!=Status::Ok)w.events.push_back({EventId{w.next_event_id++},w.tick,EventKind::CommandRejected,e.command.unit,{}, {},0,"unavailable or invalid command target"});}
    w.commands=std::move(future);UpdateOfficers(w,nav);AdvanceFormations(w,.05f);++w.tick;return {Status::Ok,{w.tick,w.events},{}};
}
}
