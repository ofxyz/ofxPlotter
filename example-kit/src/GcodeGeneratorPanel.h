#pragma once

#include "ofMain.h"
#include "PlotterZones.h"
#include <ofxPlotProcessors/src/ofxPlotProcessors.h>
#include <ofxKit.h>
#include <functional>
#include <vector>

class ImageToPath;

/// Zones list, injection rules, plot pipeline, and G-code regeneration controls.
class GcodeGeneratorPanel {
public:
    void setEngine(ImageToPath* engine) { m_engine = engine; }
    void setZoneStore(plotter::PlotterZoneStore* zones) { m_zones = zones; }
    void setPipeline(plotproc::PlotPipeline* pipeline) { m_pipeline = pipeline; }
    void setSnippetPaths(std::function<std::vector<std::string>()> getPaths) {
        m_getSnippetPaths = std::move(getPaths);
    }
    void setOnRegenerateGcode(std::function<void()> cb) { m_onRegenerate = std::move(cb); }

    bool runPipelineOnExport() const { return m_runPipelineOnExport; }
    bool writeBackToPathsOnExport() const { return m_writeBackToPaths; }
    void setLastPipelineReport(const plotproc::PipelineRunReport& report, bool valid);

    const std::vector<glm::vec2>& injectionMarkersContent() const { return m_injectionMarkers; }
    void refreshInjectionMarkers();

    void draw(const char* title, bool& visible);

    void setSelectedZoneIndex(int idx) { m_selectedZone = idx; }
    int  selectedZoneIndex() const { return m_selectedZone; }
    /// Zone fields for the Properties panel (uses m_selectedZone).
    void drawZoneInspector();

private:
    void drawZonesTab();
    void drawInjectionTab();
    void drawPipelineTab();
    void rebuildInjectionMarkers();

    ImageToPath*              m_engine = nullptr;
    plotter::PlotterZoneStore* m_zones  = nullptr;
    plotproc::PlotPipeline*   m_pipeline = nullptr;
    std::function<std::vector<std::string>()> m_getSnippetPaths;
    std::function<void()>     m_onRegenerate;

    int                       m_tab = 0;
    int                       m_selectedZone = -1;
    std::vector<glm::vec2>    m_injectionMarkers;

    bool                      m_runPipelineOnExport = true;
    bool                      m_writeBackToPaths = true;
    bool                      m_hasPipelineReport = false;
    plotproc::PipelineRunReport m_lastReport;
};
