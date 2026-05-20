#include "PlotterGCodeInjector.h"

#include "ofxGCode.hpp"

#include <cmath>
#include <fstream>
#include <sstream>

namespace plotter {

namespace {

float segmentLength(const GLine& ln) {
    return (ln.b - ln.a).length();
}

std::vector<std::string> splitGCodeLines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
            line.pop_back();
        if (line.empty()) continue;
        const auto hash = line.find(';');
        if (hash != std::string::npos) line = line.substr(0, hash);
        while (!line.empty() && line.back() == ' ') line.pop_back();
        if (line.empty()) continue;
        out.push_back(line);
    }
    return out;
}

const ZonePosition* pickPosition(const MaintenanceZone& zone, int index) {
    if (zone.positions.empty()) {
        static ZonePosition center;
        center.x = zone.x + zone.w * 0.5f;
        center.y = zone.y + zone.h * 0.5f;
        center.positionIndex = 0;
        return &center;
    }
    for (const auto& p : zone.positions) {
        if (p.positionIndex == index) return &p;
    }
    return &zone.positions.front();
}

std::vector<std::string> buildDetourBlock(const glm::vec2& fromXY,
                                          const glm::vec2& zoneXY,
                                          const glm::vec2& backXY,
                                          const std::vector<std::string>& snippetLines,
                                          const PenSettings& pen,
                                          const grbl::Envelope& envelope)
{
    using std::to_string;
    const float safeZ   = pen.penUpZ;
    const float downZ   = pen.penDownZ;
    const float plungeF = pen.drawSpeed * 0.5f;
    const bool  slow    = pen.slowTravels;
    const std::string travelCmd = slow ? "G1" : "G0";
    const std::string travelFStr = slow ? (" F" + to_string((int)pen.travelSpeed)) : "";

    float zx = fromXY.x, zy = fromXY.y, zz = safeZ;
    envelope.clampXYZ(zx, zy, zz);
    float tx = zoneXY.x, ty = zoneXY.y;
    envelope.clampXYZ(tx, ty, zz);
    float bx = backXY.x, by = backXY.y;
    envelope.clampXYZ(bx, by, zz);

    std::vector<std::string> cmds;
    cmds.push_back("G0 Z" + ofToString(safeZ, 3) + " ; injection pen up");
    cmds.push_back(travelCmd + " X" + ofToString(zx, 3) + " Y" + ofToString(zy, 3) + travelFStr);
    cmds.push_back(travelCmd + " X" + ofToString(tx, 3) + " Y" + ofToString(ty, 3) + travelFStr
                   + " ; to maintenance zone");
    for (const auto& s : snippetLines)
        cmds.push_back(s);
    cmds.push_back("G0 Z" + ofToString(safeZ, 3) + " ; injection pen up");
    cmds.push_back(travelCmd + " X" + ofToString(bx, 3) + " Y" + ofToString(by, 3) + travelFStr
                   + " ; return from maintenance");
    cmds.push_back("G1 Z" + ofToString(downZ, 3) + " F" + to_string((int)plungeF) + " ; pen down");
    return cmds;
}

} // namespace

std::vector<InjectionBreak> computeInjectionBreaks(
    const std::vector<GLine>& lines,
    const std::vector<InjectionRule>& rules,
    bool defaultCountTravel)
{
    std::vector<InjectionBreak> breaks;
    if (lines.empty() || rules.empty()) return breaks;

    size_t ruleIdx = 0;
    for (size_t ri = 0; ri < rules.size(); ++ri) {
        if (rules[ri].enabled) {
            ruleIdx = ri;
            break;
        }
    }
    const InjectionRule& rule = rules[ruleIdx];
    if (!rule.enabled || rule.intervalMm <= 0.f) return breaks;

    const bool countTravel = rule.countTravel || defaultCountTravel;
    float accumulated = 0.f;
    glm::vec2 lastTravel(lines[0].a.x, lines[0].a.y);

    for (size_t i = 0; i < lines.size(); ++i) {
        const GLine& ln = lines[i];
        const float segLen = segmentLength(ln);
        accumulated += segLen;

        if (countTravel) {
            accumulated += (glm::vec2(ln.a.x, ln.a.y) - lastTravel).length();
        }
        lastTravel = glm::vec2(ln.b.x, ln.b.y);

        if (accumulated >= rule.intervalMm) {
            InjectionBreak br;
            br.afterLineIndex = i;
            br.machineXY      = glm::vec2(ln.b.x, ln.b.y);
            br.ruleIndex      = ruleIdx;
            breaks.push_back(br);
            accumulated = 0.f;
        }
    }
    return breaks;
}

std::string loadSnippetText(const std::string& path) {
    if (path.empty()) return {};
    std::ifstream ifs(path);
    if (!ifs) return {};
    return {std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>()};
}

