#pragma once
//
//  PlotterComponents.h
//  Domain types and ECS components for ofxPlotter.
//
//  Enums, finder-settings structs, BrushPreset, and PreprocessSettings are all
//  defined here so that both ImageToPath (the engine) and the ECS components
//  (plotter:: namespace) can include this single header without circular deps.
//

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ofMain.h"
#include <vector>
#include <string>

// =============================================================
// Enums
// =============================================================

enum class PaperSize {
    A4,     // 297 x 210 mm
    A3,     // 420 x 297 mm
    A2,     // 594 x 420 mm
    A1,     // 841 x 594 mm
    A0,     // 1189 x 841 mm
    Custom
};

enum class PaperOrientation {
    Portrait,
    Landscape
};

/// Image-to-stroke algorithm selection (plot finder).
enum class PlotFinderType {
    SketchLines,
    CrossHatch,
    Spiral,
    Stippling,
    Contours
};

enum class BrushShape {
    Round,
    Square,
    Flat,       // Calligraphy-style flat nib
    Nib,        // Italic nib at angle
    Custom      // User-drawn outline
};

// =============================================================
// Plot-finder-specific settings
// =============================================================

struct SketchLinesSettings {
    float lineMinLength  = 5.0f;
    float lineMaxLength  = 80.0f;
    int   angleTests     = 18;
    float lineDensity    = 75.0f;
    float eraseMin       = 50.0f;
    float eraseMax       = 125.0f;
    int   squiggleMin    = 3;
    int   squiggleMax    = 200;
    bool  shouldLiftPen  = true;
    float plotResolution = 0.5f;
};

struct CrossHatchSettings {
    float angle1        = 45.0f;
    float angle2        = 135.0f;
    bool  useSecondary  = true;
    float lineSpacing   = 2.0f;
    float minBrightness = 0.0f;
};

struct SpiralSettings {
    float ringSpacing = 3.0f;
    float amplitude   = 1.0f;
    float velocity    = 5.0f;
    float centreX     = 50.0f;
    float centreY     = 50.0f;
    float spiralSize  = 100.0f;
    bool  ignoreWhite = true;
};

struct StipplingSettings {
    float dotSpacingMin = 0.5f;
    float dotSpacingMax = 5.0f;
    float dotRadius     = 0.2f;
    int   iterations    = 50;
};

struct ContourSettings {
    float cannyLow      = 50.0f;
    float cannyHigh     = 150.0f;
    float minContourLen = 5.0f;
};

// =============================================================
// Brush preset (pen tip definition)
// =============================================================

struct BrushPreset {
    std::string name     = "0.3mm Round";
    BrushShape  shape    = BrushShape::Round;
    float sizeMM         = 0.3f;
    float angle          = 0.0f;
    float flatRatio      = 0.3f;
    float softness       = 0.0f;
    ofColor color        = ofColor(0, 0, 0);
    std::vector<glm::vec2> customOutline;
};

// =============================================================
// Image preprocessing settings
// =============================================================

struct PreprocessSettings {
    float brightness = 0.0f;
    float contrast   = 0.0f;
    float threshold  = -1.0f;
    bool  invert     = false;
    float blur       = 0.0f;
    bool  edgeDetect = false;
};

// =============================================================
// ECS components  (plotter:: namespace)
// =============================================================

namespace plotter {

/// Toolpath geometry stored on a layer entity.
/// Plot finders write to a scratch ofPolyline buffer during generation; the engine
/// converts and moves the result here once per layer run.
struct paths_component {
    std::vector<ofPath>  paths;       ///< One ofPath per stroke; use getOutline() for polyline access
    std::vector<ofColor> pathColors;  ///< Parallel to paths — per-path colour override.
                                      ///< Empty = use layer colour for all paths.
};

/// Cached statistics for a layer's toolpaths (recomputed after each generation
/// or SVG import).  Kept separate from paths_component so stats can be read
/// cheaply without touching path geometry.
struct toolpath_stats_component {
    int   totalPaths    = 0;
    int   totalPoints   = 0;
    float totalDistance = 0.0f;
    float estimatedTime = 0.0f;
};

/// Per-layer plot finder selection and all algorithm settings.
/// The active finder type determines which settings sub-struct is used at
/// generation time; the others are preserved so users can switch finders without
/// losing their tuning.
struct settings_component {
    int             brushIndex      = 0;
    PlotFinderType  plotFinderType  = PlotFinderType::SketchLines;

    SketchLinesSettings sketchLines;
    CrossHatchSettings  crossHatch;
    SpiralSettings      spiral;
    StipplingSettings   stippling;
    ContourSettings     contours;
};

/// Per-layer SVG fill rasterization cache.
/// When `enabled`, the closed outline paths on this layer are rendered into a
/// greyscale bitmap (using each path's colour luminance) before generation.
/// The plot finder then uses that bitmap as its source, simulating fill weight with
/// pen strokes instead of flat colour.  Must be populated on the main/GL
/// thread via ImageToPath::rasterizeLayerFills() before the generate thread
/// is spawned.
struct fill_raster_component {
    bool     enabled = false;  ///< opt-in per layer
    ofPixels pixels;           ///< greyscale raster, set by rasterizeLayerFills()
    int      rasterW = 0;
    int      rasterH = 0;
    float    drawX   = 0.f;   ///< mm-space draw area matching the raster
    float    drawY   = 0.f;
    float    drawW   = 0.f;
    float    drawH   = 0.f;
};

} // namespace plotter
