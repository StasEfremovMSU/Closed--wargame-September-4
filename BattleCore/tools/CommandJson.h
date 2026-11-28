#pragma once
#include "battle/command/Runtime.h"
#include "json.hpp"
#include <limits>

namespace nlohmann {
template <class T> struct adl_serializer<std::optional<T>> {
    static void to_json(json &j, const std::optional<T> &value) {
        if (value)
            j = *value;
        else
            j = nullptr;
    }
    static void from_json(const json &j, std::optional<T> &value) {
        if (j.is_null())
            value.reset();
        else
            value = j.get<T>();
    }
};
} // namespace nlohmann

namespace battle::command {
namespace json_detail {
inline uint64_t Unsigned(const nlohmann::json &j, const char *name) {
    if (j.is_number_unsigned())
        return j.get<uint64_t>();
    if (j.is_number_integer()) {
        const auto value = j.get<int64_t>();
        if (value >= 0)
            return uint64_t(value);
    }
    throw std::invalid_argument(std::string(name) + " must be a nonnegative integer");
}
inline void StrictInputTypes(const nlohmann::json &actual, const nlohmann::json &canonical,
                             const std::string &path) {
    if (actual.is_object()) {
        if (!canonical.is_object())
            throw std::invalid_argument("unexpected object at " + path);
        for (auto it = actual.begin(); it != actual.end(); ++it) {
            if (!canonical.contains(it.key()))
                throw std::invalid_argument("unknown field at " + path + "/" + it.key());
            StrictInputTypes(it.value(), canonical.at(it.key()), path + "/" + it.key());
        }
    } else if (actual.is_array()) {
        if (!canonical.is_array() || actual.size() != canonical.size())
            throw std::invalid_argument("invalid array at " + path);
        for (size_t i = 0; i < actual.size(); ++i)
            StrictInputTypes(actual[i], canonical[i], path + "/" + std::to_string(i));
    } else if (canonical.is_number_integer()) {
        if (!actual.is_number_integer())
            throw std::invalid_argument("expected integer at " + path);
        if (canonical.is_number_unsigned() && actual.is_number_integer() &&
            !actual.is_number_unsigned() && actual.get<int64_t>() < 0)
            throw std::invalid_argument("expected nonnegative integer at " + path);
    } else if (canonical.is_number_float()) {
        if (!actual.is_number())
            throw std::invalid_argument("expected number at " + path);
    } else if (canonical.is_boolean() && !actual.is_boolean()) {
        throw std::invalid_argument("expected boolean at " + path);
    } else if (canonical.is_string() && !actual.is_string()) {
        throw std::invalid_argument("expected string at " + path);
    } else if (canonical.is_null() && !actual.is_null()) {
        // Optional values are null in the canonical record only when the input was null.
        throw std::invalid_argument("expected null at " + path);
    }
}
} // namespace json_detail

inline void ValidateCommandInputTypes(const nlohmann::json &input,
                                      const nlohmann::json &canonical,
                                      const std::string &path) {
    json_detail::StrictInputTypes(input, canonical, path);
}

#define BATTLE_COMMAND_ENUM_JSON(Type, Last)                                                      \
    inline void to_json(nlohmann::json &j, Type value) { j = uint8_t(value); }                     \
    inline void from_json(const nlohmann::json &j, Type &value) {                                  \
        const auto raw = json_detail::Unsigned(j, #Type);                                          \
        if (raw > uint8_t(Last))                                                                   \
            throw std::invalid_argument("invalid " #Type " value");                              \
        value = Type(raw);                                                                         \
    }

BATTLE_COMMAND_ENUM_JSON(Level, Level::Regiment)
BATTLE_COMMAND_ENUM_JSON(SectorRole, SectorRole::Detached)
BATTLE_COMMAND_ENUM_JSON(SupportDuty, SupportDuty::Bodyguard)
BATTLE_COMMAND_ENUM_JSON(StubStatus, StubStatus::InvalidInput)
BATTLE_COMMAND_ENUM_JSON(ObservationSource, ObservationSource::Memory)
BATTLE_COMMAND_ENUM_JSON(Allegiance, Allegiance::Enemy)
BATTLE_COMMAND_ENUM_JSON(RelativeSector, RelativeSector::Rear)
BATTLE_COMMAND_ENUM_JSON(TaskKind, TaskKind::Reform)
BATTLE_COMMAND_ENUM_JSON(InitiativePolicy, InitiativePolicy::Free)
BATTLE_COMMAND_ENUM_JSON(TaskStatus, TaskStatus::Cancelled)
BATTLE_COMMAND_ENUM_JSON(DurationOrigin, DurationOrigin::Delivered)
BATTLE_COMMAND_ENUM_JSON(ReportKind, ReportKind::CommanderLost)
BATTLE_COMMAND_ENUM_JSON(DeliveryChannel, DeliveryChannel::Courier)
BATTLE_COMMAND_ENUM_JSON(DeliveryStatus, DeliveryStatus::RecipientUnavailable)
BATTLE_COMMAND_ENUM_JSON(BrainStage, BrainStage::ReportUpward)
BATTLE_COMMAND_ENUM_JSON(FsmState, FsmState::Complete)
BATTLE_COMMAND_ENUM_JSON(FireDiscipline, FireDiscipline::Conserve)
BATTLE_COMMAND_ENUM_JSON(TargetPolicy, TargetPolicy::GreatestThreat)
#undef BATTLE_COMMAND_ENUM_JSON

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CommandNode, id, parent, level, team, name,
                                                sector_role, commander, succession, children,
                                                regiment)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(OfficerProfile, officer, tactical_skill, initiative,
                                                discipline, aggression, caution, charisma,
                                                leadership, reaction_delay_ticks,
                                                reevaluation_ticks, minimum_commitment_ticks,
                                                command_capacity, command_radius_m)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SupportAssignment, officer, command, duty)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CommandHierarchy, nodes, officers, support)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(BrainSchedule, node, next_think_tick,
                                                interval_ticks, commitment_until)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(KnownContact, command, regiment, allegiance, team,
                                                estimated_pose, estimated_strength, observed_tick,
                                                confidence, source)
