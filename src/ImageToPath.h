#pragma once
//
//  ImageToPath.h
//  Image-to-toolpath engine for pen plotters.
//  Converts a raster image into vector paths (polylines) using
//  various Path Finding Modules (PFMs) inspired by DrawingBotV3.
//  Supports multi-layer output with per-layer brush/color.
//

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ofMain.h"
#include "PenSettings.h"
#include <vector>
#include <string>
#include <functional>
#include <algorithm>

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

enum class PFMType {
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
// PFM-specific settings  (must be before PlotterLayer)
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
// Plotter Layer
// =============================================================

struct PlotterLayer {
    std::string name      = "Layer 1";
    bool visible          = true;
    bool locked           = false;
    int  brushIndex       = 0;
    ofColor color         = ofColor(0, 0, 0);

    PFMType pfmType       = PFMType::SketchLines;
    SketchLinesSettings sketchLines;
    CrossHatchSettings  crossHatch;
    SpiralSettings      spiral;
    StipplingSettings   stippling;
    ContourSettings     contours;

    std::vector<ofPolyline> paths;
    int   totalPaths    = 0;
    int   totalPoints   = 0;
    float totalDistance  = 0.0f;
    float estimatedTime = 0.0f;
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
// ImageToPath - Main engine
// =============================================================

class ImageToPath {
public:
    ImageToPath();

    bool loadImage(const std::string& path);
    void setImage(const ofPixels& pixels);
    bool hasImage() const { return m_sourceLoaded; }

    // Load an SVG and push its outlines into the active layer as mm-space
    // polylines, fitted to the current paperSize / marginMM. Unlike loadImage
    // there is no raster source and no PFM step -- the vector data IS the
    // plot, so after this call the user can go straight to Export / Print.
    // The SVG's own viewbox y-axis (down) is preserved so the preview matches
    // what designers see in Illustrator / Inkscape; the plotter viewer's
    // "Y+ up" toggle can still flip it for GRBL convention.
    bool loadVectorSVG(const std::string& path);

    const ofImage& getSourceImage() const { return m_sourceImage; }
    const ofImage& getWorkingImage() const { return m_workingImage; }

    /// Upload the processed pixel buffer to the working image GPU texture.
    /// Call this from the main thread after generate() completes if you want
    /// to display getWorkingImage() in a UI. generate() itself no longer does
    /// GPU uploads so it is safe to call from a background thread.
    void uploadWorkingImageTexture() {
        if (m_workW > 0 && m_workH > 0) {
            m_workingImage.setFromPixels(m_workingPixels);
        }
    }

    void generate(std::function<void(float, const std::string&)> onProgress = nullptr);
    void generateLayer(int layerIdx, std::function<void(float, const std::string&)> onProgress = nullptr);

    const std::vector<ofPolyline>& getPaths() const { return m_flatPaths; }
    void rebuildFlatPaths();

    glm::vec2 getPaperSizeMM() const;
    ofColor getPathColor(int flatIdx) const;

    // ---- Settings (public for UI binding) ----
    PaperSize        paperSize        = PaperSize::A3;
    PaperOrientation paperOrientation = PaperOrientation::Landscape;
    float            customWidth      = 420.0f;
    float            customHeight     = 297.0f;
    float            marginMM         = 10.0f;

    PenSettings        pen;
    PreprocessSettings preprocess;

    // ---- Layers ----
    std::vector<PlotterLayer> layers;
    int currentLayer = 0;

    PlotterLayer& getActiveLayer();
    const PlotterLayer& getActiveLayer() const;
    int addLayer(const std::string& name = "");
    void removeLayer(int idx);

    // ---- Brush library ----
    std::vector<BrushPreset> brushes;
    int addBrush(const BrushPreset& b);

    // ---- Aggregate stats (across all layers) ----
    int   totalPaths     = 0;
    int   totalPoints    = 0;
    float totalDistance   = 0.0f;
    float estimatedTime  = 0.0f;

private:
    void preprocessImage();
    void runLayerGeneration(PlotterLayer& layer,
        std::function<void(float, const std::string&)> onProgress);
    void runSketchLines(std::function<void(float, const std::string&)> onProgress);
    void runCrossHatch(std::function<void(float, const std::string&)> onProgress);
    void runSpiral(std::function<void(float, const std::string&)> onProgress);
    void runStippling(std::function<void(float, const std::string&)> onProgress);
    void runContours(std::function<void(float, const std::string&)> onProgress);
    void computeStats();

    glm::vec2 pixelToMM(float px, float py) const;
    float sampleBrightness(float mmX, float mmY) const;

    ofImage  m_sourceImage;
    ofImage  m_workingImage;
    ofPixels m_workingPixels;
    bool m_sourceLoaded = false;

    // Scratch buffer used by PFM methods during generation
    std::vector<ofPolyline> m_paths;

    // Flat path list across all visible layers
    std::vector<ofPolyline> m_flatPaths;
    std::vector<int>        m_flatPathLayerIdx;

    // Working image dimensions mapped to drawing area
    float m_drawWidth   = 0;
    float m_drawHeight  = 0;
    float m_drawOffsetX = 0;
    float m_drawOffsetY = 0;
    int m_workW = 0;
    int m_workH = 0;
};
