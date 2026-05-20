#pragma once

#include "PenSettings.h"
#include "PlotterBedCoords.h"
#include "PlotterZones.h"
#include <MachinePrefs.h>
#include <glm/vec2.hpp>
#include <string>
#include <vector>

class GLine;
class ofxGCode;

namespace plotter {

/// Break point along the toolpath (after g.lines index).
struct InjectionBreak {
    size_t      afterLineIndex = 0;
    glm::vec2   machineXY;
    size_t      ruleIndex = 0;
};

std::vector<InjectionBreak> computeInjectionBreaks(
    const std::vector<GLine>& lines,
    const std::vector<InjectionRule>& rules,
    bool defaultCountTravel);

std::string loadSnippetText(const std::string& path);

std::vector<std::string> applyInjectionToCommands(
    const std::vector<std::string>& baseCommands,
    const std::vector<GLine>& lines,
    const std::vector<InjectionBreak>& breaks,
    const std::vector<InjectionRule>& rules,
    const PlotterZoneStore& zones,
    const PenSettings& pen,
    const grbl::Envelope& envelope);

/// Machine-space injection preview markers (content coords for viewport).
std::vector<glm::vec2> injectionMarkersContent(
    const std::vector<InjectionBreak>& breaks,
    const BedView& bed);

} // namespace plotter
