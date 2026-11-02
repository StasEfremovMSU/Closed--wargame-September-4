#include "battle/Simulation.h"
#include "battle/Combat.h"
#include "battle/Control.h"
#include "battle/Formation.h"
#include "battle/Officers.h"
#include <chrono>
#include <map>
namespace battle {
Result<TickResult> Step(World &w, std::span<const CommandEnvelope> input,
                        const SimulationOptions &o, const NavigationService &nav,
                        StepProfile *profile) {
    if (profile)
        *profile = {};
    using Clock = std::chrono::steady_clock;
    auto previous = profile ? Clock::now() : Clock::time_point{};
    auto mark = [&](double StepProfile::*member) {
        if (profile) {
            auto now = Clock::now();
            profile->*member = std::chrono::duration<double, std::milli>(now - previous).count();
            previous = now;
        }
    };
    if (o.tick_ms != 50 || o.worker_count == 0)
        return {Status::InvalidInput,
                {},
                {{"step", 0, "tick_ms must be 50 and worker_count positive"}}};
    auto seen = [&](uint64_t sequence) {
        return std::binary_search(w.accepted_sequences.begin(), w.accepted_sequences.end(),
                                  sequence);
    };
    std::map<std::pair<Tick, uint64_t>, unsigned> movement_sources;
    auto inspect = [&](const CommandEnvelope &e) {
        auto kind = e.command.kind;
        if (kind == CommandKind::MoveControlledUnit)
            movement_sources[{e.tick, e.command.unit.value}] |= 1;
        if (kind == CommandKind::ExternalPoseSample)
            movement_sources[{e.tick, e.command.unit.value}] |= 2;
    };
    for (const auto &e : w.commands)
        inspect(e);
    for (const auto &e : input) {
        if (seen(e.sequence))
            continue;
        if (!e.sequence || !Finite(e.command.pose) || !Finite(e.command.velocity) ||
            !Finite(e.command.order.point) || e.tick < w.tick ||
            uint8_t(e.command.kind) > uint8_t(CommandKind::ExternalPoseSample) ||
            (e.command.kind == CommandKind::ExternalPoseSample && e.command.sample_tick != e.tick))
            return {Status::InvalidInput, {}, {{"commands", 0, "invalid command packet"}}};
        inspect(e);
    }
    for (auto &entry : movement_sources)
        if (entry.second == 3)
            return {
                Status::Conflict, {}, {{"commands", 0, "local and external movement conflict"}}};
    w.events.clear();
    for (auto e : input) {
        auto i =
            std::lower_bound(w.accepted_sequences.begin(), w.accepted_sequences.end(), e.sequence);
        if (i != w.accepted_sequences.end() && *i == e.sequence)
            continue;
        w.accepted_sequences.insert(i, e.sequence);
        w.commands.push_back(e);
    }
    std::sort(w.commands.begin(), w.commands.end(), [](auto &a, auto &b) {
        return a.tick != b.tick ? a.tick < b.tick : a.sequence < b.sequence;
    });
    std::vector<CommandEnvelope> future;
    for (const auto &e : w.commands) {
        if (e.tick > w.tick) {
            future.push_back(e);
            continue;
        }
        Status status = Status::NotFound;
        if (e.command.kind == CommandKind::IssueOrder)
            status = SubmitOrder(w, e.command.order);
        else if (e.command.kind == CommandKind::ReformRegiment)
            status = ReformRegiment(w, e.command.regiment, e.command.formation);
        else
            status = ApplyControlCommand(w, e.command);
        if (status != Status::Ok)
            w.events.push_back({EventId{w.next_event_id++},
                                w.tick,
                                EventKind::CommandRejected,
                                e.command.unit,
                                {},
                                {},
                                0,
                                "unavailable or invalid command target"});
    }
    w.commands = std::move(future);
    UpdateMorale(w);
    UpdateOfficers(w, nav);
    mark(&StepProfile::orders_ms);
    AdvanceFormations(w, .05f);
    AdvanceControlled(w, .05f);
    mark(&StepProfile::movement_ms);
    std::vector<BattleEvent> attacks;
    BuildCombatEvents(w, attacks, &w.neighbor_checks);
    mark(&StepProfile::targeting_ms);
    CommitCombatEvents(w, attacks);
    ++w.tick;
    mark(&StepProfile::commit_ms);
    return {Status::Ok, {w.tick, w.events}, {}};
}
} // namespace battle
