#include "battle/Simulation.h"
#include "battle/Combat.h"
#include "battle/Control.h"
#include "battle/Formation.h"
#include "battle/Officers.h"
#include "LocalMelee.h"
#include "RegimentDiscipline.h"
#include "FieldCombat.h"
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
        if (kind == CommandKind::MoveControlledUnit || kind == CommandKind::FaceControlledUnit ||
            kind == CommandKind::JumpControlledUnit ||
            kind == CommandKind::ParryControlledUnit)
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
            !ValidOrderShape(e.command.order) || e.tick < w.tick ||
            uint8_t(e.command.kind) > uint8_t(CommandKind::FireFieldGun) ||
            (e.command.kind == CommandKind::IssueCommandTask &&
             !command::ValidTaskRequest(e.command.command_task)) ||
            (e.command.kind == CommandKind::ExternalPoseSample && e.command.sample_tick != e.tick))
            return {Status::InvalidInput, {}, {{"commands", 0, "invalid command packet"}}};
        inspect(e);
    }
    for (auto &entry : movement_sources)
        if (entry.second == 3)
            return {
                Status::Conflict, {}, {{"commands", 0, "local and external movement conflict"}}};
    std::vector<std::pair<UnitId, Pose>> siege_before_movement;
    for (const auto &unit : w.units)
        if (unit.alive && IsSiegeEngine(unit.platform))
            siege_before_movement.emplace_back(unit.id, unit.pose);
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
    std::optional<Command> field_gun_shot;
    for (const auto &e : w.commands) {
        if (e.tick > w.tick) {
            future.push_back(e);
            continue;
        }
        Status status = Status::NotFound;
        if (e.command.kind == CommandKind::IssueOrder) {
            auto order = e.command.order;
            order.player_direct = false;
            if (order.priority == PlayerOrderPriority && w.controlled) {
                const auto issuer = std::ranges::find(w.officers, order.issuer, &OfficerSpec::id);
                order.player_direct = issuer != w.officers.end() && issuer->unit == w.controlled &&
                                      CanCommandRegiment(w, w.controlled, order.recipient);
            }
            status = SubmitOrder(w, order);
        } else if (e.command.kind == CommandKind::ReformRegiment) {
            status = ReformRegiment(w, e.command.regiment, e.command.formation);
            if (status == Status::Ok) {
                auto &regiment = *FindRegiment(w, e.command.regiment);
                if (UsesLinearInfantry(w, regiment)) {
                    regiment.manual_frontage = true;
                    regiment.decision_tick = 0;
                    regiment.route.clear();
                    regiment.waypoint = 0;
                }
            }
        } else if (e.command.kind == CommandKind::SetSkirmishers)
            status = SetSkirmishers(w, e.command.regiment, e.command.skirmishers_enabled);
        else if (e.command.kind == CommandKind::IssueCommandTask) {
            auto submitted = command::SubmitTask(w, e.command.command_task);
            status =
                submitted.status == command::StubStatus::Ready ? Status::Ok : Status::InvalidInput;
        } else if (e.command.kind == CommandKind::FireFieldGun) {
            if (!field_gun_shot && e.command.unit && e.command.unit == w.controlled && e.command.target) {
                field_gun_shot = e.command;
                status = Status::Ok;
            }
        } else
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
    if (!w.scenario.rules.regiment_tactics)
        UpdateMorale(w);
    auto command = command::UpdateCommand(w, nav, o.group_policy, o.group_order_commitment);
    if (command.status != command::StubStatus::Ready)
        return {Status::InvalidInput, {}, {{"command.runtime", 0, "command update rejected"}}};
    UpdateFieldPosture(w);
    UpdateOfficers(w, nav);
    mark(&StepProfile::orders_ms);
    std::optional<LocalMelee> contact;
    if (w.scenario.rules.local_melee) {
        UpdateSkirmishers(w);
        contact.emplace(w, .05f);
    }
    AdvanceFormations(w, .05f, contact ? &*contact : nullptr);
    auto *controlled = FindUnit(w, w.controlled);
    const bool jumping = controlled && controlled->jump_started_tick;
    AdvanceControlled(w, .05f);
    if (contact) {
        if (controlled && controlled->alive) {
            const Vec3 destination = controlled->pose.position;
            controlled->pose.position = contact->Constrain(*controlled, destination);
            if (jumping)
                controlled->pose.position.z = destination.z;
        }
        contact->ApplyShoves(w);
        contact->Refresh(w);
    }
    mark(&StepProfile::movement_ms);
    std::vector<UnitId> moving_siege;
    for (const auto &[id, pose] : siege_before_movement)
        if (FindUnit(w, id)->pose != pose)
            moving_siege.push_back(id);
    std::vector<BattleEvent> attacks;
    BuildCombatEvents(w, attacks, &w.neighbor_checks, contact ? &*contact : nullptr, moving_siege,
                      field_gun_shot ? &*field_gun_shot : nullptr);
    mark(&StepProfile::targeting_ms);
    CommitCombatEvents(w, attacks, contact ? &*contact : nullptr);
    if (field_gun_shot) {
        const auto *officer = FindUnit(w, field_gun_shot->unit);
        const auto fired = std::ranges::any_of(attacks, [&](const auto &event) {
            if (!officer || event.regiment != officer->regiment || event.target != field_gun_shot->target)
                return false;
            return event.reason == FieldGunRoundShotReason || event.reason == FieldGunCanisterReason;
        });
        if (!fired)
            w.events.push_back({EventId{w.next_event_id++}, w.tick, EventKind::CommandRejected,
                                field_gun_shot->unit, field_gun_shot->target, {}, 0,
                                std::string(FieldGunRejectedReason)});
    }
    if (w.scenario.rules.regiment_tactics)
        UpdateRegimentDiscipline(w);
    ++w.tick;
    CompleteDeployments(w);
    mark(&StepProfile::commit_ms);
    return {Status::Ok, {w.tick, w.events}, {}};
}
} // namespace battle
