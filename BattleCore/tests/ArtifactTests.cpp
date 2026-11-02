#include "doctest.h"
#include "SvgExport.h"
#include "battle/Control.h"
#include "TestSupport.h"
using namespace battle;
TEST_CASE("artifacts.actual_snapshot_and_xml_escaping"){
 auto s=test::Duel();s.obstacles={{0,2,1,3}};s.units[0].name="pike<&\"";auto w=CreateWorld(s,42).value;AcquireControl(w,{1});auto hash=StateHash(w);
 auto svg=io::ExportSvg(Capture(w),s);CHECK(svg.find("id=\"unit-1\"")!=std::string::npos);CHECK(svg.find("id=\"regiment-1\"")!=std::string::npos);CHECK(svg.find("id=\"obstacle-0\"")!=std::string::npos);CHECK(svg.find("pike&lt;&amp;&quot;")!=std::string::npos);CHECK(StateHash(w)==hash);
}
TEST_CASE("artifacts.empty_world"){
 auto w=CreateWorld(Scenario{},42).value;auto svg=io::ExportSvg(Capture(w),w.scenario);CHECK(svg.find("<svg")!=std::string::npos);CHECK(svg.find("</svg>")!=std::string::npos);CHECK(svg.find("nan")==std::string::npos);
}
