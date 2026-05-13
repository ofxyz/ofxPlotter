#include "ofApp.h"
#include "imgui.h"
#include <algorithm>

namespace {

ImTextureID imTextureIdFor(const ofTexture& tex)
{
    if (!tex.isAllocated()) {
        return (ImTextureID)0;
    }
    return (ImTextureID)(uintptr_t)tex.getTextureData().textureID;
}

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

    buildMenuBar();

    // ImGui dock space is created only in Edit mode (Ctrl/Cmd+E). Start with
    // Edit mode on so plotter panels can dock to the main dock space.
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
void ofApp::buildMenuBar()
{
    ofkitty::runtime().registerWindow({
        m_serialWin.name(), "View", true, false,
        [this](bool& visible) {
            ImGui::SetNextWindowPos(ImVec2(20, 40),   ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(360, 480), ImGuiCond_FirstUseEver);
            m_serialWin.draw(visible);
        }
    });

    ofkitty::runtime().registerWindow({
        "Image & Settings", "View", true, false,
        [this](bool& visible) {
            ImGui::SetNextWindowPos(ImVec2(400, 40),  ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(340, 700), ImGuiCond_FirstUseEver);
            drawSettingsPanel(visible);
        }
    });

    ofkitty::runtime().registerWindow({
        "Preview",
        "View",
        true,
        false,
        [this](bool& visible) {
            ImGui::SetNextWindowPos(ImVec2(760, 40), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(480, 700), ImGuiCond_FirstUseEver);
            drawPreviewPanel(visible);
        },
        "plotter_kit.preview",
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
            if (auto* w = ofkitty::runtime().findWindow(m_serialWin.name()))  w->visible = true;
            if (auto* w = ofkitty::runtime().findWindow("Image & Settings"))  w->visible = true;
            if (auto* w = ofkitty::runtime().findWindow("Preview"))           w->visible = true;
        }
    });
}

