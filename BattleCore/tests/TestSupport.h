#pragma once
#include "battle/Simulation.h"
#include <filesystem>
#include <fstream>
#include <atomic>
#include <chrono>
namespace test {
struct TempDir{std::filesystem::path path;TempDir(){static std::atomic<unsigned> n{};path=std::filesystem::temp_directory_path()/("battle-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(n++));std::filesystem::create_directories(path);}~TempDir(){std::error_code e;std::filesystem::remove_all(path,e);}};
inline void Write(const std::filesystem::path&p,const std::string&s){std::ofstream f(p);f<<s;}
inline battle::Scenario Duel(unsigned count=1){using namespace battle;Scenario s;for(unsigned side=0;side<2;++side){RegimentSpec r;r.id={side+1};r.team=int(side);r.pose={{side?10.f:-10.f,0,0},side?Pi:0};r.formation.columns=count;r.rally_point={side?100.f:-100.f,0,0};s.regiments.push_back(r);for(unsigned i=0;i<count;++i){UnitSpec u;u.id={side*count+i+1};u.regiment=r.id;u.pose=r.pose;u.name="soldier";s.units.push_back(u);}}return s;}
}
