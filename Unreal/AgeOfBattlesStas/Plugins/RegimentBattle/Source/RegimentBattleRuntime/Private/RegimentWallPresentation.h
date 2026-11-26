#pragma once

#include "DynamicMesh/DynamicMesh3.h"

#include <span>

namespace battle { struct Obstacle; }

enum class ERegimentWallMaterial : int32 { Mortar, Stone, LightStone, Coping };

UE::Geometry::FDynamicMesh3 BuildRegimentWallMesh(std::span<const battle::Obstacle> Walls);
