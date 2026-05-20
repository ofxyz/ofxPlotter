#include "ofApp.h"
#include "PlotPreviewDraw.h"
#include "PlotterGcodeSync.h"
#include "imgui.h"
#include "RulerUtil.h"
#include <algorithm>
namespace {

/// ImGui::Begin titles must include ###id matching registerWindow(..., id) for docking.
constexpr const char* kPlotterWinIdSerial   = "plotter_kit.serial";
constexpr const char* kPlotterWinIdSettings = "plotter_kit.settings";
constexpr const char* kPlotterWinIdLayers   = "plotter_kit.layers";
constexpr const char* kPlotterWinIdGcodeGen = "plotter_kit.gcode_gen";
constexpr const char* kPlotterWinIdJog       = "plotter_kit.jog";
constexpr const char* kPlotterWinIdTransport = "plotter_kit.transport";

constexpr const char* kImGuiTitleSerial   = "Serial / Machine###plotter_kit.serial";
constexpr const char* kImGuiTitleSettings = "Source / Generation###plotter_kit.settings";
constexpr const char* kImGuiTitlePreview  = "Plot Preview###plotter_kit.preview";
constexpr const char* kImGuiTitleLayers   = "Layers###plotter_kit.layers";
constexpr const char* kImGuiTitleGcodeGen = "G-code Generator###plotter_kit.gcode_gen";


} // namespace

glm::vec2 ofApp::contentToPaperMM(glm::vec2 contentPt, glm::vec2 paperMM,
                                  glm::vec2 paperOrg) const
{
    float px = contentPt.x - paperOrg.x;
    float py = contentPt.y - paperOrg.y;
    if (m_yAxisUp) py = paperMM.y - py;
    return {px, py};
}

glm::vec2 ofApp::paperToContentMM(glm::vec2 paperPt, glm::vec2 paperMM,
                                  glm::vec2 paperOrg) const
{
    float py = paperPt.y;
    if (m_yAxisUp) py = paperMM.y - py;
    return {paperOrg.x + paperPt.x, paperOrg.y + py};
}

plotter::ExportOptions ofApp::exportOptions()
{
    plotter::ExportOptions opts;
    opts.prefs = &m_prefs;
    opts.zones = &m_zones;
    opts.pipeline = &m_plotPipeline;
    opts.runPipeline = m_gcodeGen.runPipelineOnExport();
    opts.writeBackToPaths = m_gcodeGen.writeBackToPathsOnExport();
    return opts;
}

void ofApp::syncPlaybackToEditor()
{
    const int lineCount = ofkitty::runtime().codeEditorGetLineCount();
    const int line      = plotterGcodeSync::playbackToLine(m_playbackPos, lineCount);
    ofkitty::runtime().codeEditorSetHighlightLine(line);
}

void ofApp::setupEditorPlaybackSync()
{
    ofkitty::runtime().codeEditorSetSyncPlaybackFromCursor(true);
    ofkitty::runtime().codeEditorSetOnCursorLineChanged([this](int line) {
        if (m_syncingPlayback) return;
        const int lineCount = ofkitty::runtime().codeEditorGetLineCount();
        m_syncingPlayback   = true;
        m_playbackPos       = plotterGcodeSync::lineToPlayback(line, lineCount);
        m_lastSyncedPlayback = m_playbackPos;
        m_syncingPlayback   = false;
    });
}

void ofApp::updateCodeEditorSidebar()
{
    std::vector<ofkitty::CodeEditorPanel::SidebarEntry> entries;
    for (const auto& r : m_resources.resources()) {
        if (r.type != ofkitty::ResourceType::GCodeSnippet) continue;
        ofkitty::CodeEditorPanel::SidebarEntry item;
        item.label = r.name;
        item.path  = r.path;
        entries.push_back(std::move(item));
    }
    ofkitty::runtime().codeEditorSetSidebarEntries(std::move(entries));
}

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
    ofkitty::runtime().enableBuiltInWindow("Properties");

    // All content renders inside ImGui panels — no raw OpenGL scene needs to
    // bleed through the central dockspace gap, so use an opaque central node.
    ofkitty::runtime().setPassthruCentralNode(false);

    // GL_TEXTURE_2D (normalised UVs) must be requested before any textures
    // are created so ImGui can display OF textures with the correct UV space.
    ofDisableArbTex();

    m_prefs.load();
    m_zones.load();
    m_gcodeGen.setEngine(&m_engine);
    m_gcodeGen.setZoneStore(&m_zones);
    m_gcodeGen.setPipeline(&m_plotPipeline);
    {
        const std::string presetPath = ofToDataPath("plot_pipeline_default.json", true);
        if (ofFile::doesFileExist(presetPath))
            m_plotPipeline = plotproc::PlotPipeline::loadPreset(presetPath);
        else
            m_plotPipeline = plotproc::PlotPipeline::defaults();
    }
    m_gcodeGen.setSnippetPaths([this] {
        std::vector<std::string> paths;
        for (const auto& r : m_resources.resources()) {
            if (r.type == ofkitty::ResourceType::GCodeSnippet)
                paths.push_back(r.path);
        }
        return paths;
    });
    m_gcodeGen.setOnRegenerateGcode([this] { m_needsGcodeUpdate = true; });

    m_jogWin.setEngine(&m_engine);
    m_jogWin.setSender(&m_sender);
    m_jogWin.setPrefs(&m_prefs);
    m_jogWin.setImguiWindowTitle("Jog Control###plotter_kit.jog");
    m_jogWin.resetMachineCoordinates();

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
        if (m_engine.activeLayer != entt::null) {
            m_selectedZoneIdx = -1;
            m_gcodeGen.setSelectedZoneIndex(-1);
            m_selPathEntity = entt::null;
            m_selPathIdx    = -1;
            ofkitty::runtime().select(m_engine.activeLayer);
        }
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
            ofkitty::runtime().codeEditorSetText(r.text, TextEditor::LanguageDefinitionId::Gcode);
            ofkitty::runtime().setWindowVisible("Code Editor", true);
            updateCodeEditorSidebar();
        }
    });

    setupUI();

    ofkitty::runtime().addDefaultLayoutLeftDock(kImGuiTitleSerial);
    ofkitty::runtime().addDefaultLayoutLeftDock(kImGuiTitleSettings);
    ofkitty::runtime().addDefaultLayoutLeftDock(kImGuiTitleLayers);
    ofkitty::runtime().addDefaultLayoutLeftDock("Resources###plotter.resources");
    ofkitty::runtime().addDefaultLayoutLeftDock(kImGuiTitleGcodeGen);
    ofkitty::runtime().addDefaultLayoutLeftDock("Jog Control###plotter_kit.jog");
    ofkitty::runtime().addDefaultLayoutCenterDock(kImGuiTitlePreview);
    ofkitty::runtime().addDefaultLayoutCenterDock("Plot Transport###plotter_kit.transport");
    ofkitty::runtime().addDefaultLayoutRightDock("Properties###ofxkit.window.properties");
    ofkitty::runtime().addDefaultLayoutRightDock("Code Editor###ofxkit.window.code_editor");
    updateCodeEditorSidebar();
    setupEditorPlaybackSync();

    // Start in Edit mode so panels are immediately visible on first launch.
    ofkitty::runtime().setEditMode(true);
}

