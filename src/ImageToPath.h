#pragma once
//
//  ImageToPath.h
//  Image-to-toolpath engine for pen plotters.
//  Converts a raster image into vector paths (polylines) using
//  plot finders (image-to-stroke algorithms).
//  Supports multi-layer output with per-layer brush/colour.
//
//  Layers are stored as ECS entities in an internal entt::registry.
//  Each layer entity carries:
//    ecs::layer_component          — name, visible, locked, colour, sort index
//    ecs::Relationship             — parent / child hierarchy
//    plotter::settings_component   — plot finder type + algorithm settings
//    plotter::paths_component      — generated ofPath strokes
//    plotter::toolpath_stats_component — cached stats (paths, points, distance, time)
//

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "PlotterComponents.h"
#include "PenSettings.h"
#include <ofxEnTTKit/src/components/layer_components.h>
#include <ofxEnTTKit/src/components/hierarchy_components.h>
#include <entt.hpp>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>

namespace plotfind { class FinderContext; }

// =============================================================
// ImageToPath - Main engine
// =============================================================

class ImageToPath {
    friend class plotfind::FinderContext;
public:
    ImageToPath();

    bool loadImage(const std::string& path);
    void setImage(const ofPixels& pixels);
    bool hasImage() const { return m_sourceLoaded; }

    // ---- SVG import ----
    enum class SvgImportMode {
        GroupsAsLayers,   ///< one ECS layer per top-level SVG <g> (default)
        ColorsAsLayers,   ///< one ECS layer per unique stroke/fill colour
        SingleLayer       ///< all paths in the active layer (legacy)
    };

    /// Load an SVG and distribute its outlines into ECS layers according to
    /// mode, fitted to the current paperSize / marginMM.
    bool loadVectorSVG(const std::string& path,
                       SvgImportMode mode = SvgImportMode::GroupsAsLayers);

    /// Returns true when a colour preview of the last loaded SVG is available.
    bool hasSvgPreview() const { return m_svgPreview.isAllocated() && m_svgPreview.getWidth() > 0; }

    /// Colour preview image rendered from the fitted SVG paths (filled shapes
    /// use their original fill colour; open paths are drawn as lines).
    /// Dimensions match the draw area in the current paper space.
    const ofImage& getSvgPreview() const { return m_svgPreview; }

    /// Re-render the SVG colour preview into m_svgPreview.  Called
    /// automatically by loadVectorSVG; call again if you change paper / margin.
    /// Must be called on the main/GL thread.  pixelsPerMM controls resolution.
    void renderSvgColorPreview(float pixelsPerMM = 3.f);

    const ofImage& getSourceImage() const { return m_sourceImage; }
    const ofImage& getWorkingImage() const { return m_workingImage; }

    void uploadWorkingImageTexture() {
        if (m_workW > 0 && m_workH > 0)
            m_workingImage.setFromPixels(m_workingPixels);
    }

    void generate(std::function<void(float, const std::string&)> onProgress = nullptr);
    void generateLayer(entt::entity layerEntity,
        std::function<void(float, const std::string&)> onProgress = nullptr);

    /// Returns true if at least one layer has fill_raster_component::enabled
    /// and a non-empty raster cache (i.e. rasterizeLayerFills has been called).
    bool hasFillLayers() const;

    /// Render each fill-enabled layer's closed paths into a greyscale bitmap
    /// using each path's colour luminance as fill intensity.  Must be called
    /// on the main/GL thread before starting the generate worker thread.
    /// @param pixelsPerMM  Raster resolution (default 10 px/mm).
    void rasterizeLayerFills(float pixelsPerMM = 10.f);

    const std::vector<ofPolyline>&   getPaths()            const { return m_flatPaths; }
    const std::vector<entt::entity>& getFlatPathEntities() const { return m_flatPathLayerEntity; }
    const std::vector<ofColor>&      getFlatPathColors()   const { return m_flatPathColors; }
    void rebuildFlatPaths();
    /// Recompute aggregate totalPaths / totalDistance / estimatedTime from layers + flat paths.
    void refreshStats() { computeStats(); }

    /// True if the entity and all ancestors are visible (for export / stroke bridge).
    bool isEffectivelyVisible(entt::entity e) const;

    /// Replace flat path cache (e.g. after plot processor pipeline).
    void setFlatPaths(std::vector<ofPolyline> paths,
                      std::vector<entt::entity> layerEntities,
                      std::vector<ofColor> colors,
                      bool recomputeStats = true);

    glm::vec2 getPaperSizeMM() const;
    ofColor   getPathColor(int flatIdx) const;

    /// Rescale all loaded layer paths so they fill the drawable area
    /// (paper minus margins) while preserving aspect ratio.
    void scaleToFit();

    // ---- Post-load path transforms ----
    /// Mirror all paths horizontally around the horizontal centre of the draw area.
    void flipHorizontal();
    /// Mirror all paths vertically around the vertical centre of the draw area.
    void flipVertical();
    /// Rotate all paths 90° clockwise.  Swaps draw-area width/height and
    /// re-centres in the paper so the content stays inside the margins.
    void rotate90CW();
    /// Rotate all paths 90° counter-clockwise.
    void rotate90CCW();

