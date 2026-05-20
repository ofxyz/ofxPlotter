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
#include "PlotterBedCoords.h"
#include "PlotterExporter.h"
#include "PlotterStrokeBridge.h"
#include <ofxPlotFinders/src/ofxPlotFinders.h>
#include "PlotterZones.h"
#include "PlotterGCodeInjector.h"
