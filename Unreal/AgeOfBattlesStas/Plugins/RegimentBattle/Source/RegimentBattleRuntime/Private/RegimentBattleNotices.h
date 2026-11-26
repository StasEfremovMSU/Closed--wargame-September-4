#pragma once

#include "CoreMinimal.h"
#include "battle/World.h"

enum class ERegimentBattleNotice : uint8
{
    Flank, Rear, Retreat, Rout, Rally, Rallied, OfficerLost, StandardLost, Handover, Vacant, Charge
};

struct FRegimentNoticeParticipant
{
    FText Name;
    int32 Team = INDEX_NONE;
};

struct FRegimentBattleNotice
{
    ERegimentBattleNotice Kind;
    FVector Location;
    uint64 Tick;
    FRegimentNoticeParticipant Actor;
    FRegimentNoticeParticipant Target;
    uint64 FallenUnit = 0;
    uint64 TransferTicks = 0;
    battle::AttackAspect Aspect = battle::AttackAspect::Front;
};

FText RegimentName(const battle::RegimentState& Regiment);

class FRegimentBattleNotices
{
public:
    static constexpr uint64 LifetimeTicks = 80;

    void ObserveHit(const battle::World& World, const battle::BattleEvent& Hit,
        const battle::UnitState& Target, const FVector& Location);
    void ObserveStaff(const battle::World& World, const battle::BattleEvent& Event,
        const battle::UnitState& Fallen, const FVector& Location);
    void ObserveRegiment(const battle::RegimentState& Regiment, uint64 Tick, const FVector& Location);
    void Expire(uint64 Tick);
    TConstArrayView<FRegimentBattleNotice> GetEntries() const { return Entries; }

private:
    struct FRegimentState
    {
        TOptional<FVector> LastPosition;
        bool bRetreating = false;
        bool bPanicking = false;
        bool bRallying = false;
    };

    struct FStrikeKey
    {
        uint64 Actor;
        uint64 Target;
        battle::AttackAspect Aspect;
        bool bIndependentActor;
        bool bCharge;

        bool operator==(const FStrikeKey&) const = default;

        friend uint32 GetTypeHash(const FStrikeKey& Key)
        {
            const uint32 Participants = HashCombineFast(::GetTypeHash(Key.Actor), ::GetTypeHash(Key.Target));
            return HashCombineFast(Participants,
                uint32(Key.Aspect) * 4 + uint32(Key.bIndependentActor) * 2 + uint32(Key.bCharge));
        }
    };

    void Add(FRegimentBattleNotice Notice);

    TArray<FRegimentBattleNotice> Entries;
    TMap<uint64, FRegimentState> States;
    TMap<FStrikeKey, uint64> LastStrikes;
    struct FCommandLoss { uint64 Tick; FRegimentNoticeParticipant Command; };
    TMap<uint64, FCommandLoss> OfficerLosses;
};
