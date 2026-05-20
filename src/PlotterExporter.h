#pragma once



#include "ImageToPath.h"

#include "PenSettings.h"

#include "PlotterBedCoords.h"

#include "PlotterGCodeInjector.h"

#include "PlotterZones.h"

#include <ofxPlotProcessors/src/ofxPlotProcessors.h>

#include <MachinePrefs.h>

#include <string>



namespace plotter {



struct ExportOptions {

    const grbl::MachinePrefs* prefs = nullptr;

    const PlotterZoneStore*   zones = nullptr;

    const plotproc::PlotPipeline* pipeline = nullptr;

    bool runPipeline = true;
    /// Write optimized polylines back into paths_component as ofPath (then rebuild flat list).
    bool writeBackToPaths = true;

    /// Optional external command (e.g. vpype script). When set, runs after native pipeline.

    std::string externalCommand;

    /// Filled on last export when runPipeline is true.

    plotproc::PipelineRunReport lastPipelineReport;

    bool hasPipelineReport = false;

};



/// Build a full G-code program for all visible layers in @p engine.

std::string toGCode(ImageToPath& engine, ExportOptions& opts);



/// Build a G-code program for a single layer entity in @p engine.

std::string toGCodeForLayer(ImageToPath& engine,

                            entt::entity layerEntity,

                            ExportOptions& opts);



} // namespace plotter

