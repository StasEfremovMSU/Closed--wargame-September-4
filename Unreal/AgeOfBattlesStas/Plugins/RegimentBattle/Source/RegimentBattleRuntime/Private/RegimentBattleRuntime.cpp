#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "RegimentMissionAuthoringTab.h"
#endif

class FRegimentBattleRuntimeModule : public FDefaultModuleImpl
{
    void StartupModule() override
    {
#if WITH_EDITOR
        RegimentMissionAuthoringTab::Register();
#endif
    }

    void ShutdownModule() override
    {
#if WITH_EDITOR
        RegimentMissionAuthoringTab::Unregister();
#endif
    }
};

IMPLEMENT_MODULE(FRegimentBattleRuntimeModule, RegimentBattleRuntime)