std::vector<std::string> applyInjectionToCommands(
    const std::vector<std::string>& baseCommands,
    const std::vector<GLine>& lines,
    const std::vector<InjectionBreak>& breaks,
    const std::vector<InjectionRule>& rules,
    const PlotterZoneStore& zones,
    const PenSettings& pen,
    const grbl::Envelope& envelope)
{
    if (breaks.empty() || lines.empty()) return baseCommands;

    // Map line index -> commands: baseCommands structure is header (4) + per line ~2-3 cmds.
    // Simpler: rebuild from lines with injection - but we already have baseCommands.
    // Re-build by walking lines and emitting like buildGCodeString, with injection after each line.

    using std::to_string;
    const float safeZ       = pen.penUpZ;
    const float downZ       = pen.penDownZ;
    const float drawF       = pen.drawSpeed;
    const float plungeF     = pen.drawSpeed * 0.5f;
    const bool  slowTravels = pen.slowTravels;
    const float travelF     = pen.travelSpeed;
    const std::string travelCmd = slowTravels ? "G1" : "G0";
    const std::string travelFStr = slowTravels ? (" F" + to_string((int)travelF)) : "";

    std::vector<std::string> commands;
    commands.push_back("G21 ; mm mode");
    commands.push_back("G90 ; absolute positioning");
    commands.push_back("G0 Z" + ofToString(safeZ, 3) + " ; pen up");

    size_t breakPtr = 0;
    ofVec2f lastPos(0, 0);
    bool penIsUp   = true;
    bool firstDraw = true;

    auto injectAt = [&](size_t lineIdx, const glm::vec2& atXY) {
        if (breakPtr >= breaks.size() || breaks[breakPtr].afterLineIndex != lineIdx)
            return;
        const InjectionBreak& br = breaks[breakPtr++];
        if (br.ruleIndex >= rules.size()) return;
        const InjectionRule& rule = rules[br.ruleIndex];
        if (!rule.enabled) return;

        const MaintenanceZone* zone = zones.findZone(rule.zoneId);
        if (!zone) return;

        std::string snippetPath = rule.snippetPath.empty() ? zone->snippetPath : rule.snippetPath;
        const auto snippetLines = splitGCodeLines(loadSnippetText(snippetPath));
        if (snippetLines.empty()) return;

        const ZonePosition* pos = pickPosition(*zone, rule.positionIndex);
        const glm::vec2 zoneXY(pos->x, pos->y);

        if (rule.mode == InjectionMode::Inline) {
            for (const auto& s : snippetLines)
                commands.push_back(s);
            return;
        }

        auto detour = buildDetourBlock(atXY, zoneXY, atXY, snippetLines, pen, envelope);
        commands.insert(commands.end(), detour.begin(), detour.end());
        penIsUp   = false;
        firstDraw = true;
    };

    for (size_t i = 0; i < lines.size(); ++i) {
        const auto& line = lines[i];
        if (line.a != lastPos || penIsUp) {
            if (!penIsUp) {
                commands.push_back("G0 Z" + ofToString(safeZ, 3) + " ; pen up");
                penIsUp = true;
            }
            commands.push_back(travelCmd
                + " X" + ofToString(line.a.x, 3)
                + " Y" + ofToString(line.a.y, 3) + travelFStr);
            commands.push_back("G1 Z" + ofToString(downZ, 3) + " F" + to_string((int)plungeF) + " ; pen down");
            penIsUp   = false;
            firstDraw = true;
        }

        std::string move = "G1 X" + ofToString(line.b.x, 3) + " Y" + ofToString(line.b.y, 3);
        if (firstDraw) {
            move += " F" + to_string((int)drawF);
            firstDraw = false;
        }
        commands.push_back(move);
        lastPos = line.b;

        injectAt(i, glm::vec2(line.b.x, line.b.y));
    }

    commands.push_back("G0 Z" + ofToString(safeZ, 3) + " ; pen up");
    float hx = lines.empty() ? 0.f : lines.front().a.x;
    float hy = lines.empty() ? 0.f : lines.front().a.y;
    float hz = safeZ;
    envelope.clampXYZ(hx, hy, hz);
    commands.push_back(travelCmd + " X" + ofToString(hx, 3) + " Y" + ofToString(hy, 3) + travelFStr
                      + " ; park");
    commands.push_back("M2 ; end program");

    return commands;
}

std::vector<glm::vec2> injectionMarkersContent(
    const std::vector<InjectionBreak>& breaks,
    const BedView& bed)
{
    std::vector<glm::vec2> out;
    out.reserve(breaks.size());
    for (const auto& br : breaks)
        out.push_back(bed.machineToContent(br.machineXY.x, br.machineXY.y));
    return out;
}

} // namespace plotter
