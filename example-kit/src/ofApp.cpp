#include "ofApp.h"
#include "imgui.h"
#include "RulerUtil.h"
#include <algorithm>
namespace {

/// ImGui::Begin titles must include ###id matching registerWindow(..., id) for docking.
constexpr const char* kPlotterWinIdSerial   = "plotter_kit.serial";
constexpr const char* kPlotterWinIdSettings = "plotter_kit.settings";
constexpr const char* kPlotterWinIdPreview  = "plotter_kit.preview";
constexpr const char* kPlotterWinIdLayers   = "plotter_kit.layers";

constexpr const char* kImGuiTitleSerial   = "Serial / Machine###plotter_kit.serial";
constexpr const char* kImGuiTitleSettings = "Source / Generation###plotter_kit.settings";
constexpr const char* kImGuiTitlePreview  = "Preview###plotter_kit.preview";
constexpr const char* kImGuiTitleLayers   = "Layers###plotter_kit.layers";


} // namespace

// ============================================================================
void ofApp::onPlotterSourceChanged()
{
    m_workingImageSynced = false;
}

const ofTexture* ofApp::previewRasterTextureOrNull() const
{
    if (m_workingImageSynced) {
        const ofImage& work = m_engine.getWorkingImage();
        if (work.isAllocated() && work.getWidth() > 0 && work.getHeight() > 0
            && work.getTexture().isAllocated()) {
            return &work.getTexture();
        }
    }
    const ofImage& src = m_engine.getSourceImage();
    if (m_engine.hasImage() && src.isAllocated() && src.getWidth() > 0 && src.getHeight() > 0
        && src.getTexture().isAllocated()) {
        return &src.getTexture();
    }
    return nullptr;
}

// ============================================================================
void ofApp::setup()
{
    ofSetFrameRate(60);
    ofBackground(20);

    ofkitty::runtime().setAppName("Plotter Kit");
    ofkitty::runtime().setImGuiIniPath(ofToDataPath("imgui_plotter_kit.ini", true));

    // The plotter uses Code Editor (G-code) and Preferences (ruler/UI settings).
    ofkitty::runtime().enableBuiltInWindow("Code Editor");
    ofkitty::runtime().enableBuiltInWindow("Preferences");

    // All content renders inside ImGui panels — no raw OpenGL scene needs to
    // bleed through the central dockspace gap, so use an opaque central node.
    ofkitty::runtime().setPassthruCentralNode(false);

    // GL_TEXTURE_2D (normalised UVs) must be requested before any textures
    // are created so ImGui can display OF textures with the correct UV space.
    ofDisableArbTex();

    m_prefs.load();
    m_serialWin.setSender(&m_sender);
    m_serialWin.setPrefs(&m_prefs);
    m_serialWin.refreshDeviceList();
    m_serialWin.syncSelectionFromPrefs();
    m_sender.setSimulationMode(false);

    // Conservative defaults for a typical GRBL pen plotter
    m_engine.pen.penDownZ    = 0.0f;
    m_engine.pen.penUpZ      = 5.0f;
    m_engine.pen.drawSpeed   = 800.f;
    m_engine.pen.travelSpeed = 3000.f;
    m_engine.pen.slowTravels = false;

    m_engine.paperSize        = PaperSize::A4;
    m_engine.paperOrientation = PaperOrientation::Portrait;
    m_engine.marginMM         = 10.f;

    m_layersPanel.setup(&m_engine.registry, &m_engine.activeLayer);

    m_layersPanel.setOnAddLayer([this] {
        m_engine.addLayer();
        m_engine.rebuildFlatPaths();
        onPlotterSourceChanged();
    });
    m_layersPanel.setOnRemoveLayer([this](entt::entity e) {
        m_engine.removeLayer(e);
        m_engine.rebuildFlatPaths();
        onPlotterSourceChanged();
    });
    m_layersPanel.setOnReparent([this](entt::entity child,
                                       entt::entity newParent,
                                       entt::entity insertBefore) {
        m_engine.reparentLayer(child, newParent, insertBefore);
        m_engine.rebuildFlatPaths();
        onPlotterSourceChanged();
    });
    m_layersPanel.setOnLayerChanged([this] {
        m_engine.rebuildFlatPaths();
        onPlotterSourceChanged();
    });
    m_layersPanel.setGetBadge([this](entt::entity e) -> std::string {
        if (!m_engine.registry.valid(e)) return "";
        if (!m_engine.registry.all_of<plotter::toolpath_stats_component>(e)) return "";
        const auto& sc = m_engine.registry.get<plotter::toolpath_stats_component>(e);
        return std::to_string(sc.totalPaths) + " paths";
    });
    m_layersPanel.setDrawContextMenu([this](entt::entity e) {
        bool hasFillRaster = m_engine.registry.valid(e) &&
            m_engine.registry.all_of<plotter::fill_raster_component>(e) &&
            m_engine.registry.get<plotter::fill_raster_component>(e).enabled;
        bool canGen = (m_engine.hasImage() || hasFillRaster) && !m_generating;
        if (!canGen) ImGui::BeginDisabled();
        if (ImGui::MenuItem("Generate this layer")) {
            if (hasFillRaster) {
                // Rasterization needs a clean GL context — defer to update().
                m_pendingGenerateLayer = e;
                m_needsRasterize = true;
            } else {
                m_engine.generateLayer(e);
                m_engine.rebuildFlatPaths();
                onPlotterSourceChanged();
                m_needsGcodeUpdate = true;
            }
        }
        if (!canGen) ImGui::EndDisabled();
    });

    // ---- Resources panel ----
    m_resources.setOnPlace([this](const ofkitty::Resource& r) {
        if (r.type == ofkitty::ResourceType::VectorSVG) {
            if (m_engine.loadVectorSVG(r.path, m_svgImportMode)) {
                m_imageName = r.name;
                m_showSvgOverlay = true;
                m_needsSvgPreviewRender = true;
                onPlotterSourceChanged();
            }
        } else if (r.type == ofkitty::ResourceType::Image) {
            if (m_engine.loadImage(r.path)) {
                m_imageName = r.name;
                auto da = m_engine.getDrawAreaMM();
                m_engine.imageOverlayX = da.x;  m_engine.imageOverlayY = da.y;
                m_engine.imageOverlayW = da.w;  m_engine.imageOverlayH = da.h;
                m_engine.rebuildFlatPaths();
                onPlotterSourceChanged();
            }
        } else if (r.type == ofkitty::ResourceType::GCodeSnippet) {
            ofkitty::runtime().codeEditorSetText(r.text);
            ofkitty::runtime().setWindowVisible("Code Editor", true);
        }
    });

    setupUI();

    ofkitty::runtime().addDefaultLayoutLeftDock(kImGuiTitleSerial);
    ofkitty::runtime().addDefaultLayoutLeftDock(kImGuiTitleSettings);
    ofkitty::runtime().addDefaultLayoutLeftDock(kImGuiTitleLayers);
    ofkitty::runtime().addDefaultLayoutLeftDock("Resources###plotter.resources");
    ofkitty::runtime().addDefaultLayoutCenterDock(kImGuiTitlePreview);

    // Start in Edit mode so panels are immediately visible on first launch.
    ofkitty::runtime().setEditMode(true);
}

