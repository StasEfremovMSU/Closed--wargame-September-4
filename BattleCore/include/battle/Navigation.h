#pragma once
#include "Types.h"
namespace battle {
struct FormationFootprint{float width_m{},depth_m{};};
struct PathRequest{Pose start{};Vec3 goal{};FormationFootprint footprint{};};
struct PathResult{Status status{Status::Ok};std::vector<Pose> waypoints;};
class NavigationService{public:virtual ~NavigationService()=default;virtual PathResult FindPath(const PathRequest&)const=0;};
class FlatNavigationService:public NavigationService{public:PathResult FindPath(const PathRequest& p)const override{return {Status::Ok,{{p.goal,p.start.yaw}}};}};
}
