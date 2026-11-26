#pragma once
#include "CoreMinimal.h"

THIRD_PARTY_INCLUDES_START
#include "BattleTrace.h"
THIRD_PARTY_INCLUDES_END

class FRegimentBattleTrace
{
public:
    void Configure(std::optional<battle::NavigationGridGeometry> InGrid, std::string Policy);
    void BeforeStep(const battle::World& World);
    void AfterStep(const battle::World& World, std::span<const battle::CommandEnvelope> Commands,
        battle::io::BattleTraceOutcome SimulationResult);
    void Finish(const battle::World& World);

private:
    void CollectHostOrders(const battle::World& World);
    battle::io::BattleTraceConfig Config;
    TUniquePtr<battle::io::BattleTraceWriter> Writer;
    std::vector<battle::Order> HostOrders;
    uint64 NextOrderId = 0;
    bool bStartPending = false;
};