inline void to_json(nlohmann::json &j, const OwnForceSummary &force) {
    j = {{"living", force.living}, {"initial", force.initial}, {"pose", force.pose},
         {"panic", force.panic}, {"morale", force.morale}, {"cohesion", force.cohesion},
         {"fatigue", force.fatigue}, {"ammunition_fraction", force.ammunition_fraction}};
    if (force.under_ranged_fire)
        j["under_ranged_fire"] = true;
}
inline void from_json(const nlohmann::json &j, OwnForceSummary &force) {
    force.living = j.value("living", uint32_t{});
    force.initial = j.value("initial", uint32_t{});
    force.pose = j.value("pose", Pose{});
    force.panic = j.value("panic", false);
    force.morale = j.value("morale", std::optional<float>{});
    force.cohesion = j.value("cohesion", std::optional<float>{});
    force.fatigue = j.value("fatigue", std::optional<float>{});
    force.ammunition_fraction = j.value("ammunition_fraction", std::optional<float>{});
    force.under_ranged_fire = j.value("under_ranged_fire", false);
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ThreatEstimate, sector, strength, confidence,
                                                under_ranged_fire, cavalry_approaching)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TerrainAssessment, position, cover, passability,
                                                height_advantage)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LocalSituation, observer, tick, own, rally_point,
                                                contacts, threats, terrain)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CommandIntent, purpose, objective_position,
                                                objective_radius_m, target_command, target_regiment,
                                                flank_waypoint)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AuthorityLimits, initiative, may_pursue,
                                                may_break_formation, may_change_target,
                                                pursuit_limit_m, acceptable_loss_fraction)
