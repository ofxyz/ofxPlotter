#pragma once

#include "ofMain.h"
#include "ofxKit.h"
#include "ofxPlotter.h"
#include <ofxPlotProcessors/src/ofxPlotProcessors.h>
#include <ofxImGuiVectorEditor/src/ImGuiVectorEditor.h>
#include "ofxGrbl.h"
#include "ofxGrblKit.h"
#include "PlotterZones.h"
#include "PlotterBedCoords.h"
#include "GcodeGeneratorPanel.h"
#include "PlotterJogWindow.h"
#include "RulerUtil.h"

#include <atomic>
#include <thread>

/// Image-to-plotter example using ofxKit.
///
/// Workflow:
///   1. Load an image (or SVG) using the Canvas / Sources panel.
///   2. Pick a paper size, margins, and plot finder with its settings.
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
    plotter::PlotterZoneStore      m_zones;
    GcodeGeneratorPanel            m_gcodeGen;
    PlotterJogWindow               m_jogWin;

    plotter::ExportOptions exportOptions();
    plotproc::PlotPipeline  m_plotPipeline;
    plotter::BedView       bedView() const { return plotter::BedView::fromPrefs(m_prefs); }
    glm::vec2              contentToPaperMM(glm::vec2 contentPt, glm::vec2 paperMM,
                                            glm::vec2 paperOrg) const;
    glm::vec2              paperToContentMM(glm::vec2 paperPt, glm::vec2 paperMM,
                                            glm::vec2 paperOrg) const;

    // -----------------------------------------------------------------------
    // UI
    // -----------------------------------------------------------------------
    void setupUI();
    void drawSettingsPanel(bool& visible);
    // Preview panel — four focused callbacks wired to m_previewVP.
    void drawPreviewMenuBar();
    bool drawPreviewHeader();
    void drawPreviewContent();
    void drawPreviewOverlays(ofkitty::Runtime::ViewportInstance& vp);
    void drawPlotTransport(bool& visible);
    void drawPlotterPropertiesSupplement();
    void drawPlaybackTransport();
    void onPlotterSourceChanged();
    const ofTexture* previewRasterTextureOrNull() const;

    bool                 m_showImageOverlay   {false};  ///< draw source/working raster behind paths
    float                m_imageOverlayAlpha  {0.35f};  ///< 0 = invisible, 1 = fully opaque
    bool                 m_imageSelected      {false};  ///< 2D transform handle active on overlay
    bool                 m_imageOverlayLocked {false};  ///< prevent accidental moves/resizes
    bool                 m_workingImageSynced {false};
    bool                 m_showPreviewRulers  {true};  ///< synced to m_previewVP->showRulers
    ofkitty::GuideSet    m_guides;                      ///< draggable guides for the preview panel
    ofkitty::Runtime::ViewportInstance* m_previewVP {nullptr}; ///< Ortho2D canvas viewport

    bool                 m_showSvgOverlay     {true};   ///< draw SVG colour preview behind paths
    float                m_svgOverlayAlpha    {0.5f};   ///< 0 = invisible, 1 = fully opaque
    bool                 m_showMachineEnvelope{true};
    bool                 m_showZones          {true};
    bool                 m_showMargin         {true};
    bool                 m_showGrid           {true};
    bool                 m_showBrushPreview   {false};
    bool                 m_yAxisUp            {false};
    bool                 m_showPenPos         {true};
    float                m_playbackPos        {1.f};
    float                m_lastSyncedPlayback {1.f};
    bool                 m_syncingPlayback     {false};
    int                  m_lastPrintEditorLine {-1};
    float                m_livePenX           {0.f};
    float                m_livePenY           {0.f};
    bool                 m_livePenValid       {false};
    ofColor              m_marginColor        {180, 80, 220, 200};

    enum class ZoneTool { Select, NewZone, AddPosition };
    ZoneTool             m_zoneTool           {ZoneTool::Select};
    int                  m_selectedZoneIdx    {-1};
    bool                 m_zoneDragActive     {false};
    glm::vec2            m_zoneDragStartContent {};

    // Pan / zoom are now owned by m_previewVP (zoom2D, pan2D).

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
    void updateCodeEditorSidebar();
    void syncPlaybackToEditor();
    void setupEditorPlaybackSync();
};
