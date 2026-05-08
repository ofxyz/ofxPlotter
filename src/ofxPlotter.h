#pragma once

/// ofxPlotter — Pen plotter toolchain for openFrameworks.
///
/// Include this single header to pull in everything:
///
///   #include "ofxPlotter.h"
///
///   ImageToPath engine;
///   engine.loadImage("photo.jpg");
///   engine.generate();
///   std::string gcode = plotter::toGCode(engine);

#include "PenSettings.h"
#include "ImageToPath.h"
#include "PlotterExporter.h"
