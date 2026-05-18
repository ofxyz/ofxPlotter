#pragma once

#include "ofMain.h"
#include "ofxKit.h"
#include "ofxPlotter.h"
#include <ofxImGuiVectorEditor/src/ImGuiVectorEditor.h>
#include "ofxGrbl.h"
#include "ofxGrblKit.h"
#include "RulerUtil.h"

#include <atomic>
#include <thread>

/// Image-to-plotter example using ofxKit.
///
/// Workflow:
///   1. Load an image (or SVG) using the Canvas / Sources panel.
///   2. Pick a paper size, margins, and PFM with its settings.
///   3. Click Generate — preview shows toolpaths over the canvas.
///   4. Press Ctrl/Cmd+E to enter Edit mode — panels appear and can be docked.
///   5. Connect to the machine via the Serial / Machine panel.
///   6. Click Send to plotter.
///
/// Layer state lives in m_engine.registry; the active layer is m_engine.activeLayer.
/// Use m_engine.getActiveSettings() / m_engine.getActiveLayerComponent() for quick access.

class ofApp : public ofBaseApp {
public:
    void setup() override;
    void update() override;
    void draw()   override;
    void exit()   override;

private:
    // -----------------------------------------------------------------------
    // Plotter engine
    // -----------------------------------------------------------------------
    ImageToPath          m_engine;
    std::atomic<bool>    m_generating             {false};
    std::atomic<bool>    m_needsTextureUpload     {false};
    std::atomic<bool>    m_needsSvgPreviewRender  {false}; ///< deferred: render SVG colour preview in update()
    std::atomic<bool>    m_needsRasterize         {false}; ///< deferred: rasterize fill layers in update(), then start generate
    entt::entity         m_pendingGenerateLayer  {entt::null}; ///< if not null, generate this single layer after rasterize
    std::atomic<float>   m_progress               {0.f};
    std::string          m_progressMsg;   ///< written only by generate thread
    std::string          m_imageName;     ///< display name of the loaded file
    std::thread          m_generateThread;

    // -----------------------------------------------------------------------
    // Machine
    // -----------------------------------------------------------------------
    grbl::GrblSender               m_sender;
    grbl::MachinePrefs             m_prefs;
    grbl::kit::PlotterSerialWindow m_serialWin;
    ofkitty::LayersPanel           m_layersPanel;
    ofkitty::ResourcesPanel        m_resources;

    // -----------------------------------------------------------------------
    // UI
    // -----------------------------------------------------------------------
    void setupUI();
    void drawSettingsPanel(bool& visible);
    void drawPreviewPanel(bool& visible);
    void onPlotterSourceChanged();
    const ofTexture* previewRasterTextureOrNull() const;

    bool                 m_showImageOverlay   {false};  ///< draw source/working raster behind paths
    float                m_imageOverlayAlpha  {0.35f};  ///< 0 = invisible, 1 = fully opaque
    bool                 m_imageSelected      {false};  ///< 2D transform handle active on overlay
    bool                 m_imageOverlayLocked {false};  ///< prevent accidental moves/resizes
    bool                 m_workingImageSynced {false};
    bool                 m_showPreviewRulers  {true};
    ofkitty::GuideSet    m_guides;                      ///< draggable guides for the preview panel

    bool                 m_showSvgOverlay     {true};   ///< draw SVG colour preview behind paths
    float                m_svgOverlayAlpha    {0.5f};   ///< 0 = invisible, 1 = fully opaque

    // Preview zoom / pan state — relative zoom (1 = fit-to-window), pan in canvas px
    float                m_previewZoom        {1.f};
    ImVec2               m_previewPan         {0.f, 0.f};

    // FBO backing the preview canvas (OF rendering — paths, image, paper).
    ofFbo                m_previewFbo;
    glm::vec2            m_previewFboSize     {0.f, 0.f};

    // Import options (persist across loads)
    ImageToPath::SvgImportMode m_svgImportMode {ImageToPath::SvgImportMode::GroupsAsLayers};

    // Path selection — layer entity + index within plotter::paths_component::paths.
    entt::entity         m_selPathEntity      {entt::null};
    int                  m_selPathIdx         {-1};
    // Inline path editor (rendered directly in the preview canvas).
    ImVectorEditor::Editor m_pathEditor;
    ImVectorEditor::Path   m_editPath;
    ImVectorEditor::Config m_editConfig;

    std::atomic<bool>    m_needsGcodeUpdate   {false};  ///< set by generate thread

    void startGenerate();
    void sendToPlotter();
};
