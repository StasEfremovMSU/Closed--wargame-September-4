#include "LegacyImport.h"
#include "ScenarioJson.h"
#include "JsonBindings.h"
#include "battle/Simulation.h"
#include <fstream>
#include <iostream>
#include <map>
using namespace battle;
int main(int argc,char**argv){try{
 if(argc<2){std::cout<<"battle_cli import-legacy|run; use --scenario FILE --ticks N --seed N --out DIR\n";return 2;}
 std::string mode=argv[1];std::map<std::string,std::string> args;for(int i=2;i<argc;i+=2){if(i+1>=argc)throw std::invalid_argument("option requires value");args[argv[i]]=argv[i+1];}
 auto required=[&](std::string key){if(!args.contains(key))throw std::invalid_argument("missing "+key);return args.at(key);};
 if(mode=="import-legacy"){auto r=io::ImportLegacy(required("--source"),{required("--map"),1});for(auto&d:r.diagnostics)std::cerr<<d.source<<":"<<d.line<<": "<<d.message<<"\n";if(!r)return 2;auto saved=io::SaveScenario(required("--out"),r.value);if(!saved)return 2;auto stats=GetStats(CreateWorld(r.value,42).value);std::cout<<"Imported "<<stats.regiment_count<<" regiments, "<<stats.living_units<<" units\n";return 0;}
 if(mode!="run")throw std::invalid_argument("unknown mode");
 auto s=io::LoadScenario(required("--scenario"));if(!s){for(auto&d:s.diagnostics)std::cerr<<d.message<<"\n";return 2;}
 auto w=CreateWorld(s.value,args.contains("--seed")?std::stoull(args["--seed"]):42).value;FlatNavigationService flat;GridNavigationService grid(s.value.obstacles);const NavigationService&nav=s.value.obstacles.empty()?static_cast<const NavigationService&>(flat):static_cast<const NavigationService&>(grid);Tick ticks=args.contains("--ticks")?std::stoull(args["--ticks"]):100;
 std::filesystem::path out=required("--out");std::filesystem::create_directories(out);std::ofstream events(out/"events.jsonl");
 std::vector<CommandEnvelope> commands;if(args.contains("--commands")){std::ifstream f(args["--commands"]);if(!f)throw std::runtime_error("cannot open commands");nlohmann::json j;f>>j;commands=j.get<std::vector<CommandEnvelope>>();}
 for(Tick i=0;i<ticks;++i){auto r=Step(w,i==0?std::span<const CommandEnvelope>(commands):std::span<const CommandEnvelope>{}, {},nav);if(!r)return 2;for(auto&e:r.value.events)events<<nlohmann::json(e).dump()<<"\n";}
 std::ofstream snapshot(out/"snapshot.json");snapshot<<nlohmann::json(Capture(w)).dump(2);auto stats=GetStats(w);std::cout<<"tick="<<w.tick<<" living="<<stats.living_units<<" dead="<<stats.dead_units<<" hash="<<StateHash(w)<<"\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 2;}}