// ============================================================================
void ofApp::drawSettingsPanel(bool& visible)
{
    if (!ImGui::Begin("Image & Settings", &visible)) { ImGui::End(); return; }

    // ---- Image ----
    ImGui::SeparatorText("Image");
    if (ImGui::Button("Load image...")) {
        ofFileDialogResult r = ofSystemLoadDialog("Load image");
        if (r.bSuccess) {
            if (m_engine.loadImage(r.filePath)) {
                m_imageName = ofFilePath::getFileName(r.filePath);
                m_engine.rebuildFlatPaths();
                onPlotterSourceChanged();
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Load SVG...")) {
        ofFileDialogResult r = ofSystemLoadDialog("Load SVG");
        if (r.bSuccess) {
            if (m_engine.loadVectorSVG(r.filePath)) {
                m_imageName = ofFilePath::getFileName(r.filePath);
                onPlotterSourceChanged();
            }
        }
    }
    if (!m_imageName.empty())
        ImGui::TextDisabled("%s", m_imageName.c_str());

    // ---- Paper ----
    ImGui::SeparatorText("Paper");
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

    // ---- Preprocess ----
    ImGui::SeparatorText("Preprocess");
    ImGui::SliderFloat("Brightness",  &m_engine.preprocess.brightness, -1.f, 1.f);
    ImGui::SliderFloat("Contrast",    &m_engine.preprocess.contrast,   -1.f, 1.f);
    ImGui::SliderFloat("Threshold",   &m_engine.preprocess.threshold,  -1.f, 1.f);
    ImGui::Checkbox("Invert",         &m_engine.preprocess.invert);
    ImGui::SliderFloat("Blur",        &m_engine.preprocess.blur, 0.f, 10.f);
    ImGui::Checkbox("Edge detect",    &m_engine.preprocess.edgeDetect);

    // ---- PFM ----
    ImGui::SeparatorText("Path Finding");
    {
        static const char* kPFMs[] = {
            "Sketch Lines", "Cross-Hatch", "Spiral", "Stippling", "Contours"
        };
        int idx = (int)m_engine.getActiveLayer().pfmType;
        if (ImGui::Combo("PFM", &idx, kPFMs, IM_ARRAYSIZE(kPFMs)))
            m_engine.getActiveLayer().pfmType = (PFMType)idx;
    }

    PlotterLayer& layer = m_engine.getActiveLayer();
    ImGui::Indent();
    switch (layer.pfmType) {
        case PFMType::SketchLines: {
            auto& s = layer.sketchLines;
            ImGui::DragFloat("Min length",  &s.lineMinLength,  0.5f,  0.f, 200.f, "%.1f mm");
            ImGui::DragFloat("Max length",  &s.lineMaxLength,  0.5f,  0.f, 200.f, "%.1f mm");
            ImGui::DragFloat("Density",     &s.lineDensity,    1.f,   0.f, 200.f);
            ImGui::DragInt  ("Angle tests", &s.angleTests,     1,     1,   72);
            ImGui::Checkbox ("Lift pen",    &s.shouldLiftPen);
            break;
        }
        case PFMType::CrossHatch: {
            auto& s = layer.crossHatch;
            ImGui::DragFloat("Angle 1",        &s.angle1,        1.f, 0.f,  180.f, "%.0f°");
            ImGui::DragFloat("Angle 2",        &s.angle2,        1.f, 0.f,  180.f, "%.0f°");
            ImGui::Checkbox ("Second pass",    &s.useSecondary);
            ImGui::DragFloat("Line spacing",   &s.lineSpacing,   0.1f, 0.1f, 20.f, "%.1f mm");
            ImGui::DragFloat("Min brightness", &s.minBrightness, 0.01f, 0.f, 1.f);
            break;
        }
        case PFMType::Spiral: {
            auto& s = layer.spiral;
            ImGui::DragFloat("Ring spacing", &s.ringSpacing, 0.1f, 0.1f, 20.f, "%.1f mm");
            ImGui::DragFloat("Amplitude",    &s.amplitude,   0.1f, 0.f,  10.f);
            ImGui::DragFloat("Velocity",     &s.velocity,    0.1f, 0.1f, 20.f);
            ImGui::Checkbox ("Ignore white", &s.ignoreWhite);
            break;
        }
        case PFMType::Stippling: {
            auto& s = layer.stippling;
            ImGui::DragFloat("Spacing min", &s.dotSpacingMin, 0.05f, 0.1f, 20.f, "%.2f mm");
            ImGui::DragFloat("Spacing max", &s.dotSpacingMax, 0.05f, 0.1f, 20.f, "%.2f mm");
            ImGui::DragFloat("Dot radius",  &s.dotRadius,     0.01f, 0.1f,  5.f, "%.2f mm");
            ImGui::DragInt  ("Iterations",  &s.iterations,    1,     1,   500);
            break;
        }
        case PFMType::Contours: {
            auto& s = layer.contours;
            ImGui::DragFloat("Canny low",   &s.cannyLow,       1.f, 0.f, 255.f);
            ImGui::DragFloat("Canny high",  &s.cannyHigh,      1.f, 0.f, 255.f);
            ImGui::DragFloat("Min length",  &s.minContourLen,  0.5f, 0.f, 50.f, "%.1f mm");
            break;
        }
    }
    ImGui::Unindent();

    // ---- Pen / Machine ----
    ImGui::SeparatorText("Pen / Machine");
    ImGui::DragFloat("Pen down Z",   &m_engine.pen.penDownZ,    0.1f,  -20.f, 20.f,    "%.1f mm");
    ImGui::DragFloat("Pen up Z",     &m_engine.pen.penUpZ,      0.1f,    0.f, 40.f,    "%.1f mm");
    ImGui::DragFloat("Draw speed",   &m_engine.pen.drawSpeed,  10.f,  100.f, 10000.f, "%.0f mm/min");
    ImGui::DragFloat("Travel speed", &m_engine.pen.travelSpeed,10.f,  100.f, 10000.f, "%.0f mm/min");
    ImGui::Checkbox ("Slow travels", &m_engine.pen.slowTravels);

    // ---- Generate ----
    ImGui::SeparatorText("Generate");
    bool generating = m_generating.load();
    if (generating) {
        ImGui::ProgressBar(m_progress.load(), ImVec2(-1.f, 0.f), m_progressMsg.c_str());
    } else {
        bool canGenerate = m_engine.hasImage();
        if (!canGenerate) ImGui::BeginDisabled();
        if (ImGui::Button("Generate", ImVec2(-1.f, 0.f))) startGenerate();
        if (!canGenerate) ImGui::EndDisabled();
        if (!canGenerate) ImGui::TextDisabled("Load an image first.");
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

    ImGui::End();
}

// ============================================================================
void ofApp::drawPreviewPanel(bool& visible)
{
    constexpr ImGuiWindowFlags kWinFlags = ImGuiWindowFlags_MenuBar;
    if (!ImGui::Begin("Preview###plotter_kit.preview", &visible, kWinFlags)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("Preview")) {
            if (ImGui::MenuItem("Image", nullptr, m_previewMode == PlotterPreviewMode::Image)) {
                m_previewMode = PlotterPreviewMode::Image;
            }
            if (ImGui::MenuItem("Paths", nullptr, m_previewMode == PlotterPreviewMode::Paths)) {
                m_previewMode = PlotterPreviewMode::Paths;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    if (m_generating.load()) {
        ImGui::ProgressBar(m_progress.load(), ImVec2(-1.f, 0.f), m_progressMsg.c_str());
        ImGui::End();
        return;
    }

    if (m_previewMode == PlotterPreviewMode::Image) {
        const ofTexture* tex = previewRasterTextureOrNull();
        if (!tex) {
            ImGui::TextDisabled("No raster image — load a bitmap or generate from one.");
            ImGui::TextDisabled("SVG-only imports: use Paths preview.");
            ImGui::End();
            return;
        }

        ImVec2 avail = ImGui::GetContentRegionAvail();
        float tw = (float)tex->getWidth();
        float th = (float)tex->getHeight();
        float s = std::min(avail.x / tw, avail.y / th);
        if (s <= 0.f) s = 1.f;
        ImVec2 disp(tw * s, th * s);

        float padX = std::max(0.f, (avail.x - disp.x) * 0.5f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padX);
        ImTextureID tid = imTextureIdFor(*tex);
        // OpenGL origin is bottom-left; flip V so the preview matches on-screen orientation.
        ImGui::Image(tid, disp, ImVec2(0, 1), ImVec2(1, 0));
        ImGui::End();
        return;
    }

    const auto& paths = m_engine.getPaths();
    if (paths.empty()) {
        ImGui::TextDisabled("No paths — load an image and click Generate, or import SVG.");
        ImGui::End();
        return;
    }

    glm::vec2 paperMM = m_engine.getPaperSizeMM();
    ImVec2 avail = ImGui::GetContentRegionAvail();

    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("canvas##paths", avail);

    float scale = std::min(avail.x / paperMM.x, avail.y / paperMM.y);
    float ox = canvasPos.x + (avail.x - paperMM.x * scale) * 0.5f;
    float oy = canvasPos.y + (avail.y - paperMM.y * scale) * 0.5f;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(
        ImVec2(ox, oy),
        ImVec2(ox + paperMM.x * scale, oy + paperMM.y * scale),
        IM_COL32(255, 255, 255, 255));

    for (int i = 0; i < (int)paths.size(); ++i) {
        const auto& poly = paths[i];
        if (poly.size() < 2) continue;
        ofColor c = m_engine.getPathColor(i);
        ImU32 col = IM_COL32(c.r, c.g, c.b, 220);
        const auto& verts = poly.getVertices();
        for (int v = 1; v < (int)verts.size(); ++v) {
            dl->AddLine(
                ImVec2(ox + verts[v - 1].x * scale, oy + verts[v - 1].y * scale),
                ImVec2(ox + verts[v].x * scale, oy + verts[v].y * scale),
                col, 1.f);
        }
    }

    ImGui::End();
}

// ============================================================================
void ofApp::startGenerate()
{
    if (!m_engine.hasImage()) return;
    if (m_generating) return;

    if (m_generateThread.joinable()) m_generateThread.join();

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
        m_generating = false;
    });
}

// ============================================================================
void ofApp::sendToPlotter()
{
    if (m_engine.getPaths().empty()) return;
    m_sender.enqueueGCodeBlock(plotter::toGCode(m_engine));
}
