#pragma once
#include "battle/Scenario.h"
#include <filesystem>
namespace battle::io {Result<Scenario> LoadScenario(const std::filesystem::path&);Result<bool> SaveScenario(const std::filesystem::path&,const Scenario&);}