// ============================================================================
void ofApp::update()
{
    m_sender.update();
    m_jogWin.update();

    if (m_jogWin.isMachinePositionLive()) {
        const glm::vec3 mp = m_jogWin.getMachinePosition();
        m_livePenX     = mp.x;
        m_livePenY     = mp.y;
        m_livePenValid = true;
    } else {
        m_livePenValid = false;
    }

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
        m_playbackPos = 1.f;
        auto opts = exportOptions();
        std::string gcode = plotter::toGCode(m_engine, opts);
        if (opts.hasPipelineReport)
            m_gcodeGen.setLastPipelineReport(opts.lastPipelineReport, true);
        ofkitty::runtime().codeEditorSetText(gcode, TextEditor::LanguageDefinitionId::Gcode);
        ofkitty::runtime().setWindowVisible("Code Editor", true);
        updateCodeEditorSidebar();
        m_lastSyncedPlayback = m_playbackPos;
        syncPlaybackToEditor();
    }

    // Highlight the G-code line currently being sent while the queue is active.
    const bool printing = m_sender.pendingLines() > 0 || m_sender.isWaitingAck();
    const int  curLine  = m_sender.currentEditorLine();
    if (printing && curLine >= 0) {
        ofkitty::runtime().codeEditorSetHighlightLine(curLine);
        m_lastPrintEditorLine = curLine;
    } else if (m_lastPrintEditorLine >= 0) {
        m_lastPrintEditorLine = -1;
        syncPlaybackToEditor();
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
    m_zones.save();
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

    // Ortho2D viewport — content size is machine envelope (updated in headerDraw).
    m_previewVP = ofkitty::runtime().addViewportWindow2D(
        kImGuiTitlePreview, bedView().contentSize(), "mm", /*editModeOnly=*/false);
    m_previewVP->showRulers = m_showPreviewRulers;
    m_previewVP->guides     = &m_guides;

    m_previewVP->menuBarDraw = [this] { drawPreviewMenuBar(); };
    m_previewVP->headerDraw  = [this]() -> bool { return drawPreviewHeader(); };
    m_previewVP->renderer2D  = [this] { drawPreviewContent(); };
    m_previewVP->overlayDraw = [this](ofkitty::Runtime::ViewportInstance& vp) {
        drawPreviewOverlays(vp);
    };

    ofkitty::runtime().registerWindow({
        "Layers", "View", true, true,
        [this](bool& visible) { m_layersPanel.draw(kImGuiTitleLayers, visible); },
        kPlotterWinIdLayers,
    });

    ofkitty::runtime().registerWindow({
        "Resources", "View", true, true,
        [this](bool& visible) {
            m_resources.draw("Resources###plotter.resources", visible);
            updateCodeEditorSidebar();
        },
    });

    ofkitty::runtime().registerWindow({
        "G-code Generator", "View", true, true,
        [this](bool& visible) { m_gcodeGen.draw(kImGuiTitleGcodeGen, visible); },
        kPlotterWinIdGcodeGen,
    });

    ofkitty::runtime().registerWindow({
        "Jog Control", "View", true, false,
        [this](bool& visible) { m_jogWin.draw(visible); },
        kPlotterWinIdJog,
    });

    ofkitty::runtime().registerWindow({
        "Plot Transport", "View", true, false,
        [this](bool& visible) { drawPlotTransport(visible); },
        kPlotterWinIdTransport,
    });

    ofkitty::runtime().registerStatusItem({
        "plotter_kit.status.machine",
        "Plotter",
        true,
        [this] {
            const bool sim = m_sender.isSimulationMode();
            const bool connected = m_sender.isConnected();
            if (connected) {
                const ImVec4 col = sim ? ImVec4(0.95f, 0.75f, 0.25f, 1.f)
                                       : ImVec4(0.39f, 0.90f, 0.50f, 1.f);
                ImGui::TextColored(col, sim ? "Simulation" : "Connected");
            } else {
                ImGui::TextColored(ImVec4(0.9f, 0.35f, 0.35f, 1.f), "Disconnected");
            }
            if (m_livePenValid) {
                ImGui::SameLine();
                ImGui::TextDisabled("X%.1f Y%.1f", m_livePenX, m_livePenY);
            }
        },
    });

    ofkitty::runtime().registerStatusItem({
        "plotter_kit.status.jog",
        "Plotter",
        true,
        [this] { m_jogWin.drawStatusBar(); },
    });

    ofkitty::runtime().setPropertiesSupplement([this] { drawPlotterPropertiesSupplement(); });

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
            ofkitty::runtime().setWindowVisible("Plot Preview",          true);
            ofkitty::runtime().setWindowVisible("Jog Control",           true);
            ofkitty::runtime().setWindowVisible("Plot Transport",      true);
            ofkitty::runtime().setWindowVisible("Properties",          true);
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
                    "plot finder below to simulate their fill weight with pen strokes.\n"
                    "Each layer can use a different finder and settings.");
            if (frc->enabled) ImGui::Spacing();
        }

        auto& settings = m_engine.getActiveSettings();
        {
            static const char* kPlotFinders[] = {
                "Sketch Lines", "Cross-Hatch", "Spiral", "Stippling", "Contours"
            };
            int idx = (int)settings.plotFinderType;
            if (ImGui::Combo("Plot finder", &idx, kPlotFinders, IM_ARRAYSIZE(kPlotFinders)))
                settings.plotFinderType = (PlotFinderType)idx;
        }

        ImGui::Indent();
        switch (settings.plotFinderType) {
            case PlotFinderType::SketchLines: {
                auto& s = settings.sketchLines;
                ImGui::DragFloat("Min length",  &s.lineMinLength,  0.5f,  0.f, 200.f, "%.1f mm");
                ImGui::DragFloat("Max length",  &s.lineMaxLength,  0.5f,  0.f, 200.f, "%.1f mm");
                ImGui::DragFloat("Density",     &s.lineDensity,    1.f,   0.f, 200.f);
                ImGui::DragInt  ("Angle tests", &s.angleTests,     1,     1,   72);
                ImGui::Checkbox ("Lift pen",    &s.shouldLiftPen);
                break;
            }
            case PlotFinderType::CrossHatch: {
                auto& s = settings.crossHatch;
                ImGui::DragFloat("Angle 1",        &s.angle1,        1.f, 0.f,  180.f, "%.0f°");
                ImGui::DragFloat("Angle 2",        &s.angle2,        1.f, 0.f,  180.f, "%.0f°");
                ImGui::Checkbox ("Second pass",    &s.useSecondary);
                ImGui::DragFloat("Line spacing",   &s.lineSpacing,   0.1f, 0.1f, 20.f, "%.1f mm");
                ImGui::DragFloat("Min brightness", &s.minBrightness, 0.01f, 0.f, 1.f);
                break;
            }
            case PlotFinderType::Spiral: {
                auto& s = settings.spiral;
                ImGui::DragFloat("Ring spacing", &s.ringSpacing, 0.1f, 0.1f, 20.f, "%.1f mm");
                ImGui::DragFloat("Amplitude",    &s.amplitude,   0.1f, 0.f,  10.f);
                ImGui::DragFloat("Velocity",     &s.velocity,    0.1f, 0.1f, 20.f);
                ImGui::Checkbox ("Ignore white", &s.ignoreWhite);
                break;
            }
            case PlotFinderType::Stippling: {
                auto& s = settings.stippling;
                ImGui::DragFloat("Spacing min", &s.dotSpacingMin, 0.05f, 0.1f, 20.f, "%.2f mm");
                ImGui::DragFloat("Spacing max", &s.dotSpacingMax, 0.05f, 0.1f, 20.f, "%.2f mm");
                ImGui::DragFloat("Dot radius",  &s.dotRadius,     0.01f, 0.1f,  5.f, "%.2f mm");
                ImGui::DragInt  ("Iterations",  &s.iterations,    1,     1,   500);
                break;
            }
            case PlotFinderType::Contours: {
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
                    auto opts = exportOptions();
        std::string gcode = plotter::toGCode(m_engine, opts);
        if (opts.hasPipelineReport)
            m_gcodeGen.setLastPipelineReport(opts.lastPipelineReport, true);
                    ofkitty::runtime().codeEditorSetText(gcode, TextEditor::LanguageDefinitionId::Gcode);
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
// Preview — four focused callbacks wired to the Ortho2D viewport (m_previewVP)
// ============================================================================

void ofApp::drawPreviewMenuBar()
{
    if (!m_previewVP) return;
    if (ImGui::BeginMenu("Plot Preview")) {
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
        ImGui::MenuItem("Rulers", nullptr, &m_previewVP->showRulers);
        ImGui::Separator();
        ImGui::MenuItem("Guides", nullptr, &m_guides.visible);
        if (ImGui::MenuItem("Clear Guides")) { m_guides.h.clear(); m_guides.v.clear(); }
        ImGui::Separator();
        ImGui::MenuItem("Grid", nullptr, &m_showGrid);
        ImGui::MenuItem("Brush preview", nullptr, &m_showBrushPreview);
        ImGui::MenuItem("Y+ up (GRBL)", nullptr, &m_yAxisUp);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("ON: origin bottom-left like GRBL output.\nOFF: top-left like the source image.");
        ImGui::MenuItem("Machine envelope", nullptr, &m_showMachineEnvelope);
        ImGui::MenuItem("Zones", nullptr, &m_showZones);
        ImGui::MenuItem("Margin", nullptr, &m_showMargin);
        ImGui::MenuItem("Pen position", nullptr, &m_showPenPos);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Cyan = live GRBL position. Red crosshair = playback play head.");
        if (ImGui::BeginMenu("Zone tool")) {
            bool sel = m_zoneTool == ZoneTool::Select;
            bool nz  = m_zoneTool == ZoneTool::NewZone;
            bool ap  = m_zoneTool == ZoneTool::AddPosition;
            if (ImGui::MenuItem("Select", nullptr, sel)) m_zoneTool = ZoneTool::Select;
            if (ImGui::MenuItem("New zone (drag)", nullptr, nz)) m_zoneTool = ZoneTool::NewZone;
            if (ImGui::MenuItem("Add position", nullptr, ap)) m_zoneTool = ZoneTool::AddPosition;
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Fit to window"))
            { m_previewVP->zoom2D = 1.f; m_previewVP->pan2D = {}; }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::SmallButton(" - "))
        m_previewVP->zoom2D = std::max(0.1f, m_previewVP->zoom2D / 1.25f);
    ImGui::SameLine(0, 2);
    char zoomLabel[16];
    snprintf(zoomLabel, sizeof(zoomLabel), " %3.0f%% ", m_previewVP->zoom2D * 100.f);
    if (ImGui::SmallButton(zoomLabel)) { m_previewVP->zoom2D = 1.f; m_previewVP->pan2D = {}; }
    ImGui::SameLine(0, 2);
    if (ImGui::SmallButton(" + "))
        m_previewVP->zoom2D = std::min(50.f, m_previewVP->zoom2D * 1.25f);
}

void ofApp::drawPlaybackTransport()
{
    if (m_engine.getPaths().empty()) {
        ImGui::TextDisabled("Generate paths to enable playback.");
        return;
    }

    const int totalPaths = (int)m_engine.getPaths().size();
    int currentPath      = (int)(m_playbackPos * totalPaths);
    currentPath          = std::clamp(currentPath, 0, totalPaths);
    const int gcodeLines = ofkitty::runtime().codeEditorGetLineCount();
    const int gcodeLine  = plotterGcodeSync::playbackToLine(m_playbackPos, gcodeLines);
    float pct            = m_playbackPos * 100.f;

    if (ImGui::Button("|<")) {
        m_playbackPos = 0.f;
        syncPlaybackToEditor();
        m_lastSyncedPlayback = m_playbackPos;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Start");
    ImGui::SameLine();
    if (ImGui::Button("<")) {
        m_playbackPos = std::max(0.f, m_playbackPos - 1.f / std::max(1, totalPaths));
        syncPlaybackToEditor();
        m_lastSyncedPlayback = m_playbackPos;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Previous path");
    ImGui::SameLine();
    if (ImGui::Button(">")) {
        m_playbackPos = std::min(1.f, m_playbackPos + 1.f / std::max(1, totalPaths));
        syncPlaybackToEditor();
        m_lastSyncedPlayback = m_playbackPos;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Next path");
    ImGui::SameLine();
    if (ImGui::Button(">|")) {
        m_playbackPos = 1.f;
        syncPlaybackToEditor();
        m_lastSyncedPlayback = m_playbackPos;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("End");

    ImGui::SameLine();
    ImGui::PushItemWidth(-1.f);
    char label[192];
    snprintf(label, sizeof(label),
             "Play head %.1f%%  |  G-code line %d/%d  |  Path %d/%d  |  red tip in preview",
             pct, gcodeLine + 1, gcodeLines, currentPath, totalPaths);
    if (ImGui::SliderFloat("##playback", &pct, 0.f, 100.f, label)) {
        m_playbackPos = pct / 100.f;
        if (!m_syncingPlayback && m_playbackPos != m_lastSyncedPlayback) {
            m_lastSyncedPlayback = m_playbackPos;
            syncPlaybackToEditor();
        }
    }
    ImGui::PopItemWidth();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Scrub the plot play head. Syncs to the Code Editor current line.\n"
            "Cyan crosshair = live machine position when connected.");
}

bool ofApp::drawPreviewHeader()
{
    if (m_generating.load()) {
        ImGui::ProgressBar(m_progress.load(), ImVec2(-1.f, 0.f), m_progressMsg.c_str());
        return true;
    }
    if (m_engine.getPaths().empty() && !m_engine.hasImage()) {
        ImGui::TextDisabled("No paths — load an image and click Generate, or import SVG.");
        return true;
    }
    if (m_previewVP)
        m_previewVP->contentSize = bedView().contentSize();

    if (!m_engine.getPaths().empty())
        drawPlaybackTransport();

    return false;
}

void ofApp::drawPlotTransport(bool& visible)
{
    ImGui::SetNextWindowSize(ImVec2(640, 72), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Plot Transport###plotter_kit.transport", &visible)) {
        ImGui::End();
        return;
    }
    drawPlaybackTransport();
    ImGui::End();
}

void ofApp::drawPlotterPropertiesSupplement()
{
    if (m_selectedZoneIdx >= 0 && m_selectedZoneIdx < (int)m_zones.zones.size()) {
        ImGui::TextUnformatted("Maintenance zone");
        m_gcodeGen.setSelectedZoneIndex(m_selectedZoneIdx);
        m_gcodeGen.drawZoneInspector();
        return;
    }

    if (ofkitty::runtime().selected() != entt::null) {
        ImGui::TextDisabled("Layer / path — ECS components below.");
        return;
    }

    ImGui::TextUnformatted("Pen / machine");
    ImGui::DragFloat("Pen down Z", &m_engine.pen.penDownZ, 0.1f, -20.f, 20.f, "%.1f mm");
    ImGui::DragFloat("Pen up Z", &m_engine.pen.penUpZ, 0.1f, 0.f, 40.f, "%.1f mm");
    ImGui::DragFloat("Draw speed", &m_engine.pen.drawSpeed, 10.f, 100.f, 10000.f, "%.0f mm/min");
    ImGui::DragFloat("Travel speed", &m_engine.pen.travelSpeed, 10.f, 100.f, 10000.f, "%.0f mm/min");
    ImGui::Checkbox("Slow travels", &m_engine.pen.slowTravels);
    ImGui::TextDisabled("Click a zone in Plot Preview or a path/layer to edit more.");
}

void ofApp::drawPreviewContent()
{
    // Content coords = machine bed relative to envelope min.
    const plotter::BedView bed = bedView();
    const glm::vec2 paperMM  = m_engine.getPaperSizeMM();
    const glm::vec2 paperOrg = bed.paperOriginContent();

    ofSetColor(40, 42, 48);
    ofDrawRectangle(0, 0, bed.contentSize().x, bed.contentSize().y);

    ofPushMatrix();
    ofTranslate(paperOrg.x, paperOrg.y);
    if (m_yAxisUp) {
        ofTranslate(0.f, paperMM.y);
        ofScale(1.f, -1.f);
    }

    ofSetColor(m_engine.canvasColor);
    ofDrawRectangle(0, 0, paperMM.x, paperMM.y);

    if (m_showGrid && m_previewVP)
        plotPreview::drawPaperGrid(paperMM.x, paperMM.y, m_previewVP->contentZoom());

    if (m_showSvgOverlay && m_engine.hasSvgPreview()) {
        const ofTexture& svgTex = m_engine.getSvgPreview().getTexture();
        if (svgTex.isAllocated()) {
            const float dx = m_engine.imageOverlayX, dy = m_engine.imageOverlayY;
            const float dw = m_engine.imageOverlayW, dh = m_engine.imageOverlayH;
            if (dw > 0.f && dh > 0.f) {
                ofSetColor(255, 255, 255, (int)(m_svgOverlayAlpha * 255.f));
                svgTex.draw(dx, dy, dw, dh);
            }
        }
    }

    if (m_showImageOverlay) {
        const ofTexture* tex = previewRasterTextureOrNull();
        const float iox = m_engine.imageOverlayX, ioy = m_engine.imageOverlayY;
        const float iow = m_engine.imageOverlayW, ioh = m_engine.imageOverlayH;
        if (tex && tex->isAllocated() && iow > 0.f && ioh > 0.f) {
            ofSetColor(255, 255, 255, (int)(m_imageOverlayAlpha * 255.f));
            tex->draw(iox, ioy, iow, ioh);
        }
    }

    const auto& fpaths = m_engine.getPaths();
    const int maxPath  = std::clamp((int)(m_playbackPos * fpaths.size()), 0, (int)fpaths.size());

    if (m_showBrushPreview)
        plotPreview::drawBrushEstimation(m_engine, maxPath);

    ofSetLineWidth(1.5f);
    for (int i = 0; i < maxPath; ++i) {
        const ofPolyline& poly = fpaths[i];
        if (poly.size() < 2) continue;
        ofColor c = m_engine.getPathColor(i);
        ofSetColor(c.r, c.g, c.b, 220);
        poly.draw();
    }

    ofPopMatrix();
}


void ofApp::drawPreviewOverlays(ofkitty::Runtime::ViewportInstance& vp)
{
    ImDrawList*    dl      = ImGui::GetWindowDrawList();
    const ImGuiIO& io      = ImGui::GetIO();
    const float    zoom    = vp.contentZoom();
    const plotter::BedView bed = bedView();
    const glm::vec2 paperMM  = m_engine.getPaperSizeMM();
    const glm::vec2 paperOrg = bed.paperOriginContent();
    const auto&    fpaths  = m_engine.getPaths();

    // ---- Machine envelope (full content area) ----
    if (m_showMachineEnvelope) {
        const glm::vec2 cs = bed.contentSize();
        ImVec2 tl = vp.toScreen(0, 0);
        ImVec2 br = vp.toScreen(cs.x, cs.y);
        dl->AddRect(tl, br, IM_COL32(120, 140, 180, 200), 0.f, 0, 2.f);
    }

    // ---- Maintenance zones ----
    if (m_showZones) {
        for (int zi = 0; zi < (int)m_zones.zones.size(); ++zi) {
            const auto& z = m_zones.zones[zi];
            const glm::vec2 ztl = bed.machineToContent(z.x, z.y);
            const glm::vec2 zbr = bed.machineToContent(z.x + z.w, z.y + z.h);
            const bool sel = m_selectedZoneIdx == zi;
            const ImU32 fill = sel ? IM_COL32(80, 160, 255, 40) : IM_COL32(80, 160, 255, 20);
            const ImU32 stroke = sel ? IM_COL32(80, 200, 255, 220) : IM_COL32(80, 160, 255, 140);
            ImVec2 sTL = vp.toScreen(ztl.x, ztl.y);
            ImVec2 sBR = vp.toScreen(zbr.x, zbr.y);
            dl->AddRectFilled(sTL, sBR, fill);
            dl->AddRect(sTL, sBR, stroke, 0.f, 0, sel ? 2.f : 1.f);
            for (const auto& p : z.positions) {
                const glm::vec2 pc = bed.machineToContent(p.x, p.y);
                ImVec2 sp = vp.toScreen(pc.x, pc.y);
                dl->AddCircleFilled(sp, 5.f, IM_COL32(255, 200, 60, 255));
                dl->AddCircle(sp, 5.f, IM_COL32(40, 40, 40, 255), 0, 1.5f);
            }
        }
    }

    if (!fpaths.empty())
        m_gcodeGen.refreshInjectionMarkers();

    // ---- Injection preview markers ----
    for (const auto& mk : m_gcodeGen.injectionMarkersContent()) {
        ImVec2 sp = vp.toScreen(mk.x, mk.y);
        dl->AddCircleFilled(sp, 4.f, IM_COL32(255, 80, 80, 220));
    }

    // ---- Live machine pen (cyan) and playback tip (red) ----
    if (m_showPenPos) {
        if (m_livePenValid) {
            const glm::vec2 lc = bed.machineToContent(m_livePenX, m_livePenY);
            ImVec2 sp = vp.toScreen(lc.x, lc.y);
            dl->AddCircle(sp, 6.f, IM_COL32(50, 200, 255, 220), 0, 2.f);
            dl->AddLine(ImVec2(sp.x - 8, sp.y), ImVec2(sp.x + 8, sp.y), IM_COL32(50, 200, 255, 255), 1.5f);
            dl->AddLine(ImVec2(sp.x, sp.y - 8), ImVec2(sp.x, sp.y + 8), IM_COL32(50, 200, 255, 255), 1.5f);
        }
        const auto& fpaths = m_engine.getPaths();
        if (!fpaths.empty()) {
            const int maxPath = std::clamp((int)(m_playbackPos * fpaths.size()), 1, (int)fpaths.size());
            const auto& lastPath = fpaths[maxPath - 1];
            if (lastPath.size() >= 1) {
                const auto& v = lastPath.getVertices().back();
                const glm::vec2 pc = paperToContentMM({v.x, v.y}, paperMM, paperOrg);
                ImVec2 sp = vp.toScreen(pc.x, pc.y);
                dl->AddCircle(sp, 5.f, IM_COL32(255, 50, 50, 200), 0, 1.5f);
            }
        }
    }

    // ---- Zone tool: drag new zone ----
    if (m_showZones && m_zoneTool == ZoneTool::NewZone && vp.isCanvasHovered()) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_zoneDragActive = true;
            glm::vec2 c = vp.toContent(io.MousePos.x, io.MousePos.y);
            m_zoneDragStartContent = c;
        }
        if (m_zoneDragActive && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            glm::vec2 c = vp.toContent(io.MousePos.x, io.MousePos.y);
            const float x1 = std::min(m_zoneDragStartContent.x, c.x);
            const float y1 = std::min(m_zoneDragStartContent.y, c.y);
            const float x2 = std::max(m_zoneDragStartContent.x, c.x);
            const float y2 = std::max(m_zoneDragStartContent.y, c.y);
            if ((x2 - x1) > 2.f && (y2 - y1) > 2.f) {
                plotter::MaintenanceZone z;
                z.id   = m_zones.makeUniqueZoneId();
                z.name = "Zone " + std::to_string((int)m_zones.zones.size() + 1);
                const glm::vec2 m1 = bed.contentToMachine(x1, y1);
                const glm::vec2 m2 = bed.contentToMachine(x2, y2);
                z.x = m1.x;
                z.y = m1.y;
                z.w = m2.x - m1.x;
                z.h = m2.y - m1.y;
                m_zones.zones.push_back(std::move(z));
                m_selectedZoneIdx = (int)m_zones.zones.size() - 1;
                m_gcodeGen.setSelectedZoneIndex(m_selectedZoneIdx);
                ofkitty::runtime().select(entt::null);
                ofkitty::runtime().setWindowVisible("Properties", true);
            }
            m_zoneDragActive = false;
        }
        if (m_zoneDragActive) {
            glm::vec2 c = vp.toContent(io.MousePos.x, io.MousePos.y);
            ImVec2 s1 = vp.toScreen(m_zoneDragStartContent.x, m_zoneDragStartContent.y);
            ImVec2 s2 = vp.toScreen(c.x, c.y);
            dl->AddRect(s1, s2, IM_COL32(80, 200, 255, 180), 0.f, 0, 1.f);
        }
    }

    // ---- Selected zone transform handle (machine coords) ----
    if (m_showZones && m_selectedZoneIdx >= 0
        && m_selectedZoneIdx < (int)m_zones.zones.size()
        && m_zoneTool == ZoneTool::Select)
    {
        auto& z = m_zones.zones[m_selectedZoneIdx];
        static ecs::TransformHandle2D s_zoneHandle;
        const glm::vec2 ztl = bed.machineToContent(z.x, z.y);
        ecs::Rect2D r { ztl.x, ztl.y, z.w, z.h };
        auto toScr = [&](float cx, float cy) -> ImVec2 { return vp.toScreen(cx, cy); };
        auto toCnt = [&](float sx, float sy) -> ImVec2 {
            auto c = vp.toContent(sx, sy);
            return { c.x, c.y };
        };
        s_zoneHandle.draw(dl, r, toScr, toCnt);
        const glm::vec2 mtl = bed.contentToMachine(r.x, r.y);
        z.x = mtl.x;
        z.y = mtl.y;
        z.w = r.w;
        z.h = r.h;
    }

    // ---- Zone select / add position click ----
    if (m_showZones && vp.isCanvasHovered()
        && ImGui::IsMouseReleased(ImGuiMouseButton_Left)
        && m_zoneTool != ZoneTool::NewZone)
    {
        ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        if (drag.x * drag.x + drag.y * drag.y < 16.f) {
            glm::vec2 c = vp.toContent(io.MousePos.x, io.MousePos.y);
            const glm::vec2 m = bed.contentToMachine(c.x, c.y);
            if (m_zoneTool == ZoneTool::AddPosition && m_selectedZoneIdx >= 0
                && m_selectedZoneIdx < (int)m_zones.zones.size())
            {
                auto& z = m_zones.zones[m_selectedZoneIdx];
                plotter::ZonePosition p;
                p.x = m.x;
                p.y = m.y;
                p.positionIndex = (int)z.positions.size();
                p.label = "pos" + std::to_string(p.positionIndex);
                z.positions.push_back(p);
            } else {
                int hit = -1;
                for (int zi = 0; zi < (int)m_zones.zones.size(); ++zi) {
                    const auto& z = m_zones.zones[zi];
                    if (m.x >= z.x && m.x <= z.x + z.w && m.y >= z.y && m.y <= z.y + z.h) {
                        hit = zi;
                    }
                }
                m_selectedZoneIdx = hit;
                m_gcodeGen.setSelectedZoneIndex(hit);
                m_selPathEntity = entt::null;
                m_selPathIdx    = -1;
                ofkitty::runtime().select(entt::null);
                ofkitty::runtime().setWindowVisible("Properties", true);
            }
        }
    }

    // ---- Image overlay hit-test and transform handle (paper coords → content) ----
    if (m_showImageOverlay) {
        const glm::vec2 imgTLc = paperToContentMM(
            {m_engine.imageOverlayX, m_engine.imageOverlayY}, paperMM, paperOrg);
        const glm::vec2 imgBRc = paperToContentMM(
            {m_engine.imageOverlayX + m_engine.imageOverlayW,
             m_engine.imageOverlayY + m_engine.imageOverlayH},
            paperMM, paperOrg);
        const float iox = imgTLc.x, ioy = imgTLc.y;
        const float iow = imgBRc.x - imgTLc.x, ioh = imgBRc.y - imgTLc.y;
        if (iow > 0.f && ioh > 0.f) {
            ImVec2 imgTL = vp.toScreen(iox,       ioy);
            ImVec2 imgBR = vp.toScreen(iox + iow, ioy + ioh);

            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                ImVec2 mp   = ImGui::GetMousePos();
                ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                bool   tiny = drag.x * drag.x + drag.y * drag.y < 16.f;
                bool inside = mp.x >= imgTL.x && mp.x <= imgBR.x &&
                              mp.y >= imgTL.y && mp.y <= imgBR.y;
                if (tiny) m_imageSelected = inside;
            }
        }

        if (m_imageSelected && !m_imageOverlayLocked) {
            static ecs::TransformHandle2D s_imgHandle;
            ecs::Rect2D r { iox, ioy, iow, ioh };
            auto toScr = [&](float cx, float cy) -> ImVec2 { return vp.toScreen(cx, cy); };
            auto toCnt = [&](float sx, float sy) -> ImVec2 {
                auto c = vp.toContent(sx, sy); return { c.x, c.y };
            };
            s_imgHandle.draw(dl, r, toScr, toCnt);
            const glm::vec2 paperTL = contentToPaperMM({r.x, r.y}, paperMM, paperOrg);
            m_engine.imageOverlayX = paperTL.x;
            m_engine.imageOverlayY = paperTL.y;
            m_engine.imageOverlayW = r.w;
            m_engine.imageOverlayH = r.h;
        } else if (m_imageSelected && m_imageOverlayLocked) {
            const float iox2 = iox, ioy2 = ioy;
            const float iow2 = iow, ioh2 = ioh;
            ImVec2 sTL = vp.toScreen(iox2,        ioy2);
            ImVec2 sTR = vp.toScreen(iox2 + iow2, ioy2);
            ImVec2 sBR = vp.toScreen(iox2 + iow2, ioy2 + ioh2);
            ImVec2 sBL = vp.toScreen(iox2,         ioy2 + ioh2);
            dl->AddQuad(sTL, sTR, sBR, sBL, IM_COL32(200, 200, 200, 160), 1.f);
        }
    }

    // ---- Path selection on clean click ----
    if (vp.isCanvasHovered()
        && ImGui::IsMouseReleased(ImGuiMouseButton_Left)
        && !m_imageSelected
        && m_zoneTool == ZoneTool::Select)
    {
        ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        if (drag.x * drag.x + drag.y * drag.y < 16.f) {
        glm::vec2 mm  = vp.toContent(io.MousePos.x, io.MousePos.y);
        glm::vec2 pap = contentToPaperMM(mm, paperMM, paperOrg);
        float mmx = pap.x, mmy = pap.y;

        const auto& fentities  = m_engine.getFlatPathEntities();
        float bestDist2 = 100.f * 100.f / (zoom * zoom);
        int   bestFlat  = -1;
        for (int fi = 0; fi < (int)fpaths.size(); ++fi) {
            const auto& verts = fpaths[fi].getVertices();
            for (size_t vi = 0; vi + 1 < verts.size(); ++vi) {
                float ax = verts[vi].x, ay = verts[vi].y;
                float bx = verts[vi+1].x, by = verts[vi+1].y;
                float dx = bx - ax, dy = by - ay;
                float len2 = dx*dx + dy*dy;
                float t = len2 > 0 ? std::clamp(((mmx-ax)*dx+(mmy-ay)*dy)/len2, 0.f, 1.f) : 0.f;
                float ex = ax+t*dx-mmx, ey = ay+t*dy-mmy;
                float d2 = ex*ex + ey*ey;
                if (d2 < bestDist2) { bestDist2 = d2; bestFlat = fi; }
            }
        }

        if (bestFlat >= 0) {
            entt::entity layerEnt = (bestFlat < (int)fentities.size())
                                  ? fentities[bestFlat] : entt::null;
            int pathIdx = -1;
            if (m_engine.registry.valid(layerEnt)) {
                auto& pc = m_engine.registry.get<plotter::paths_component>(layerEnt);
                int offset = 0;
                for (int fi2 = 0; fi2 < bestFlat; ++fi2)
                    if (fi2 < (int)fentities.size() && fentities[fi2] == layerEnt)
                        ++offset;
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
                m_selectedZoneIdx  = -1;
                m_gcodeGen.setSelectedZoneIndex(-1);
                ofkitty::runtime().select(layerEnt);
                ofkitty::runtime().setWindowVisible("Properties", true);
            }
        } else {
            m_selPathEntity = entt::null;
            m_selPathIdx    = -1;
            ofkitty::runtime().select(entt::null);
        }
        } // end drag-check
    }   // end isCanvasHovered click block

    // ---- Inline path editor ----
    if (m_selPathEntity != entt::null && m_selPathIdx >= 0
        && m_engine.registry.valid(m_selPathEntity))
    {
        m_editConfig.canvasSize             = ImVec2(vp.canvasW(), vp.canvasH());
        m_editConfig.showGrid               = false;
        m_editConfig.allowKeyboardShortcuts = false;
        m_editConfig.style.backgroundColor  = IM_COL32(0, 0, 0, 0);
        m_editConfig.style.pathColor        = IM_COL32(255, 200, 50, 200);
        m_editConfig.style.previewColor     = IM_COL32(255, 200, 50, 80);
        m_editConfig.transform.zoom         = zoom;
        const glm::vec2 paperOrgScr = paperToContentMM({0.f, 0.f}, paperMM, paperOrg);
        m_editConfig.transform.pan          = ImVec2(vp._ox - vp._canvasOx + paperOrgScr.x * zoom,
                                                     vp._oy - vp._canvasOy + paperOrgScr.y * zoom);
        if (m_yAxisUp) m_editConfig.transform.pan.y += paperMM.y * zoom;
        ImGui::SetCursorScreenPos(vp.canvasOriginPx());
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

        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            m_selPathEntity = entt::null;
            m_selPathIdx    = -1;
            m_editPath.clear();
        }
    }

    // ---- Margin rectangle (paper space → content) ----
    if (m_showMargin && m_engine.marginMM > 0.f
        && paperMM.x > 2.f * m_engine.marginMM
        && paperMM.y > 2.f * m_engine.marginMM)
    {
        const float mm = m_engine.marginMM;
        const glm::vec2 mtl = paperToContentMM({mm, mm}, paperMM, paperOrg);
        const glm::vec2 mbr = paperToContentMM({paperMM.x - mm, paperMM.y - mm}, paperMM, paperOrg);
        ofkitty::drawMarginRect(
            dl,
            vp.toScreen(mtl.x, mtl.y),
            vp.toScreen(mbr.x, mbr.y),
            IM_COL32(m_marginColor.r, m_marginColor.g, m_marginColor.b, m_marginColor.a), 1.f);
    }
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
    auto opts = exportOptions();
    const std::string gcode = plotter::toGCode(m_engine, opts);
    if (opts.hasPipelineReport)
        m_gcodeGen.setLastPipelineReport(opts.lastPipelineReport, true);
    ofkitty::runtime().codeEditorSetText(gcode, TextEditor::LanguageDefinitionId::Gcode);
    ofkitty::runtime().setWindowVisible("Code Editor", true);
    m_sender.enqueueGCodeBlock(gcode);
}
