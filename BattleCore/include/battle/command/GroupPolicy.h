#pragma once
#include "Brains.h"

#include <array>
#include <iosfwd>

namespace battle::command {
inline constexpr uint32_t GroupPolicyFeatureSchema = 2;
// Rows are in candidate order. Training consumes these names from the simulator CSV.
inline constexpr auto GroupPolicyFeatureNames = std::to_array<std::string_view>({
    "choose_support",         "choose_alternate",     "choose_flank",
    "previous_support",       "previous_alternate",   "previous_flank",
    "group_living_fraction",  "lead_living_fraction", "wing_living_fraction",
    "lead_strength",          "wing_strength",        "primary_strength",
    "primary_strength_known", "candidate_strength",   "candidate_strength_known",
    "primary_confidence",     "candidate_confidence", "wing_primary_range",
    "lead_primary_range",     "wing_candidate_range", "wing_lead_range",
    "option_travel",          "lead_melee_contact",   "ranged_pressure",
    "alternate_available",    "flank_available",      "wing_primary_forward",
    "wing_primary_lateral",
});
inline constexpr size_t GroupPolicyHiddenSize = 24;
inline constexpr uint32_t GroupPolicyCommitmentFeatureSchema = 3;
inline constexpr auto GroupPolicyCommitmentFeatureNames = [] {
    constexpr auto extra = std::to_array<std::string_view>(
        {"choose_keep", "order_pending", "order_executing", "order_age", "order_progress"});
    std::array<std::string_view, GroupPolicyFeatureNames.size() + extra.size()> names{};
    for (size_t i = 0; i < GroupPolicyFeatureNames.size(); ++i)
        names[i] = GroupPolicyFeatureNames[i];
    for (size_t i = 0; i < extra.size(); ++i)
        names[GroupPolicyFeatureNames.size() + i] = extra[i];
    return names;
}();

class GroupPolicy final : public ITacticalEvaluator {
  public:
    StubResult<std::vector<ActionScore>> Evaluate(const EvaluationRequest &) const override;

  private:
    friend std::optional<GroupPolicy> LoadGroupPolicy(std::istream &);
    uint32_t schema_{};
    size_t input_size_{};
    std::array<float, GroupPolicyHiddenSize * GroupPolicyCommitmentFeatureNames.size()> input_weights_{};
    std::array<float, GroupPolicyHiddenSize> input_bias_{};
    std::array<float, GroupPolicyHiddenSize> output_weights_{};
    float output_bias_{};
    float baseline_margin_{};
};

// Loads a versioned portable single-hidden-layer ReLU network. Blank or invalid input is rejected.
std::optional<GroupPolicy> LoadGroupPolicy(std::istream &);
} // namespace battle::command
