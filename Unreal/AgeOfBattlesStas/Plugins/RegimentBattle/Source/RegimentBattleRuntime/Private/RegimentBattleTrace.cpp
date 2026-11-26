#include "RegimentBattleTrace.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogRegimentBattleTrace, Log, All);

namespace
{
bool LogTraceError(const battle::Result<bool>& Result)
{
    if (Result)
        return true;
    for (const auto& Diagnostic : Result.diagnostics)
        UE_LOG(LogRegimentBattleTrace, Warning, TEXT("%s"), UTF8_TO_TCHAR(Diagnostic.message.c_str()));
    return false;
}
}

void FRegimentBattleTrace::Configure(std::optional<battle::NavigationGridGeometry> InGrid, std::string Policy)
{
    bStartPending = FParse::Param(FCommandLine::Get(), TEXT("RegimentTrace"));
    if (bStartPending)
        Config = {InGrid, std::move(Policy), false};
}

void FRegimentBattleTrace::BeforeStep(const battle::World& World)
{
    if (bStartPending)
    {
        bStartPending = false;
        FString BaseDirectory;
        if (!FParse::Value(FCommandLine::Get(), TEXT("RegimentTraceDirectory="), BaseDirectory) || BaseDirectory.IsEmpty())
            BaseDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BattleTraces"));
        const FString Name = FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT("-") +
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(BaseDirectory, Name));
        Writer = MakeUnique<battle::io::BattleTraceWriter>();
        if (LogTraceError(Writer->Begin(std::filesystem::path(*Directory), World, Config)))
        {
            NextOrderId = World.next_order_id;
            UE_LOG(LogRegimentBattleTrace, Display, TEXT("Recording battle at %s"), *Directory);
        }
        else
            Writer.Reset();
    }
    if (Writer)
        CollectHostOrders(World);
}

void FRegimentBattleTrace::CollectHostOrders(const battle::World& World)
{
    HostOrders.clear();
    if (NextOrderId != World.next_order_id)
        for (const auto& Order : World.pending_orders)
            if (Order.id.value >= NextOrderId)
                HostOrders.push_back(Order);
}

void FRegimentBattleTrace::AfterStep(const battle::World& World,
    std::span<const battle::CommandEnvelope> Commands, battle::io::BattleTraceOutcome SimulationResult)
{
    if (!Writer)
        return;
    std::optional<battle::io::BattleTraceOutcome> ObjectiveResult;
    if (SimulationResult.outcome != World.outcome || SimulationResult.winner != World.winner_team)
        ObjectiveResult = battle::io::BattleTraceOutcome{World.outcome, World.winner_team};
    if (!LogTraceError(Writer->Record(Commands, World, HostOrders, ObjectiveResult)))
    {
        Writer.Reset();
        return;
    }
    NextOrderId = World.next_order_id;
    if (World.outcome != battle::BattleOutcome::Running)
        Finish(World);
}

void FRegimentBattleTrace::Finish(const battle::World& World)
{
    if (Writer)
    {
        CollectHostOrders(World);
        LogTraceError(Writer->Finish(World, HostOrders));
        Writer.Reset();
    }
}
