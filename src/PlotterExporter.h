#pragma once
//
//  PlotterExporter.h
//  G-code export helpers for ImageToPath.
//
//  These free functions live in ofxPlotter (machine-aware) rather than in
//  ofxGCode (general-purpose vector library) because they need PenSettings:
//  feed rates, pen-down Z, slow-travel mode, etc.
//
//  Usage:
//      std::string gcode = plotter::toGCode(engine);
//      std::string layer = plotter::toGCodeForLayer(engine, 0);
//

#include "ImageToPath.h"
#include "PenSettings.h"
#include <string>

namespace plotter {

/// Build a full G-code program for all visible layers in @p engine.
std::string toGCode(const ImageToPath& engine);

/// Build a G-code program for a single layer (by index) in @p engine.
/// Returns an empty string if @p layerIdx is out of range.
std::string toGCodeForLayer(const ImageToPath& engine, int layerIdx);

} // namespace plotter
