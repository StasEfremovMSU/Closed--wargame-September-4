#pragma once
#include "Types.h"
#include "Scenario.h"
namespace battle {
struct FormationFootprint{float width_m{},depth_m{};};
struct PathRequest{Pose start{};Vec3 goal{};FormationFootprint footprint{};};
struct PathResult{Status status{Status::Ok};std::vector<Pose> waypoints;};
class NavigationService{public:virtual ~NavigationService()=default;virtual PathResult FindPath(const PathRequest&)const=0;};
class FlatNavigationService:public NavigationService{public:PathResult FindPath(const PathRequest& p)const override{return {Status::Ok,{{p.goal,p.start.yaw}}};}};
class GridNavigationService:public NavigationService{
 std::vector<Obstacle> obstacles_;float cell_m_;uint32_t width_,height_;Vec3 origin_;
public:explicit GridNavigationService(std::vector<Obstacle> obstacles={},float cell_m=2,uint32_t width=256,uint32_t height=256,Vec3 origin={-256,-256,0}):obstacles_(std::move(obstacles)),cell_m_(cell_m),width_(width),height_(height),origin_(origin){}
 bool CanTraverse(Pose from,Pose to,FormationFootprint)const;
 PathResult FindPath(const PathRequest&)const override;
};
}
