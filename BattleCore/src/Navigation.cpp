#include "battle/Navigation.h"
#include "battle/Formation.h"
#include <queue>
#include <limits>
namespace battle {
bool GridNavigationService::CanTraverse(Pose a,Pose b,FormationFootprint fp)const{
 if(!Finite(a)||!Finite(b)||!std::isfinite(fp.width_m)||!std::isfinite(fp.depth_m)||fp.width_m<=0||fp.depth_m<=0||cell_m_<=0||!width_||!height_)return false;
 bool turn=std::abs(std::remainder(b.yaw-a.yaw,2*Pi))>.00001f;
 float radius=std::hypot(fp.width_m,fp.depth_m)*.5f;
 for(Pose p:{a,b}){float ex=turn?radius:(std::abs(std::cos(p.yaw))*fp.depth_m+std::abs(std::sin(p.yaw))*fp.width_m)*.5f;float ey=turn?radius:(std::abs(std::sin(p.yaw))*fp.depth_m+std::abs(std::cos(p.yaw))*fp.width_m)*.5f;if(p.position.x-ex<origin_.x-cell_m_*.5f||p.position.x+ex>origin_.x+(float(width_)-.5f)*cell_m_||p.position.y-ey<origin_.y-cell_m_*.5f||p.position.y+ey>origin_.y+(float(height_)-.5f)*cell_m_)return false;}
 for(auto&o:obstacles_){Pose box{{(o.min_x+o.max_x)*.5f,(o.min_y+o.max_y)*.5f,a.position.z},0};FormationFootprint size{o.max_y-o.min_y,o.max_x-o.min_x};if(turn){float x=std::clamp(a.position.x,o.min_x,o.max_x),y=std::clamp(a.position.y,o.min_y,o.max_y);if(std::hypot(a.position.x-x,a.position.y-y)<radius)return false;}if(SweepLimit(a,b,fp,box,size)<.99999f||RectanglesOverlap(b,fp,box,size))return false;}
 return true;
}
PathResult GridNavigationService::FindPath(const PathRequest&r)const{
 auto fail=[](){return PathResult{Status::NoPath,{}};};
 if(!Finite(r.start)||!Finite(r.goal)||!std::isfinite(cell_m_)||cell_m_<=0||!width_||!height_||uint64_t(width_)*height_>1000000)return fail();
 auto point=[&](int x,int y,int h){return Pose{{origin_.x+x*cell_m_,origin_.y+y*cell_m_,r.start.position.z},h*Pi/2};};
 auto cell=[&](Vec3 p){return std::pair{int(std::round((p.x-origin_.x)/cell_m_)),int(std::round((p.y-origin_.y)/cell_m_))};};
 // Reject bounds before converting untrusted coordinates to int.
 if(r.start.position.x<origin_.x||r.start.position.y<origin_.y||r.start.position.x>origin_.x+(width_-1)*cell_m_||r.start.position.y>origin_.y+(height_-1)*cell_m_||r.goal.x<origin_.x||r.goal.y<origin_.y||r.goal.x>origin_.x+(width_-1)*cell_m_||r.goal.y>origin_.y+(height_-1)*cell_m_)return fail();
 auto[sx,sy]=cell(r.start.position);auto[gx,gy]=cell(r.goal);int sh=int(std::round(std::remainder(r.start.yaw,2*Pi)/(Pi/2)));sh=(sh%4+4)%4;auto start=point(sx,sy,sh);
 Pose initial_turn{r.start.position,start.yaw};if(!CanTraverse(r.start,initial_turn,r.footprint)||!CanTraverse(initial_turn,start,r.footprint))return fail();
 auto node=[&](int x,int y,int h){return (y*int(width_)+x)*4+h;};int begin=node(sx,sy,sh);size_t total=size_t(width_)*height_*4;
 std::vector<int> cost(total,std::numeric_limits<int>::max()),parent(total,-1);using Entry=std::pair<int,int>;std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> open;cost[begin]=0;open.push({std::abs(gx-sx)+std::abs(gy-sy),begin});int found=-1;
 constexpr int dx[]{1,0,-1,0},dy[]{0,1,0,-1};
 while(!open.empty()){
  int n=open.top().second;open.pop();int h=n%4,x=(n/4)%int(width_),y=(n/4)/int(width_);auto pose=point(x,y,h);
  if(x==gx&&y==gy&&CanTraverse(pose,{r.goal,pose.yaw},r.footprint)){found=n;break;}
  for(int action=0;action<4;++action){int xx=x,yy=y,hh=h;if(action<2){int direction=action==0?1:-1;xx+=dx[h]*direction;yy+=dy[h]*direction;}else hh=(h+(action==2?1:3))%4;if(xx<0||yy<0||xx>=int(width_)||yy>=int(height_))continue;int next=node(xx,yy,hh);if(cost[n]+1>=cost[next])continue;auto target=point(xx,yy,hh);if(!CanTraverse(pose,target,r.footprint))continue;cost[next]=cost[n]+1;parent[next]=n;open.push({cost[next]+std::abs(gx-xx)+std::abs(gy-yy),next});}
 }
 if(found<0)return fail();std::vector<Pose> reverse;for(int n=found;n!=-1;n=parent[n])reverse.push_back(point((n/4)%int(width_),(n/4)/int(width_),n%4));std::reverse(reverse.begin(),reverse.end());std::vector<Pose> route{initial_turn};route.insert(route.end(),reverse.begin(),reverse.end());route.push_back({r.goal,route.back().yaw});return {Status::Ok,std::move(route)};
}
}
