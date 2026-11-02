#include "battle/Formation.h"
#include <numeric>
namespace battle {
namespace{float Angle(float x){return std::remainder(x,2*Pi);}Vec3 Towards(Vec3 a,Vec3 b,float distance){auto d=b-a;float n=Length(d);return n<=distance?b:a+d*(distance/n);}}
std::vector<Pose> BuildFormationSlots(const FormationSpec&s,Pose centre,uint32_t count){std::vector<Pose> out;if(!s.columns||!count)return out;out.reserve(count);uint32_t rows=(count+s.columns-1)/s.columns;for(uint32_t i=0;i<count;++i){float x=(float(rows-1)*.5f-float(i/s.columns))*s.spacing_m;float y=(float(i%s.columns)-float(s.columns-1)*.5f)*s.spacing_m;out.push_back({centre.position+Forward(centre.yaw)*x+Right(centre.yaw)*y,centre.yaw});}return out;}
FormationFootprint Footprint(const RegimentState&r){size_t count=r.slots.empty()?r.units.size():r.slots.size();if(!count)return {};size_t rows=(count+r.formation.columns-1)/r.formation.columns;return {(r.formation.columns-1)*r.formation.spacing_m+2*r.formation.body_radius_m,float(rows-1)*r.formation.spacing_m+2*r.formation.body_radius_m};}
bool RectanglesOverlap(Pose a,FormationFootprint af,Pose b,FormationFootprint bf){if(af.width_m<=0||af.depth_m<=0||bf.width_m<=0||bf.depth_m<=0)return false;auto ax=Forward(a.yaw),ay=Right(a.yaw),bx=Forward(b.yaw),by=Right(b.yaw);for(auto axis:{ax,ay,bx,by}){float ra=std::abs(Dot(axis,ax))*af.depth_m*.5f+std::abs(Dot(axis,ay))*af.width_m*.5f;float rb=std::abs(Dot(axis,bx))*bf.depth_m*.5f+std::abs(Dot(axis,by))*bf.width_m*.5f;if(std::abs(Dot(b.position-a.position,axis))>=ra+rb-0.00001f)return false;}return true;}
float SweepLimit(Pose a,Pose end,FormationFootprint af,Pose b,FormationFootprint bf){
 if(af.width_m<=0||af.depth_m<=0||bf.width_m<=0||bf.depth_m<=0)return 1;
 auto ax=Forward(a.yaw),ay=Right(a.yaw),bx=Forward(b.yaw),by=Right(b.yaw);bool rotating=std::abs(Angle(end.yaw-a.yaw))>0.00001f;float enter=-1e30f,leave=1e30f;
 for(auto axis:{ax,ay,bx,by}){float ra=rotating?std::hypot(af.width_m,af.depth_m)*.5f:std::abs(Dot(axis,ax))*af.depth_m*.5f+std::abs(Dot(axis,ay))*af.width_m*.5f;float rb=std::abs(Dot(axis,bx))*bf.depth_m*.5f+std::abs(Dot(axis,by))*bf.width_m*.5f;float radius=ra+rb;float d=Dot(b.position-a.position,axis),v=Dot(end.position-a.position,axis);if(std::abs(v)<1e-9f){if(std::abs(d)>=radius-.00001f)return 1;continue;}float x=(d-radius)/v,y=(d+radius)/v;if(x>y)std::swap(x,y);enter=std::max(enter,x);leave=std::min(leave,y);if(enter>=leave)return 1;}
 if(leave<=.00001f||enter>=1)return 1;return std::clamp(enter-.00001f,0.f,1.f);
}
void InitializeFormations(World&w){for(auto&r:w.regiments){r.slots=BuildFormationSlots(r.formation,r.pose,uint32_t(r.units.size()));for(size_t i=0;i<r.units.size();++i){auto*u=FindUnit(w,r.units[i]);u->slot_index=uint32_t(i);u->slot=r.slots[i];if(u->pose==r.pose)u->pose=u->slot;}}}
Status ReformRegiment(World&w,RegimentId id,FormationSpec s){if(!s.columns||!std::isfinite(s.spacing_m)||s.spacing_m<=0||!std::isfinite(s.body_radius_m)||s.body_radius_m<=0)return Status::InvalidInput;auto*r=FindRegiment(w,id);if(!r)return Status::NotFound;r->formation=s;std::erase_if(r->units,[&](auto id){auto*u=FindUnit(w,id);return !u||!u->alive;});r->slots=BuildFormationSlots(s,r->pose,uint32_t(r->units.size()));for(size_t i=0;i<r->units.size();++i){auto*u=FindUnit(w,r->units[i]);u->slot_index=uint32_t(i);u->slot=r->slots[i];}return Status::Ok;}
void AdvanceFormations(World&w,float dt){
 std::vector<size_t> order(w.regiments.size());std::iota(order.begin(),order.end(),0);std::sort(order.begin(),order.end(),[&](size_t a,size_t b){auto&x=w.regiments[a];auto&y=w.regiments[b];int xp=x.order?x.order->priority:0,yp=y.order?y.order->priority:0;return xp!=yp?xp>yp:x.id<y.id;});
 for(size_t index:order){auto&r=w.regiments[index];if(r.units.empty())continue;Pose next=r.pose;if(r.waypoint<r.route.size()){
   const auto target=r.route[r.waypoint];float turn=std::clamp(Angle(target.yaw-r.pose.yaw),-r.turn_rate*dt,r.turn_rate*dt);next.yaw=Angle(r.pose.yaw+turn);float speed=r.speed;for(auto id:r.units)if(auto*u=FindUnit(w,id);u&&u->alive&&u->control!=ControlMode::PlayerControlled)speed=std::min(speed,u->speed);if(std::abs(Angle(target.yaw-next.yaw))<.01f)next.position=Towards(r.pose.position,target.position,speed*dt);
   float fraction=1;for(size_t j=0;j<w.regiments.size();++j)if(j!=index&&!w.regiments[j].units.empty())fraction=std::min(fraction,SweepLimit(r.pose,next,Footprint(r),w.regiments[j].pose,Footprint(w.regiments[j])));
   for(const auto&box:w.scenario.obstacles){Pose p{{(box.min_x+box.max_x)*.5f,(box.min_y+box.max_y)*.5f,r.pose.position.z},0};FormationFootprint fp{box.max_y-box.min_y,box.max_x-box.min_x};fraction=std::min(fraction,SweepLimit(r.pose,next,Footprint(r),p,fp));}
   next={r.pose.position+(next.position-r.pose.position)*fraction,r.pose.yaw+Angle(next.yaw-r.pose.yaw)*fraction};
   r.pose=next;if(Distance(next.position,target.position)<.001f&&std::abs(Angle(next.yaw-target.yaw))<.001f)++r.waypoint;
  }
  r.slots=BuildFormationSlots(r.formation,r.pose,uint32_t(r.slots.size()));
  for(auto id:r.units){auto*u=FindUnit(w,id);if(!u||!u->alive||u->control==ControlMode::PlayerControlled)continue;if(u->slot_index>=r.slots.size())continue;u->slot=r.slots[u->slot_index];u->pose.position=Towards(u->pose.position,u->slot.position,u->speed*dt);u->pose.yaw=u->slot.yaw;if(u->control==ControlMode::Returning&&Distance(u->pose.position,u->slot.position)<.01f)u->control=ControlMode::FormationControlled;}
 }
}
}
