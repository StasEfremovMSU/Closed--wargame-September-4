#include "SvgExport.h"
#include "battle/Formation.h"
#include <iomanip>
#include <locale>
#include <sstream>
namespace battle::io {
namespace {
std::string Xml(const std::string &s) {
    std::string out;
    for (char c : s) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '\'':
            out += "&apos;";
            break;
        default:
            out += c;
        }
    }
    return out;
}
const char *Colour(int team) {
    static constexpr const char *colors[]{"#2767b1", "#c34b45", "#8c55b3", "#be8c2c"};
    return colors[uint32_t(team) % 4];
}
} // namespace
std::string ExportSvg(const Snapshot &snapshot, const Scenario &scenario) {
    auto checked = Restore(snapshot);
    if (!checked)
        return "<svg xmlns=\"http://www.w3.org/2000/svg\"><text x=\"10\" y=\"30\">Invalid "
               "snapshot</text></svg>";
    const auto &w = checked.value;
    auto stats = GetStats(w);
    float minx = 0, miny = 0, maxx = 0, maxy = 0;
    bool first = true;
    auto bounds = [&](Vec3 p) {
        if (first) {
            minx = maxx = p.x;
            miny = maxy = p.y;
            first = false;
        } else {
            minx = std::min(minx, p.x);
            maxx = std::max(maxx, p.x);
            miny = std::min(miny, p.y);
            maxy = std::max(maxy, p.y);
        }
    };
    for (auto &u : w.units)
        bounds(u.pose.position);
    for (auto &r : w.regiments) {
        bounds(r.pose.position);
        for (auto p : r.route)
            bounds(p.position);
    }
    if (first)
        for (auto &o : scenario.obstacles) {
            bounds({o.min_x, o.min_y, 0});
            bounds({o.max_x, o.max_y, 0});
        }
    minx -= 5;
    miny -= 5;
    maxx += 5;
    maxy += 5;
    float scale =
        std::min(1100.f / std::max(10.f, maxx - minx), 620.f / std::max(10.f, maxy - miny));
    auto x = [&](float v) { return 50 + (v - minx) * scale; };
    auto y = [&](float v) { return 120 + (maxy - v) * scale; };
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << std::fixed << std::setprecision(3);
    o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1200\" height=\"800\" viewBox=\"0 0 "
         "1200 800\">\n<rect width=\"1200\" height=\"800\" fill=\"#f4f6f8\"/>\n<g "
         "font-family=\"Segoe UI,Arial,sans-serif\" fill=\"#243242\"><text x=\"40\" y=\"44\" "
         "font-size=\"27\" font-weight=\"600\">BattleCore | recorded battlefield</text>\n<text "
         "x=\"40\" y=\"76\" font-size=\"16\">Tick "
      << w.tick << " | " << w.tick * .05 << " s | Living " << stats.living_units << " | Fallen "
      << stats.dead_units << " | Seed " << w.seed
      << "</text>\n<text x=\"40\" y=\"101\" font-size=\"13\">Blue / red: sides    Green ring: "
         "player control    Grey: fallen    Dashed: route    Arrows: facing</text>\n";
    o << "<defs><clipPath id=\"field\"><rect x=\"40\" y=\"110\" width=\"1120\" "
         "height=\"640\"/></clipPath></defs><g clip-path=\"url(#field)\">";
    for (size_t i = 0; i < scenario.obstacles.size(); ++i) {
        auto &b = scenario.obstacles[i];
        o << "<rect id=\"obstacle-" << i << "\" x=\"" << x(b.min_x) << "\" y=\"" << y(b.max_y)
          << "\" width=\"" << (b.max_x - b.min_x) * scale << "\" height=\""
          << (b.max_y - b.min_y) * scale << "\" fill=\"#657180\"/>\n";
    }
    for (auto &r : w.regiments) {
        auto fp = Footprint(r);
        const char *color = Colour(r.team);
        o << "<g id=\"regiment-" << r.id.value << "\"><rect x=\"" << -fp.depth_m * scale / 2
          << "\" y=\"" << -fp.width_m * scale / 2 << "\" width=\"" << fp.depth_m * scale
          << "\" height=\"" << fp.width_m * scale << "\" fill=\"none\" stroke=\"" << color
          << "\" stroke-width=\"1.5\" transform=\"translate(" << x(r.pose.position.x) << " "
          << y(r.pose.position.y) << ") rotate(" << -r.pose.yaw * 180 / Pi << ")\"/>\n";
        o << "<polyline fill=\"none\" stroke=\"" << color
          << "\" stroke-width=\"1\" stroke-dasharray=\"5 5\" points=\"" << x(r.pose.position.x)
          << "," << y(r.pose.position.y) << " ";
        for (size_t k = r.waypoint; k < r.route.size(); ++k)
            o << x(r.route[k].position.x) << "," << y(r.route[k].position.y) << " ";
        o << "\"/>\n";
        auto forward = Forward(r.pose.yaw);
        o << "<line x1=\"" << x(r.pose.position.x) << "\" y1=\"" << y(r.pose.position.y)
          << "\" x2=\"" << x(r.pose.position.x) + forward.x * 22 << "\" y2=\""
          << y(r.pose.position.y) - forward.y * 22 << "\" stroke=\"" << color
          << "\" stroke-width=\"3\"/>\n<text x=\"" << x(r.pose.position.x) + 6 << "\" y=\""
          << y(r.pose.position.y) - 9 << "\" font-size=\"12\" fill=\"" << color << "\">R"
          << r.id.value << " · " << r.units.size() << (r.panic ? " PANIC" : "") << "</text></g>\n";
    }
    for (auto &u : w.units) {
        o << "<g id=\"unit-" << u.id.value << "\"><title>" << Xml(u.name) << " | ID " << u.id.value
          << " | HP " << u.health << "</title>";
        if (u.alive)
            o << "<circle cx=\"" << x(u.pose.position.x) << "\" cy=\"" << y(u.pose.position.y)
              << "\" r=\"" << std::clamp(scale * .25f, 1.8f, 4.0f) << "\" fill=\""
              << Colour(UnitTeam(w, u)) << "\"/>";
        else {
            o << "<path d=\"M " << x(u.pose.position.x) - 2 << " " << y(u.pose.position.y) - 2
              << " l 4 4 m -4 0 l 4 -4\" stroke=\"#a7adb4\"/>";
        }
        if (u.control == ControlMode::PlayerControlled)
            o << "<circle cx=\"" << x(u.pose.position.x) << "\" cy=\"" << y(u.pose.position.y)
              << "\" r=\"7\" fill=\"none\" stroke=\"#1f9c65\" stroke-width=\"2.5\"/>";
        o << "</g>\n";
    }
    o << "</g><text x=\"40\" y=\"775\" font-size=\"13\">Coordinates: metres | X right, Y up | "
      << 1 / scale << " metres / pixel | Geometry from simulation snapshot</text></g></svg>\n";
    return o.str();
}
} // namespace battle::io