inline void to_json(nlohmann::json &j, const CommandTask &t) {
    j = {{"id", t.id}, {"parent_task", t.parent_task}, {"issuer", t.issuer}, {"recipient", t.recipient},
         {"author", t.author}, {"action", t.action}, {"intent", t.intent}, {"authority", t.authority},
         {"priority", t.priority}, {"created_tick", t.created_tick}, {"not_before_tick", t.not_before_tick},
         {"expiry_tick", t.expiry_tick}, {"duration_ticks", t.duration_ticks},
         {"duration_origin", t.duration_origin}, {"status", t.status}, {"formation_columns", t.formation_columns},
         {"formation_spacing_m", t.formation_spacing_m}, {"received_tick", t.received_tick}};
    if (t.supersede_player_orders_through)
        j["supersede_player_orders_through"] = *t.supersede_player_orders_through;
}
inline void from_json(const nlohmann::json &j, CommandTask &t) {
    const CommandTask defaults;
    t.id = j.value("id", defaults.id);
    t.parent_task = j.value("parent_task", defaults.parent_task);
    t.issuer = j.value("issuer", defaults.issuer);
    t.recipient = j.value("recipient", defaults.recipient);
    t.author = j.value("author", defaults.author);
    t.action = j.value("action", defaults.action);
    t.intent = j.value("intent", defaults.intent);
    t.authority = j.value("authority", defaults.authority);
    t.priority = j.value("priority", defaults.priority);
    t.created_tick = j.value("created_tick", defaults.created_tick);
    t.not_before_tick = j.value("not_before_tick", defaults.not_before_tick);
    t.expiry_tick = j.value("expiry_tick", defaults.expiry_tick);
    t.duration_ticks = j.value("duration_ticks", defaults.duration_ticks);
    t.duration_origin = j.value("duration_origin", defaults.duration_origin);
    t.status = j.value("status", defaults.status);
    t.formation_columns = j.value("formation_columns", defaults.formation_columns);
    t.formation_spacing_m = j.value("formation_spacing_m", defaults.formation_spacing_m);
    t.received_tick = j.value("received_tick", defaults.received_tick);
    t.supersede_player_orders_through.reset();
    if (const auto cutoff = j.find("supersede_player_orders_through"); cutoff != j.end())
        t.supersede_player_orders_through = json_detail::Unsigned(*cutoff, "supersede_player_orders_through");
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SituationReport, kind, sender, recipient,
                                                about_task, observed_tick, created_tick, situation,
                                                explanation)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TacticalOption, action, recipient, target_command,
                                                target_regiment, destination, rationale)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DecisionRecord, tick, cause, selected, explanation)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(BrainMemory, state, state_entered_tick,
                                                task_started_tick, active_task, task_status, target,
                                                goal, remembered_contacts, recent_decisions,
                                                acknowledged_tasks, last_think_tick)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(NodeRuntime, node, memory, orders, reports,
                                                schedule, last_order_id, sent_tasks)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CommandSetup, hierarchy, missions)

inline void to_json(nlohmann::json &j, const MessageEnvelope &m) {
    j = {{"id", m.id}, {"sender", m.sender}, {"recipient", m.recipient},
         {"channel", m.channel}, {"courier", m.courier}, {"created_tick", m.created_tick},
         {"delivery_tick", m.delivery_tick}, {"expiry_tick", m.expiry_tick},
         {"status", m.status}};
    if (const auto *task = std::get_if<CommandTask>(&m.payload))
        j["payload"] = {{"kind", "task"}, {"value", *task}};
    else
        j["payload"] = {{"kind", "report"}, {"value", std::get<SituationReport>(m.payload)}};
}
inline void from_json(const nlohmann::json &j, MessageEnvelope &m) {
    m.id = j.at("id").get<MessageId>();
    m.sender = j.at("sender").get<NodeId>();
    m.recipient = j.at("recipient").get<NodeId>();
    m.channel = j.at("channel").get<DeliveryChannel>();
    m.courier = j.at("courier").get<UnitId>();
    m.created_tick = json_detail::Unsigned(j.at("created_tick"), "created_tick");
    m.delivery_tick = json_detail::Unsigned(j.at("delivery_tick"), "delivery_tick");
    m.expiry_tick.reset();
    if (!j.at("expiry_tick").is_null())
        m.expiry_tick = json_detail::Unsigned(j.at("expiry_tick"), "expiry_tick");
    m.status = j.at("status").get<DeliveryStatus>();
    const auto &payload = j.at("payload");
    if (!payload.is_object() || payload.size() != 2 || !payload.contains("kind") ||
        !payload.contains("value") || !payload.at("kind").is_string())
        throw std::invalid_argument("message payload must contain a kind and value");
    const auto kind = payload.at("kind").get<std::string>();
    if (kind == "task")
        m.payload = payload.at("value").get<CommandTask>();
    else if (kind == "report")
        m.payload = payload.at("value").get<SituationReport>();
    else
        throw std::invalid_argument("unknown message payload kind");
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CommunicationsState, messages)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CommandState, hierarchy, nodes, communications,
                                                next_task_id, next_message_id)

} // namespace battle::command
