#include "battle/command/GroupPolicy.h"

#include <cmath>
#include <istream>

namespace battle::command {
std::optional<GroupPolicy> LoadGroupPolicy(std::istream &input) {
    std::string signature;
    size_t hidden{};
    GroupPolicy policy;
    if (!(input >> signature >> policy.schema_ >> policy.input_size_ >> hidden >> policy.baseline_margin_) ||
        signature != "BATTLE_GROUP_MLP" || hidden != GroupPolicyHiddenSize ||
        !std::isfinite(policy.baseline_margin_) || policy.baseline_margin_ < 0 ||
        policy.baseline_margin_ > 1)
        return {};
    const bool legacy = policy.schema_ == GroupPolicyFeatureSchema &&
                        policy.input_size_ == GroupPolicyFeatureNames.size();
    const bool commitment = policy.schema_ == GroupPolicyCommitmentFeatureSchema &&
                             policy.input_size_ == GroupPolicyCommitmentFeatureNames.size();
    if (!legacy && !commitment)
        return {};
    const auto read = [&](auto &&weights) {
        for (auto &weight : weights)
            if (!(input >> weight) || !std::isfinite(weight) || std::abs(weight) > 256.f)
                return false;
        return true;
    };
    std::span bias{&policy.output_bias_, size_t{1}};
    if (!read(std::span{policy.input_weights_}.first(policy.input_size_ * GroupPolicyHiddenSize)) ||
        !read(policy.input_bias_) ||
        !read(policy.output_weights_) || !read(bias))
        return {};
    input >> std::ws;
    if (!input.eof())
        return {};
    return policy;
}

StubResult<std::vector<ActionScore>> GroupPolicy::Evaluate(const EvaluationRequest &request) const {
    const size_t limit = schema_ == GroupPolicyCommitmentFeatureSchema ? 4 : 3;
    if (!input_size_ || request.level != Level::Group || request.feature_schema != schema_ ||
        request.options.empty() || request.options.size() > limit ||
        request.baseline_option >= request.options.size() ||
        request.features.size() != input_size_ * request.options.size())
        return {StubStatus::InvalidInput, std::nullopt, "Invalid group policy feature schema."};
    for (float feature : request.features)
        if (!std::isfinite(feature))
            return {StubStatus::InvalidInput, std::nullopt, "Non-finite group policy feature."};

    std::vector<ActionScore> scores;
    scores.reserve(request.options.size());
    for (size_t option = 0; option < request.options.size(); ++option) {
        const auto row = request.features.subspan(option * input_size_, input_size_);
        float utility = output_bias_;
        for (size_t neuron = 0; neuron < GroupPolicyHiddenSize; ++neuron) {
            float activation = input_bias_[neuron];
            for (size_t feature = 0; feature < input_size_; ++feature)
                activation += input_weights_[neuron * input_size_ + feature] * row[feature];
            utility += output_weights_[neuron] * std::max(0.f, activation);
        }
        if (option == request.baseline_option)
            utility += baseline_margin_;
        scores.push_back({static_cast<uint32_t>(option), utility, {}});
    }
    return {StubStatus::Ready, std::move(scores), "Trained group policy options scored."};
}
} // namespace battle::command