// ============================================================================
void ofApp::update()
{
    m_sender.update();

    if (m_needsTextureUpload && !m_generating) {
        m_needsTextureUpload = false;
        m_engine.uploadWorkingImageTexture();
        m_workingImageSynced = true;
    }

    // Deferred SVG colour preview — must run outside any ImGui/FBO callback.
    if (m_needsSvgPreviewRender) {
        m_needsSvgPreviewRender = false;
        m_engine.renderSvgColorPreview();
    }

    // Deferred fill rasterization — must run on the GL thread, then either
    // generates a single layer or spawns the full generate worker thread.
    if (m_needsRasterize && !m_generating) {
        m_needsRasterize = false;
        m_engine.rasterizeLayerFills();

        entt::entity singleLayer = m_pendingGenerateLayer;
        m_pendingGenerateLayer = entt::null;

        if (singleLayer != entt::null && m_engine.registry.valid(singleLayer)) {
            // Single-layer generate (synchronous, context-menu path).
            m_engine.generateLayer(singleLayer);
            m_engine.rebuildFlatPaths();
            onPlotterSourceChanged();
            m_needsGcodeUpdate = true;
        } else {
            // Full generate: spawn worker thread.
            m_generating = true;
            m_progress   = 0.f;
            m_progressMsg.clear();
            m_workingImageSynced = false;
            m_generateThread = std::thread([this] {
                m_engine.generate([this](float p, const std::string& msg) {
                    m_progress    = p;
                    m_progressMsg = msg;
                });
                m_needsTextureUpload = true;
                m_needsGcodeUpdate   = true;
                m_generating = false;
            });
        }
    }

    // After generation completes, build G-code and push it into the Code Editor.
    if (m_needsGcodeUpdate && !m_generating) {
        m_needsGcodeUpdate = false;
        std::string gcode = plotter::toGCode(m_engine);
        ofkitty::runtime().codeEditorSetText(gcode);
        ofkitty::runtime().setWindowVisible("Code Editor", true);
    }

}

// ============================================================================
void ofApp::draw()
{
}

// ============================================================================
void ofApp::exit()
{
    if (m_generateThread.joinable()) m_generateThread.join();
    m_prefs.save();
}

// ============================================================================
void ofApp::setupUI()
{
    m_serialWin.setImguiWindowTitle(kImGuiTitleSerial);

    ofkitty::runtime().registerWindow({
        m_serialWin.name(), "View", true, true,
        [this](bool& visible) { m_serialWin.draw(visible); },
        kPlotterWinIdSerial,
    });

    ofkitty::runtime().registerWindow({
        "Source / Generation", "View", true, true,
        [this](bool& visible) { drawSettingsPanel(visible); },
        kPlotterWinIdSettings,
    });

    ofkitty::runtime().registerWindow({
        "Preview", "View", true, true,
        [this](bool& visible) { drawPreviewPanel(visible); },
        kPlotterWinIdPreview,
    });

    ofkitty::runtime().registerWindow({
        "Layers", "View", true, true,
        [this](bool& visible) { m_layersPanel.draw(kImGuiTitleLayers, visible); },
        kPlotterWinIdLayers,
    });

    ofkitty::runtime().registerWindow({
        "Resources", "View", true, true,
        [this](bool& visible) { m_resources.draw("Resources###plotter.resources", visible); },
    });

    ofkitty::runtime().addMenuBarGroup("Plotter", [this] {
        bool ready = m_sender.isConnected() && !m_generating
                     && !m_engine.getPaths().empty();

        if (!ready) ImGui::BeginDisabled();
        if (ImGui::MenuItem("Send to plotter")) sendToPlotter();
        if (!ready) ImGui::EndDisabled();

        if (ImGui::MenuItem("Pause", nullptr, m_sender.isQueuePaused()))
            m_sender.setQueuePaused(!m_sender.isQueuePaused());
        if (ImGui::MenuItem("Feed hold"))   m_sender.sendFeedHold();
        if (ImGui::MenuItem("Resume"))      m_sender.sendCycleStart();
        if (ImGui::MenuItem("Clear queue")) m_sender.clearQueue();

        ImGui::Separator();
        if (ImGui::MenuItem("Show all panels")) {
            ofkitty::runtime().setWindowVisible(m_serialWin.name(),      true);
            ofkitty::runtime().setWindowVisible("Source / Generation",   true);
            ofkitty::runtime().setWindowVisible("Preview",               true);
            ofkitty::runtime().setWindowVisible("Layers",                true);
        }
    });
}

