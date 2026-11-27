#pragma once
#include "battle/Scenario.h"

namespace battle {
struct CavalryTuning {
    float turn_scale;
    float charge_start_pace;
    float charge_runup_m;
    float charge_momentum;
    float shove_scale;
};

constexpr CavalryTuning HorseCavalryTuning(const UnitSpec &rider) {
    if (rider.platform != BattlePlatform::None)
        return {1.f, 1.45f, 3.f, 1.f, 1.f};
    switch (rider.cavalry) {
    case CavalryClass::Heavy: return {.70f, .72f, 5.5f, 1.65f, 1.12f};
    case CavalryClass::Light: return {1.20f, 1.25f, 2.5f, .55f, .58f};
    default: return {1.f, 1.45f, 3.f, 1.f, .76f};
    }
}
} // namespace battle
