#pragma once
#include "battle/Snapshot.h"
namespace battle::io {
std::string ExportSvg(const Snapshot &, const Scenario &);
}