// ============================================================================
void ofApp::drawSettingsPanel(bool& visible)
{
    if (!ImGui::Begin(kImGuiTitleSettings, &visible)) { ImGui::End(); return; }

    // ---- Canvas ----
    if (ImGui::CollapsingHeader("Canvas", ImGuiTreeNodeFlags_DefaultOpen)) {
        {
            static const char* kSizes[] = { "A4","A3","A2","A1","A0","Custom" };
            int idx = (int)m_engine.paperSize;
            if (ImGui::Combo("Size", &idx, kSizes, IM_ARRAYSIZE(kSizes)))
                m_engine.paperSize = (PaperSize)idx;
        }
        {
            static const char* kOrient[] = { "Portrait","Landscape" };
            int idx = (int)m_engine.paperOrientation;
            if (ImGui::Combo("Orientation", &idx, kOrient, IM_ARRAYSIZE(kOrient)))
                m_engine.paperOrientation = (PaperOrientation)idx;
        }
        if (m_engine.paperSize == PaperSize::Custom) {
            ImGui::DragFloat("Width mm",  &m_engine.customWidth,  1.f, 10.f, 2000.f, "%.0f");
            ImGui::DragFloat("Height mm", &m_engine.customHeight, 1.f, 10.f, 2000.f, "%.0f");
        }
        ImGui::DragFloat("Margin mm", &m_engine.marginMM, 0.5f, 0.f, 100.f, "%.1f");
        float col[3] = {
            m_engine.canvasColor.r / 255.f,
            m_engine.canvasColor.g / 255.f,
            m_engine.canvasColor.b / 255.f,
        };
        if (ImGui::ColorEdit3("Colour##canvas", col)) {
            m_engine.canvasColor.r = (unsigned char)(col[0] * 255.f);
            m_engine.canvasColor.g = (unsigned char)(col[1] * 255.f);
            m_engine.canvasColor.b = (unsigned char)(col[2] * 255.f);
        }
    }

    // ---- Sources ----
    if (ImGui::CollapsingHeader("Sources", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Load image...")) {
            ofFileDialogResult r = ofSystemLoadDialog("Load image");
            if (r.bSuccess) {
                ofkitty::Resource res;
                res.type   = ofkitty::ResourceType::Image;
                res.path   = r.filePath;
                res.name   = ofFilePath::getFileName(r.filePath);
                res.loaded = true;
                m_resources.addResource(std::move(res));
                if (m_engine.loadImage(r.filePath)) {
                    m_imageName = ofFilePath::getFileName(r.filePath);
                    m_engine.rebuildFlatPaths();
                    auto da = m_engine.getDrawAreaMM();
                    m_engine.imageOverlayX = da.x;  m_engine.imageOverlayY = da.y;
                    m_engine.imageOverlayW = da.w;  m_engine.imageOverlayH = da.h;
                    onPlotterSourceChanged();
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Load SVG...")) {
            ofFileDialogResult r = ofSystemLoadDialog("Load SVG");
            if (r.bSuccess) {
                ofkitty::Resource res;
                res.type   = ofkitty::ResourceType::VectorSVG;
                res.path   = r.filePath;
                res.name   = ofFilePath::getFileName(r.filePath);
                res.loaded = true;
                m_resources.addResource(std::move(res));
                if (m_engine.loadVectorSVG(r.filePath, m_svgImportMode)) {
                    m_imageName = ofFilePath::getFileName(r.filePath);
                    m_showSvgOverlay = true;
                    m_needsSvgPreviewRender = true;
                    onPlotterSourceChanged();
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Scale to Fit")) {
            m_engine.scaleToFit();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Rescale all layer paths to fill the canvas (maintains aspect ratio)");

        // SVG import mode selector
        {
            static const char* kModes[] = { "Groups as Layers", "Colours as Layers", "Single Layer" };
            int modeIdx = (int)m_svgImportMode;
            ImGui::SetNextItemWidth(160.f);
            if (ImGui::Combo("SVG import", &modeIdx, kModes, IM_ARRAYSIZE(kModes)))
                m_svgImportMode = (ImageToPath::SvgImportMode)modeIdx;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("How layers are created when importing an SVG file.\nTakes effect on the next Load SVG.");
        }

        if (!m_imageName.empty())
            ImGui::TextDisabled("%s", m_imageName.c_str());
    }

    // ---- Transform ----
    bool hasPaths = !m_engine.getPaths().empty();
    if (ImGui::CollapsingHeader("Transform")) {
        if (!hasPaths) ImGui::BeginDisabled();

        // Rotation row
        float btnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("Rotate 90° CW", ImVec2(btnW, 0))) {
            m_engine.rotate90CW();
            m_needsSvgPreviewRender = true;
            onPlotterSourceChanged();
        }
        ImGui::SameLine();
        if (ImGui::Button("Rotate 90° CCW", ImVec2(btnW, 0))) {
            m_engine.rotate90CCW();
            m_needsSvgPreviewRender = true;
            onPlotterSourceChanged();
        }

        // Flip row
        if (ImGui::Button("Flip Horizontal", ImVec2(btnW, 0))) {
            m_engine.flipHorizontal();
            m_needsSvgPreviewRender = true;
            onPlotterSourceChanged();
        }
        ImGui::SameLine();
        if (ImGui::Button("Flip Vertical", ImVec2(btnW, 0))) {
            m_engine.flipVertical();
            m_needsSvgPreviewRender = true;
            onPlotterSourceChanged();
        }

        if (!hasPaths) ImGui::EndDisabled();
        if (!hasPaths) ImGui::TextDisabled("Load an image or SVG first.");
    }

    // ---- Pre-process ----
    if (ImGui::CollapsingHeader("Pre-process")) {
        ImGui::SliderFloat("Brightness",  &m_engine.preprocess.brightness, -1.f, 1.f);
        ImGui::SliderFloat("Contrast",    &m_engine.preprocess.contrast,   -1.f, 1.f);
        ImGui::SliderFloat("Threshold",   &m_engine.preprocess.threshold,  -1.f, 1.f);
        ImGui::Checkbox("Invert",         &m_engine.preprocess.invert);
        ImGui::SliderFloat("Blur",        &m_engine.preprocess.blur, 0.f, 10.f);
        ImGui::Checkbox("Edge detect",    &m_engine.preprocess.edgeDetect);
    }

    // ---- Path Finding ----
    if (ImGui::CollapsingHeader("Path Finding", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool layerLocked = m_engine.getActiveLayerComponent().locked;
        if (layerLocked) ImGui::BeginDisabled();

        // Per-layer fill-with-strokes toggle.
        auto* frc = m_engine.activeLayer != entt::null
            ? m_engine.registry.try_get<plotter::fill_raster_component>(m_engine.activeLayer)
            : nullptr;
        if (frc) {
            ImGui::Checkbox("Fill shapes with strokes", &frc->enabled);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Rasterise this layer's closed filled shapes and run the\n"
                    "PFM below to simulate their fill weight with pen strokes.\n"
                    "Each layer can use a different PFM and settings.");
            if (frc->enabled) ImGui::Spacing();
        }

        auto& settings = m_engine.getActiveSettings();
        {
            static const char* kPFMs[] = {
                "Sketch Lines", "Cross-Hatch", "Spiral", "Stippling", "Contours"
            };
            int idx = (int)settings.pfmType;
            if (ImGui::Combo("PFM", &idx, kPFMs, IM_ARRAYSIZE(kPFMs)))
                settings.pfmType = (PFMType)idx;
        }

        ImGui::Indent();
        switch (settings.pfmType) {
            case PFMType::SketchLines: {
                auto& s = settings.sketchLines;
                ImGui::DragFloat("Min length",  &s.lineMinLength,  0.5f,  0.f, 200.f, "%.1f mm");
                ImGui::DragFloat("Max length",  &s.lineMaxLength,  0.5f,  0.f, 200.f, "%.1f mm");
                ImGui::DragFloat("Density",     &s.lineDensity,    1.f,   0.f, 200.f);
                ImGui::DragInt  ("Angle tests", &s.angleTests,     1,     1,   72);
                ImGui::Checkbox ("Lift pen",    &s.shouldLiftPen);
                break;
            }
            case PFMType::CrossHatch: {
                auto& s = settings.crossHatch;
                ImGui::DragFloat("Angle 1",        &s.angle1,        1.f, 0.f,  180.f, "%.0f°");
                ImGui::DragFloat("Angle 2",        &s.angle2,        1.f, 0.f,  180.f, "%.0f°");
                ImGui::Checkbox ("Second pass",    &s.useSecondary);
                ImGui::DragFloat("Line spacing",   &s.lineSpacing,   0.1f, 0.1f, 20.f, "%.1f mm");
                ImGui::DragFloat("Min brightness", &s.minBrightness, 0.01f, 0.f, 1.f);
                break;
            }
            case PFMType::Spiral: {
                auto& s = settings.spiral;
                ImGui::DragFloat("Ring spacing", &s.ringSpacing, 0.1f, 0.1f, 20.f, "%.1f mm");
                ImGui::DragFloat("Amplitude",    &s.amplitude,   0.1f, 0.f,  10.f);
                ImGui::DragFloat("Velocity",     &s.velocity,    0.1f, 0.1f, 20.f);
                ImGui::Checkbox ("Ignore white", &s.ignoreWhite);
                break;
            }
            case PFMType::Stippling: {
                auto& s = settings.stippling;
                ImGui::DragFloat("Spacing min", &s.dotSpacingMin, 0.05f, 0.1f, 20.f, "%.2f mm");
                ImGui::DragFloat("Spacing max", &s.dotSpacingMax, 0.05f, 0.1f, 20.f, "%.2f mm");
                ImGui::DragFloat("Dot radius",  &s.dotRadius,     0.01f, 0.1f,  5.f, "%.2f mm");
                ImGui::DragInt  ("Iterations",  &s.iterations,    1,     1,   500);
                break;
            }
            case PFMType::Contours: {
                auto& s = settings.contours;
                ImGui::DragFloat("Canny low",   &s.cannyLow,       1.f, 0.f, 255.f);
                ImGui::DragFloat("Canny high",  &s.cannyHigh,      1.f, 0.f, 255.f);
                ImGui::DragFloat("Min length",  &s.minContourLen,  0.5f, 0.f, 50.f, "%.1f mm");
                break;
            }
        }
        ImGui::Unindent();

        if (layerLocked) ImGui::EndDisabled();
        if (layerLocked) ImGui::TextDisabled("Layer is locked — unlock it in the Layers panel.");
    }

    // ---- Pen / Machine ----
    if (ImGui::CollapsingHeader("Pen / Machine", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat("Pen down Z",   &m_engine.pen.penDownZ,    0.1f,  -20.f, 20.f,    "%.1f mm");
        ImGui::DragFloat("Pen up Z",     &m_engine.pen.penUpZ,      0.1f,    0.f, 40.f,    "%.1f mm");
        ImGui::DragFloat("Draw speed",   &m_engine.pen.drawSpeed,  10.f,  100.f, 10000.f, "%.0f mm/min");
        ImGui::DragFloat("Travel speed", &m_engine.pen.travelSpeed,10.f,  100.f, 10000.f, "%.0f mm/min");
        ImGui::Checkbox ("Slow travels", &m_engine.pen.slowTravels);
    }

    // ---- Generate ----
    if (ImGui::CollapsingHeader("Generate", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool generating = m_generating.load();
        if (generating) {
            ImGui::ProgressBar(m_progress.load(), ImVec2(-1.f, 0.f), m_progressMsg.c_str());
        } else {
            bool canGenerate = m_engine.hasImage() || m_engine.hasFillLayers();
            bool hasSvgPaths = !m_engine.getPaths().empty() && !canGenerate;
            if (!canGenerate && !hasSvgPaths) ImGui::BeginDisabled();
            if (ImGui::Button("Generate##run", ImVec2(-1.f, 0.f))) {
                if (hasSvgPaths) {
                    std::string gcode = plotter::toGCode(m_engine);
                    ofkitty::runtime().codeEditorSetText(gcode);
                    ofkitty::runtime().setWindowVisible("Code Editor", true);
                } else {
                    startGenerate();
                }
            }
            if (!canGenerate && !hasSvgPaths) ImGui::EndDisabled();
            if (!canGenerate && !hasSvgPaths) ImGui::TextDisabled("Load an image or SVG first.");
        }

        if (!generating && !m_engine.getPaths().empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("Paths: %d   Points: %d",
                m_engine.totalPaths, m_engine.totalPoints);
            ImGui::TextDisabled("Distance: %.0f mm   Est. time: %.0f s",
                m_engine.totalDistance, m_engine.estimatedTime);

            ImGui::Spacing();
            bool connected = m_sender.isConnected();
            if (!connected) ImGui::BeginDisabled();
            if (ImGui::Button("Send to plotter", ImVec2(-1.f, 0.f))) sendToPlotter();
            if (!connected) ImGui::EndDisabled();
            if (!connected) ImGui::TextDisabled("Connect machine in Serial / Machine panel.");
        }
    }

    ImGui::End();
}

// ============================================================================
void ofApp::drawPreviewPanel(bool& visible)
{
    constexpr ImGuiWindowFlags kWinFlags = ImGuiWindowFlags_MenuBar;
    if (!ImGui::Begin(kImGuiTitlePreview, &visible, kWinFlags)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("Preview")) {
            if (m_engine.hasSvgPreview()) {
                ImGui::MenuItem("Show SVG", nullptr, &m_showSvgOverlay);
                if (m_showSvgOverlay) {
                    ImGui::SetNextItemWidth(120.f);
                    ImGui::SliderFloat("Dim##svg", &m_svgOverlayAlpha, 0.f, 1.f, "%.2f");
                }
                ImGui::Separator();
            }
            ImGui::MenuItem("Show Image", nullptr, &m_showImageOverlay);
            if (m_showImageOverlay) {
                ImGui::SetNextItemWidth(120.f);
                ImGui::SliderFloat("Dim##img", &m_imageOverlayAlpha, 0.f, 1.f, "%.2f");
                ImGui::MenuItem("Lock Image", nullptr, &m_imageOverlayLocked);
            }
            ImGui::Separator();
            ImGui::MenuItem("Rulers", nullptr, &m_showPreviewRulers);
            ImGui::Separator();
            ImGui::MenuItem("Guides", nullptr, &m_guides.visible);
            if (ImGui::MenuItem("Clear Guides")) { m_guides.h.clear(); m_guides.v.clear(); }
            ImGui::Separator();
            {
                bool showMargin = ofkitty::runtime().showMarginRect();
                if (ImGui::MenuItem("Margin", nullptr, &showMargin))
                    ofkitty::runtime().setShowMarginRect(showMargin);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Toggle the printable-area outline (purple by default).\n"
                                      "Colour lives in Preferences > Margin overlay.");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Fit to window")) { m_previewZoom = 1.f; m_previewPan = {0.f, 0.f}; }
            ImGui::EndMenu();
        }

        // ---- Zoom controls in menu bar ----
        ImGui::Separator();
        if (ImGui::SmallButton(" - "))
            m_previewZoom = std::max(0.1f, m_previewZoom / 1.25f);
        ImGui::SameLine(0, 2);
        char zoomLabel[16];
        snprintf(zoomLabel, sizeof(zoomLabel), " %3.0f%% ", m_previewZoom * 100.f);
        if (ImGui::SmallButton(zoomLabel)) { m_previewZoom = 1.f; m_previewPan = {0.f, 0.f}; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to fit\nScroll wheel to zoom\nAlt+drag or middle-drag to pan");
        ImGui::SameLine(0, 2);
        if (ImGui::SmallButton(" + "))
            m_previewZoom = std::min(50.f, m_previewZoom * 1.25f);

        ImGui::EndMenuBar();
    }

    if (m_generating.load()) {
        ImGui::ProgressBar(m_progress.load(), ImVec2(-1.f, 0.f), m_progressMsg.c_str());
        ImGui::End();
        return;
    }

    const auto& paths = m_engine.getPaths();
    if (paths.empty() && !m_engine.hasImage()) {
        ImGui::TextDisabled("No paths — load an image and click Generate, or import SVG.");
        ImGui::End();
        return;
    }

    // -------------------------------------------------------------------------
    // Layout: ruler strips are pinned to the window content edges; the canvas
    // starts after them so artwork is never under a ruler strip.
    // -------------------------------------------------------------------------
    const auto& prefs = ofkitty::runtime().appPrefs();
    const bool useMM  = (prefs.rulerUnit == ofkitty::Runtime::AppPrefs::RulerUnit::Millimetres);

    const float RS_px = m_showPreviewRulers
        ? std::round(20.f * ofkitty::runtime().uiScale() * prefs.rulerScale)
        : 0.f;

    // Full available content region (from ImGui cursor position, post-menubar)
    const ImVec2 windowOrigin = ImGui::GetCursorScreenPos();   // ruler corner
    const ImVec2 fullAvail    = ImGui::GetContentRegionAvail();

    // Canvas area: to the right/below the ruler strips
    const ImVec2 canvasOrigin(windowOrigin.x + RS_px, windowOrigin.y + RS_px);
    const float  canvasW = fullAvail.x - RS_px;
    const float  canvasH = fullAvail.y - RS_px;

    // Place the InvisibleButton over the canvas area only, so the ruler strip
    // area keeps its own hit zone for guide-drag creation.
    ImGui::SetCursorScreenPos(canvasOrigin);
    ImGui::InvisibleButton("canvas##paths", ImVec2(canvasW, canvasH));
    const bool canvasHovered = ImGui::IsItemHovered();
    ImGui::SetCursorScreenPos(windowOrigin);   // restore for any later ImGui calls

    const ImGuiIO& io = ImGui::GetIO();

    // ---- Zoom with mouse wheel (zoom around cursor) ----
    glm::vec2 paperMM = m_engine.getPaperSizeMM();
    const float fitZoom = std::min(canvasW / paperMM.x, canvasH / paperMM.y);

    if (canvasHovered && io.MouseWheel != 0.f) {
        const float factor  = (io.MouseWheel > 0.f) ? 1.15f : 1.f / 1.15f;
        const float zoomOld = fitZoom * m_previewZoom;
        // Paper left/top in canvas-local coords before zoom
        const float pxOld = canvasW * 0.5f + m_previewPan.x - paperMM.x * zoomOld * 0.5f;
        const float pyOld = canvasH * 0.5f + m_previewPan.y - paperMM.y * zoomOld * 0.5f;
        // Mouse position in paper units
        const float mu = (io.MousePos.x - canvasOrigin.x - pxOld) / zoomOld;
        const float mv = (io.MousePos.y - canvasOrigin.y - pyOld) / zoomOld;
        m_previewZoom  *= factor;
        const float zoomNew = fitZoom * m_previewZoom;
        // Adjust pan so the paper point under the cursor stays fixed
        m_previewPan.x = io.MousePos.x - canvasOrigin.x - mu * zoomNew - canvasW * 0.5f + paperMM.x * zoomNew * 0.5f;
        m_previewPan.y = io.MousePos.y - canvasOrigin.y - mv * zoomNew - canvasH * 0.5f + paperMM.y * zoomNew * 0.5f;
    }

    // ---- Pan with middle-mouse drag or Alt+LMB drag ----
    if (canvasHovered && (io.MouseDown[2] || (io.MouseDown[0] && io.KeyAlt))) {
        m_previewPan.x += io.MouseDelta.x;
        m_previewPan.y += io.MouseDelta.y;
    }

    // ---- Double-click to fit ----
    if (canvasHovered && io.MouseDoubleClicked[0] && !io.KeyAlt) {
        m_previewZoom = 1.f;
        m_previewPan  = {0.f, 0.f};
    }

    // ---- Compute paper screen position ----
    const float zoom = fitZoom * m_previewZoom;
    // ox, oy = screen-space position of the paper's top-left corner.
    // Coordinate system (Y-DOWN, matching OF screen and ImGui screen):
    //   screen = (ox + mm.x * zoom,  oy + mm.y * zoom)
    //   mm     = ((screen.x - ox) / zoom,  (screen.y - oy) / zoom)
    const float ox = canvasOrigin.x + canvasW * 0.5f + m_previewPan.x - paperMM.x * zoom * 0.5f;
    const float oy = canvasOrigin.y + canvasH * 0.5f + m_previewPan.y - paperMM.y * zoom * 0.5f;

    // Canonical coordinate converters — use these for ALL hit-testing and positioning.
    auto toScreen = [&](float mmx, float mmy) -> ImVec2 {
        return { ox + mmx * zoom, oy + mmy * zoom };
    };
    auto toMm = [&](float sx, float sy) -> glm::vec2 {
        return { (sx - ox) / zoom, (sy - oy) / zoom };
    };

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Clip all drawing to the canvas area so nothing bleeds under the ruler strips
    dl->PushClipRect(canvasOrigin,
                     ImVec2(canvasOrigin.x + canvasW, canvasOrigin.y + canvasH), true);

    // ---- Allocate / resize the OF-backed preview FBO ----
    {
        bool needsAlloc = !m_previewFbo.isAllocated()
            || std::fabs(m_previewFboSize.x - canvasW) > 0.5f
            || std::fabs(m_previewFboSize.y - canvasH) > 0.5f;
        if (needsAlloc) {
            ofDisableArbTex();
            ofFboSettings s;
            s.width          = std::max(1, (int)canvasW);
            s.height         = std::max(1, (int)canvasH);
            s.internalformat = GL_RGBA;
            s.useDepth       = false;
            m_previewFbo.allocate(s);
            ofEnableArbTex();
            m_previewFboSize = { canvasW, canvasH };
        }
    }

    // ---- Render scene into FBO ----
    // Paper origin in FBO pixel space (FBO covers canvasOrigin..canvasOrigin+canvasW/H)
    const float fboOx = ox - canvasOrigin.x;
    const float fboOy = oy - canvasOrigin.y;

    m_previewFbo.begin();
    ofClear(0, 0, 0, 0);
    // ofSetupScreenOrtho sets Y-UP (glm::ortho with bottom=0, top=h).
    // We immediately flip to Y-DOWN so that (0,0) is the top-left, matching
    // the screen-space margin overlay and making pan behave intuitively.
    ofSetupScreenOrtho(canvasW, canvasH, -1.f, 1.f);
    ofTranslate(0.f, canvasH, 0.f);
    ofScale(1.f, -1.f, 1.f);

    // Paper / canvas background
    {
        const ofColor& c = m_engine.canvasColor;
        ofSetColor(c);
        ofDrawRectangle(fboOx, fboOy, paperMM.x * zoom, paperMM.y * zoom);
    }

    // SVG colour preview overlay (drawn before paths and image so it sits at the bottom)
    if (m_showSvgOverlay && m_engine.hasSvgPreview()) {
        const ofTexture& svgTex = m_engine.getSvgPreview().getTexture();
        if (svgTex.isAllocated()) {
            const float dx = m_engine.imageOverlayX, dy = m_engine.imageOverlayY;
            const float dw = m_engine.imageOverlayW, dh = m_engine.imageOverlayH;
            if (dw > 0.f && dh > 0.f) {
                ofSetColor(255, 255, 255, (int)(m_svgOverlayAlpha * 255.f));
                svgTex.draw(fboOx + dx * zoom, fboOy + dy * zoom, dw * zoom, dh * zoom);
            }
        }
    }

    // Image overlay (drawn before paths so it sits underneath)
    if (m_showImageOverlay) {
        const ofTexture* tex = previewRasterTextureOrNull();
        const float iox = m_engine.imageOverlayX, ioy = m_engine.imageOverlayY;
        const float iow = m_engine.imageOverlayW, ioh = m_engine.imageOverlayH;
        if (tex && tex->isAllocated() && iow > 0.f && ioh > 0.f) {
            ofSetColor(255, 255, 255, (int)(m_imageOverlayAlpha * 255.f));
            tex->draw(fboOx + iox * zoom, fboOy + ioy * zoom, iow * zoom, ioh * zoom);
        }
    }

    // Paths — drawn with OF so we get smooth anti-aliased strokes
    {
        const auto& fpaths = m_engine.getPaths();
        ofPushMatrix();
        ofTranslate(fboOx, fboOy);
        ofScale(zoom, zoom);
        ofSetLineWidth(1.5f);   // screen pixels regardless of zoom
        for (int i = 0; i < (int)fpaths.size(); ++i) {
            const ofPolyline& poly = fpaths[i];
            if (poly.size() < 2) continue;
            ofColor c = m_engine.getPathColor(i);
            ofSetColor(c.r, c.g, c.b, 220);
            poly.draw();
        }
        ofPopMatrix();
    }

    m_previewFbo.end();

    // ---- Blit FBO into the ImGui panel ----
    // GL FBOs store rows bottom-up; flip V so it appears right-side up in ImGui.
    {
        ImGui::SetCursorScreenPos(canvasOrigin);
        ImTextureID tid = GetImTextureID(m_previewFbo.getTexture());
        ImGui::Image(tid, ImVec2(canvasW, canvasH), ImVec2(0, 1), ImVec2(1, 0));
    }

    // ---- Image overlay selection (hit-test only — rendering is in FBO) ----
    if (m_showImageOverlay) {
        const float iox = m_engine.imageOverlayX, ioy = m_engine.imageOverlayY;
        const float iow = m_engine.imageOverlayW, ioh = m_engine.imageOverlayH;
        if (iow > 0.f && ioh > 0.f) {
            ImVec2 imgTL(ox + iox * zoom,         oy + ioy * zoom);
            ImVec2 imgBR(ox + (iox + iow) * zoom, oy + (ioy + ioh) * zoom);

            // Select on mouse release so a drag doesn't accidentally select.
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                ImVec2 mp   = ImGui::GetMousePos();
                ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                bool   tiny = drag.x * drag.x + drag.y * drag.y < 16.f; // < 4px
                bool inside = mp.x >= imgTL.x && mp.x <= imgBR.x &&
                              mp.y >= imgTL.y && mp.y <= imgBR.y;
                if (tiny) m_imageSelected = inside;
            }
        }

        // 2D transform handle (only when selected and not locked)
        if (m_imageSelected && !m_imageOverlayLocked) {
            static ecs::TransformHandle2D s_imgHandle;
            ecs::Rect2D r { m_engine.imageOverlayX, m_engine.imageOverlayY,
                                 m_engine.imageOverlayW, m_engine.imageOverlayH };
            auto toScreenHandle = [&](float cx, float cy) -> ImVec2 { return toScreen(cx, cy); };
            auto toCanvasHandle = [&](float sx, float sy) -> ImVec2 {
                auto mm = toMm(sx, sy);
                return { mm.x, mm.y };
            };
            s_imgHandle.draw(dl, r, toScreenHandle, toCanvasHandle);
            m_engine.imageOverlayX = r.x;  m_engine.imageOverlayY = r.y;
            m_engine.imageOverlayW = r.w;  m_engine.imageOverlayH = r.h;
        } else if (m_imageSelected && m_imageOverlayLocked) {
            // Show a dashed outline to indicate the image is selected but locked.
            const float iox2 = m_engine.imageOverlayX, ioy2 = m_engine.imageOverlayY;
            const float iow2 = m_engine.imageOverlayW, ioh2 = m_engine.imageOverlayH;
            ImVec2 sTL(ox + iox2 * zoom,         oy + ioy2 * zoom);
            ImVec2 sTR(ox + (iox2+iow2) * zoom,  oy + ioy2 * zoom);
            ImVec2 sBR(ox + (iox2+iow2) * zoom,  oy + (ioy2+ioh2) * zoom);
            ImVec2 sBL(ox + iox2 * zoom,          oy + (ioy2+ioh2) * zoom);
            dl->AddQuad(sTL, sTR, sBR, sBL, IM_COL32(200, 200, 200, 160), 1.f);
        }
    }

    // Click in canvas to select the nearest path (for inline path editor).
    // Only act on a clean click (no drag, not over the image handle).
    if (canvasHovered
        && ImGui::IsMouseReleased(ImGuiMouseButton_Left)
        && !m_imageSelected
        && ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).x * ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).x
         + ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).y * ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).y < 16.f)
    {
        ImVec2 mp = io.MousePos;
        auto [mmx, mmy] = toMm(mp.x, mp.y);

        // Find the flat path whose nearest segment is closest to the click.
        const auto& fpaths     = paths;  // already retrieved at top of drawPreviewPanel
        const auto& fentities  = m_engine.getFlatPathEntities();
        float bestDist2 = 100.f * 100.f / (zoom * zoom);  // ~100 screen px threshold
        int   bestFlat  = -1;
        for (int fi = 0; fi < (int)fpaths.size(); ++fi) {
            const auto& poly = fpaths[fi];
            const auto& verts = poly.getVertices();
            for (size_t vi = 0; vi + 1 < verts.size(); ++vi) {
                // Closest point on segment to (mmx, mmy)
                float ax = verts[vi].x, ay = verts[vi].y;
                float bx = verts[vi+1].x, by = verts[vi+1].y;
                float dx = bx - ax, dy = by - ay;
                float len2 = dx*dx + dy*dy;
                float t = len2 > 0 ? std::max(0.f, std::min(1.f, ((mmx-ax)*dx + (mmy-ay)*dy) / len2)) : 0.f;
                float cx2 = ax + t*dx - mmx, cy2 = ay + t*dy - mmy;
                float d2 = cx2*cx2 + cy2*cy2;
                if (d2 < bestDist2) { bestDist2 = d2; bestFlat = fi; }
            }
        }

        if (bestFlat >= 0) {
            entt::entity layerEnt = (bestFlat < (int)fentities.size())
                                  ? fentities[bestFlat] : entt::null;
            // Map flat index → per-layer path index via paths_component order.
            int pathIdx = -1;
            if (m_engine.registry.valid(layerEnt)) {
                auto& pc = m_engine.registry.get<plotter::paths_component>(layerEnt);
                // Count how many flat paths come from earlier layers to find offset.
                int offset = 0;
                for (int fi2 = 0; fi2 < bestFlat; ++fi2)
                    if ((fi2 < (int)fentities.size()) && fentities[fi2] == layerEnt)
                        ++offset;
                // Count outlines per path until we reach offset.
                int count = 0;
                for (int pi = 0; pi < (int)pc.paths.size(); ++pi) {
                    int ol = (int)pc.paths[pi].getOutline().size();
                    if (count + ol > offset) { pathIdx = pi; break; }
                    count += ol;
                }
            }

            if (layerEnt != entt::null && pathIdx >= 0) {
                m_selPathEntity = layerEnt;
                m_selPathIdx    = pathIdx;
                auto& pc = m_engine.registry.get<plotter::paths_component>(layerEnt);
                // Convert selected ofPath into ImVectorEditor::Path for inline editing.
                m_editPath.clear();
                m_editConfig.tool = ImVectorEditor::Tool::Select;
                for (const auto& cmd : pc.paths[pathIdx].getCommands()) {
                    if (cmd.type == ofPath::Command::moveTo || cmd.type == ofPath::Command::lineTo) {
                        ImVectorEditor::Anchor a;
                        a.position = ImVec2(cmd.to.x, cmd.to.y);
                        m_editPath.anchors.push_back(a);
                    } else if (cmd.type == ofPath::Command::bezierTo) {
                        if (!m_editPath.anchors.empty()) {
                            m_editPath.anchors.back().handleOut    = ImVec2(cmd.cp1.x, cmd.cp1.y);
                            m_editPath.anchors.back().hasHandleOut = true;
                        }
                        ImVectorEditor::Anchor a;
                        a.position    = ImVec2(cmd.to.x, cmd.to.y);
                        a.handleIn    = ImVec2(cmd.cp2.x, cmd.cp2.y);
                        a.hasHandleIn = true;
                        m_editPath.anchors.push_back(a);
                    } else if (cmd.type == ofPath::Command::close) {
                        m_editPath.closed = true;
                    }
                }
                m_engine.activeLayer = layerEnt;
            }
        } else {
            // Click on empty space — deselect.
            m_selPathEntity = entt::null;
            m_selPathIdx    = -1;
            ofkitty::runtime().select(entt::null);
        }
    }

    // Inline vector editor — draws anchor handles over the selected path and
    // writes any changes immediately back to plotter::paths_component.
    if (m_selPathEntity != entt::null && m_selPathIdx >= 0
        && m_engine.registry.valid(m_selPathEntity)) {

        // Sync transform every frame so pan/zoom stays in lock-step with the preview.
        m_editConfig.canvasSize             = ImVec2(canvasW, canvasH);
        m_editConfig.showGrid               = false;
        m_editConfig.allowKeyboardShortcuts = false;
        m_editConfig.style.backgroundColor  = IM_COL32(0, 0, 0, 0);  // transparent
        m_editConfig.style.pathColor        = IM_COL32(255, 200, 50, 200);
        m_editConfig.style.previewColor     = IM_COL32(255, 200, 50, 80);
        m_editConfig.transform.zoom         = zoom;
        m_editConfig.transform.pan          = ImVec2(ox - canvasOrigin.x, oy - canvasOrigin.y);

        ImGui::SetCursorScreenPos(canvasOrigin);
        auto res = m_pathEditor.Draw("##pathEdit", m_editPath, m_editConfig);

        if (res.changed) {
            auto* pc = m_engine.registry.try_get<plotter::paths_component>(m_selPathEntity);
            if (pc && m_selPathIdx < (int)pc->paths.size()) {
                ofPath newPath;
                bool first = true;
                for (const auto& anchor : m_editPath.anchors) {
                    glm::vec3 p(anchor.position.x, anchor.position.y, 0.f);
                    if (first) { newPath.moveTo(p); first = false; }
                    else if (anchor.hasHandleIn || anchor.hasHandleOut) {
                        newPath.bezierTo(
                            glm::vec3(anchor.handleIn.x,  anchor.handleIn.y,  0.f),
                            glm::vec3(anchor.handleOut.x, anchor.handleOut.y, 0.f),
                            p);
                    } else {
                        newPath.lineTo(p);
                    }
                }
                if (m_editPath.closed) newPath.close();
                pc->paths[m_selPathIdx] = std::move(newPath);
                m_engine.rebuildFlatPaths();
            }
        }

        // Escape to deselect.
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            m_selPathEntity = entt::null;
            m_selPathIdx    = -1;
            m_editPath.clear();
        }
    }

    // Margin rectangle — paper inset by m_engine.marginMM on all four sides.
    // Drawn over paths so the printable boundary stays visible; the canvas
    // clip rect (still active here) prevents bleed into the ruler strips.
    if (ofkitty::runtime().showMarginRect() && m_engine.marginMM > 0.f
        && paperMM.x > 2.f * m_engine.marginMM
        && paperMM.y > 2.f * m_engine.marginMM)
    {
        const float mm = m_engine.marginMM;
        const ImVec2 marginTL(ox + mm * zoom, oy + mm * zoom);
        const ImVec2 marginBR(ox + (paperMM.x - mm) * zoom,
                              oy + (paperMM.y - mm) * zoom);
        const ofColor& mc = ofkitty::runtime().marginColor();
        ofkitty::drawMarginRect(
            dl, marginTL, marginBR,
            IM_COL32(mc.r, mc.g, mc.b, mc.a),
            1.f);
    }

    dl->PopClipRect();

    // ---- Rulers (drawn unclipped so strips reach window edges) ----
    if (m_showPreviewRulers && zoom > 0.f) {
        // scrollPx tells the ruler where tick-0 (the paper top-left) is
        // relative to the ruler's content-area origin (canvasOrigin).
        const float scrollX = ox - canvasOrigin.x;
        const float scrollY = oy - canvasOrigin.y;
        ofkitty::drawRulersInRegion(
            dl,
            windowOrigin,                    // ruler corner = window content top-left
            fullAvail,                       // strips span the entire content area
            io.MousePos,
            useMM ? zoom : 1.0f,
            useMM ? "mm" : "px",
            ofkitty::runtime().uiScale(),
            prefs.rulerScale,
            &m_guides,
            ImVec2(scrollX, scrollY));
    }

    ImGui::End();
}

// ============================================================================
void ofApp::startGenerate()
{
    if (!m_engine.hasImage() && !m_engine.hasFillLayers()) return;
    if (m_generating) return;

    if (m_generateThread.joinable()) m_generateThread.join();

    if (m_engine.hasFillLayers()) {
        // Rasterization must happen on the GL thread — defer to update().
        // update() will call rasterizeLayerFills() and then spawn the thread.
        m_needsRasterize = true;
    } else {
        // No fill layers: start the generate thread immediately.
        m_generating = true;
        m_progress   = 0.f;
        m_progressMsg.clear();
        m_workingImageSynced = false;

        m_generateThread = std::thread([this] {
            m_engine.generate([this](float p, const std::string& msg) {
                m_progress    = p;
                m_progressMsg = msg;
            });
            m_needsTextureUpload = true;
            m_needsGcodeUpdate   = true;
            m_generating = false;
        });
    }
}

// ============================================================================
void ofApp::sendToPlotter()
{
    if (m_engine.getPaths().empty()) return;
    m_sender.enqueueGCodeBlock(plotter::toGCode(m_engine));
}