    // ---- Draw-area rect (mm, relative to paper origin) ----
    /// The rectangle within the paper that the image is mapped to.
    /// Paths are generated in this same coordinate space.
    struct DrawAreaMM { float x = 0, y = 0, w = 0, h = 0; };
    DrawAreaMM getDrawAreaMM() const {
        if (m_drawWidth > 0.f && m_drawHeight > 0.f)
            return { m_drawOffsetX, m_drawOffsetY, m_drawWidth, m_drawHeight };
        if (!m_sourceLoaded || m_sourceImage.getWidth() <= 0 || m_sourceImage.getHeight() <= 0)
            return {};
        glm::vec2 paper  = getPaperSizeMM();
        float     availW = paper.x - 2.f * marginMM;
        float     availH = paper.y - 2.f * marginMM;
        if (availW <= 0.f || availH <= 0.f) return {};
        float aspect = (float)m_sourceImage.getWidth() / (float)m_sourceImage.getHeight();
        float dw, dh;
        if (aspect > availW / availH) { dw = availW; dh = availW / aspect; }
        else                          { dh = availH; dw = availH * aspect; }
        return { marginMM + (availW - dw) * 0.5f,
                 marginMM + (availH - dh) * 0.5f,
                 dw, dh };
    }

    // ---- Image overlay transform (mm, relative to paper origin) ----
    /// Initialised from getDrawAreaMM() after every load.
    /// Can be edited live via the 2D transform handle in the preview.
    float imageOverlayX {0}, imageOverlayY {0};
    float imageOverlayW {0}, imageOverlayH {0};

    // ---- Settings (public for UI binding) ----
    PaperSize        paperSize        = PaperSize::A3;
    PaperOrientation paperOrientation = PaperOrientation::Landscape;
    float            customWidth      = 420.0f;
    float            customHeight     = 297.0f;
    float            marginMM         = 10.0f;
    ofColor          canvasColor      {255, 255, 255};  ///< paper/substrate background colour

    PenSettings        pen;
    PreprocessSettings preprocess;

    // ---- ECS layer store ----
    /// Registry owns all layer entities.  External code may read components
    /// directly for rendering or UI; mutating operations should use the
    /// add/remove/generate helpers to keep layerOrder in sync.
    entt::registry            registry;

    /// Ordered list of layer entities (bottom-first, matching render order).
    /// EnTT storage order is undefined, so this vector is the canonical order.
    std::vector<entt::entity> layerOrder;

    /// The layer entity currently selected for editing / generation.
    entt::entity activeLayer = entt::null;

    /// Convenience accessors for the active layer's components.
    plotter::settings_component& getActiveSettings();
    const plotter::settings_component& getActiveSettings() const;
    ecs::layer_component& getActiveLayerComponent();
    const ecs::layer_component& getActiveLayerComponent() const;

    /// Layer CRUD — keeps layerOrder (DFS cache) and Relationship links in sync.
    entt::entity addLayer(const std::string& name = "",
                          entt::entity parent = entt::null);
    void         removeLayer(entt::entity e);

    /// Rewire an entity's position in the hierarchy then rebuild layerOrder.
    /// @param child        Entity to move.
    /// @param newParent    New parent (entt::null = root level).
    /// @param insertBefore Sibling to insert before (entt::null = append last).
    void         reparentLayer(entt::entity child,
                               entt::entity newParent,
                               entt::entity insertBefore);

    // ---- Brush library ----
    std::vector<BrushPreset> brushes;
    int addBrush(const BrushPreset& b);

    // ---- Aggregate stats (across all visible layers) ----
    int   totalPaths    = 0;
    int   totalPoints   = 0;
    float totalDistance = 0.0f;
    float estimatedTime = 0.0f;

private:
    /// Rebuild layerOrder from a depth-first walk of the Relationship tree.
    void rebuildLayerOrder();

    /// Append `child` as the last item in `parent`'s child list (or the root
    /// sibling chain when parent == entt::null). Assumes `child` already has an
    /// ecs::Relationship component and is fully unlinked.
    static void linkChild(entt::registry& reg,
                          entt::entity parent, entt::entity child);

    /// Remove `entity` from its parent's child list (or the root sibling chain),
    /// fixing all prev/next pointers. Does NOT recurse into descendants.
    static void unlinkChild(entt::registry& reg, entt::entity entity);

    /// Apply a 2-D vertex transform to every path on every layer, then
    /// rebuild flat paths, recompute stats, and refresh the SVG preview.
    void applyPathTransform(std::function<glm::vec2(glm::vec2)> xform);

    void preprocessImage();
    void runLayerGeneration(entt::entity layerEntity,
        std::function<void(float, const std::string&)> onProgress);
    void computeStats();

    glm::vec2 pixelToMM(float px, float py) const;
    float sampleBrightness(float mmX, float mmY) const;

    ofImage  m_sourceImage;
    ofImage  m_workingImage;
    ofPixels m_workingPixels;
    bool     m_sourceLoaded = false;

    ofImage  m_svgPreview;   ///< colour preview rendered from fitted SVG paths

    // Scratch buffer used by plot finders during generation
    std::vector<ofPolyline> m_paths;

    // Flat path list across all visible layers (for preview + full G-code export)
    std::vector<ofPolyline>   m_flatPaths;
    std::vector<entt::entity> m_flatPathLayerEntity;  ///< parallel to m_flatPaths
    std::vector<ofColor>      m_flatPathColors;        ///< per-flat-path colour; SVG import sets individual colours, finders fall back to layer colour

    // Working image dimensions mapped to drawing area
    float m_drawWidth   = 0;
    float m_drawHeight  = 0;
    float m_drawOffsetX = 0;
    float m_drawOffsetY = 0;
    int m_workW = 0;
    int m_workH = 0;

    // Entity currently being generated (set temporarily during runLayerGeneration
    // so finders can read from registry.get<settings_component>(m_genEntity))
    entt::entity m_genEntity = entt::null;
};
