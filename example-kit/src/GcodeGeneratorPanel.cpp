#include "GcodeGeneratorPanel.h"
#include "ImageToPath.h"
#include "PlotterBedCoords.h"
#include "PlotterGCodeInjector.h"
#include "ofxGCode.hpp"
#include "imgui.h"
#include <cstring>
#include <MachinePrefs.h>

namespace {

const char* kZoneKinds[] = {"Cleaning", "Tool change", "Custom"};

} // namespace

void GcodeGeneratorPanel::refreshInjectionMarkers()
{
    rebuildInjectionMarkers();
}

void GcodeGeneratorPanel::rebuildInjectionMarkers()
{
    m_injectionMarkers.clear();
    if (!m_engine || !m_zones || m_zones->injectionRules.empty()) return;

    ofxGCode g;
    g.setup(1.0f);
    for (const auto& outline : m_engine->getPaths()) {
        if (outline.size() < 2) continue;
        std::vector<ofVec2f> pts;
        for (const auto& v : outline.getVertices())
            pts.push_back({v.x, v.y});
        g.polygon(pts, outline.isClosed());
    }

    grbl::MachinePrefs prefs;
    prefs.load();
    plotter::BedView bed = plotter::BedView::fromPrefs(prefs);
    const float ox = bed.bed.paperOriginX;
    const float oy = bed.bed.paperOriginY;
    for (auto& ln : g.lines) {
        ln.a.x += ox;
        ln.a.y += oy;
        ln.b.x += ox;
        ln.b.y += oy;
    }

    const auto breaks = plotter::computeInjectionBreaks(
        g.lines, m_zones->injectionRules, false);
    m_injectionMarkers = plotter::injectionMarkersContent(breaks, bed);
}

void GcodeGeneratorPanel::drawZonesTab()
{
    if (!m_zones) return;

    if (ImGui::Button("Add zone")) {
        plotter::MaintenanceZone z;
        z.id   = m_zones->makeUniqueZoneId();
        z.name = "Cleaning zone";
        z.kind = plotter::ZoneKind::Cleaning;
        if (m_engine) {
            grbl::MachinePrefs prefs;
            prefs.load();
            z.x = prefs.envelope.maxX - 60.f;
            z.y = prefs.envelope.minY + 10.f;
            z.w = 50.f;
            z.h = 80.f;
        }
        m_zones->zones.push_back(std::move(z));
        m_selectedZone = (int)m_zones->zones.size() - 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save zones")) {
        m_zones->save();
    }

    ImGui::Separator();

    for (int i = 0; i < (int)m_zones->zones.size(); ++i) {
        auto& z = m_zones->zones[i];
        const bool sel = m_selectedZone == i;
        if (ImGui::Selectable((z.name + "##zone" + std::to_string(i)).c_str(), sel)) {
            m_selectedZone = i;
        }
    }

    if (m_selectedZone < 0 || m_selectedZone >= (int)m_zones->zones.size()) {
        ImGui::TextDisabled("Select a zone to edit.");
        return;
    }

    drawZoneInspector();
}

void GcodeGeneratorPanel::drawZoneInspector()
{
    if (!m_zones || m_selectedZone < 0 || m_selectedZone >= (int)m_zones->zones.size())
        return;

    auto& z = m_zones->zones[m_selectedZone];
    char nameBuf[128];
    strncpy(nameBuf, z.name.c_str(), sizeof(nameBuf) - 1);
    nameBuf[sizeof(nameBuf) - 1] = '\0';
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
        z.name = nameBuf;

    int kindIdx = (int)z.kind;
    if (kindIdx < 0) kindIdx = 0;
    if (kindIdx > 2) kindIdx = 2;
    if (ImGui::Combo("Kind", &kindIdx, kZoneKinds, IM_ARRAYSIZE(kZoneKinds)))
        z.kind = (plotter::ZoneKind)kindIdx;

    ImGui::DragFloat4("Rect X/Y/W/H", &z.x, 0.5f, -500.f, 2000.f, "%.1f");
    ImGui::TextDisabled("Machine coordinates (mm)");

    auto paths = m_getSnippetPaths ? m_getSnippetPaths() : std::vector<std::string>{};
    if (!paths.empty()) {
        int cur = -1;
        for (int pi = 0; pi < (int)paths.size(); ++pi) {
            if (paths[pi] == z.snippetPath) { cur = pi; break; }
        }
        std::vector<const char*> labels;
        labels.reserve(paths.size() + 1);
        labels.push_back("(none)");
        for (const auto& p : paths)
            labels.push_back(ofFilePath::getFileName(p).c_str());
        int combo = cur < 0 ? 0 : cur + 1;
        if (ImGui::Combo("Snippet", &combo, labels.data(), (int)labels.size())) {
            z.snippetPath = combo == 0 ? std::string() : paths[combo - 1];
        }
    } else {
        char pathBuf[512];
        strncpy(pathBuf, z.snippetPath.c_str(), sizeof(pathBuf) - 1);
        pathBuf[sizeof(pathBuf) - 1] = '\0';
        if (ImGui::InputText("Snippet path", pathBuf, sizeof(pathBuf)))
            z.snippetPath = pathBuf;
    }

    ImGui::Separator();
    ImGui::Text("Positions (%d)", (int)z.positions.size());
    if (ImGui::Button("Add default position")) {
        plotter::ZonePosition p;
        p.x = z.x + z.w * 0.5f;
        p.y = z.y + z.h * 0.5f;
        p.positionIndex = (int)z.positions.size();
        p.label = "pos" + std::to_string(p.positionIndex);
        z.positions.push_back(p);
    }

    for (int pi = 0; pi < (int)z.positions.size(); ++pi) {
        ImGui::PushID(pi);
        auto& p = z.positions[pi];
        ImGui::DragFloat2("XY", &p.x, 0.5f);
        ImGui::InputInt("Index", &p.positionIndex);
        char lbl[64];
        strncpy(lbl, p.label.c_str(), sizeof(lbl) - 1);
        lbl[sizeof(lbl) - 1] = '\0';
        if (ImGui::InputText("Label", lbl, sizeof(lbl)))
            p.label = lbl;
        if (ImGui::Button("Remove position"))
            z.positions.erase(z.positions.begin() + pi);
        ImGui::PopID();
    }

    if (ImGui::Button("Delete zone", ImVec2(-1, 0))) {
        m_zones->zones.erase(m_zones->zones.begin() + m_selectedZone);
        m_selectedZone = -1;
    }
}

void GcodeGeneratorPanel::setLastPipelineReport(const plotproc::PipelineRunReport& report, bool valid)
{
    m_lastReport = report;
    m_hasPipelineReport = valid;
}

void GcodeGeneratorPanel::drawPipelineTab()
{
    if (!m_pipeline) {
        ImGui::TextDisabled("No pipeline attached.");
        return;
    }

    ImGui::Checkbox("Run pipeline on export", &m_runPipelineOnExport);
    ImGui::Checkbox("Write back to layers", &m_writeBackToPaths);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("After merge/sort, update paths_component (ofPath) on each layer.\n"
                          "Turn off to optimize G-code only without changing the document.");

    if (m_hasPipelineReport) {
        const float saved = m_lastReport.initial.travelLengthMM - m_lastReport.final.travelLengthMM;
        ImGui::Text("Travel before: %.1f mm", m_lastReport.initial.travelLengthMM);
        ImGui::Text("Travel after:  %.1f mm", m_lastReport.final.travelLengthMM);
        ImGui::Text("Travel saved:  %.1f mm", saved);
    }

    ImGui::Separator();
    ImGui::Text("Steps");

    int removeIdx = -1;
    for (int i = 0; i < (int)m_pipeline->steps.size(); ++i) {
        ImGui::PushID(i);
        auto& step = m_pipeline->steps[i];
        ImGui::Checkbox("##en", &step.enabled);
        ImGui::SameLine();
        ImGui::Text("%s", step.processorId.c_str());

        if (auto* proc = plotproc::ProcessorRegistry::instance().get(step.processorId)) {
            if (step.options.is_null() || (step.options.is_object() && step.options.empty()))
                step.options = proc->defaultOptions();
            if (step.processorId == "line_merge") {
                float tol = step.options.value("tolerance_mm", 0.05f);
                if (ImGui::DragFloat("Tolerance mm", &tol, 0.01f, 0.001f, 5.f))
                    step.options["tolerance_mm"] = tol;
            } else if (step.processorId == "line_sort") {
                bool twoOpt = step.options.value("two_opt", false);
                if (ImGui::Checkbox("2-opt", &twoOpt)) step.options["two_opt"] = twoOpt;
            } else if (step.processorId == "squiggles") {
                float amp = step.options.value("amplitude_mm", 0.5f);
                float period = step.options.value("period_mm", 3.f);
                if (ImGui::DragFloat("Amplitude", &amp, 0.05f, 0.f, 30.f))
                    step.options["amplitude_mm"] = amp;
                if (ImGui::DragFloat("Period", &period, 0.1f, 0.1f, 200.f))
                    step.options["period_mm"] = period;
            } else if (step.processorId == "filter") {
                float minL = step.options.value("min_length_mm", 0.f);
                if (ImGui::DragFloat("Min length mm", &minL, 0.1f, 0.f, 1000.f))
                    step.options["min_length_mm"] = minL;
            }
        }

        if (ImGui::Button("Remove")) removeIdx = i;
        ImGui::PopID();
    }
    if (removeIdx >= 0)
        m_pipeline->steps.erase(m_pipeline->steps.begin() + removeIdx);

    const auto ids = plotproc::ProcessorRegistry::instance().ids();
    static int addIdx = 0;
    if (!ids.empty()) {
        std::vector<const char*> labels;
        for (const auto& id : ids) labels.push_back(id.c_str());
        addIdx = std::min(addIdx, (int)labels.size() - 1);
        ImGui::Combo("Add processor", &addIdx, labels.data(), (int)labels.size());
        ImGui::SameLine();
        if (ImGui::Button("Add step") && addIdx >= 0 && addIdx < (int)ids.size()) {
            plotproc::PipelineStep step;
            step.processorId = ids[addIdx];
            step.enabled = true;
            if (auto* p = plotproc::ProcessorRegistry::instance().get(step.processorId))
                step.options = p->defaultOptions();
            m_pipeline->steps.push_back(step);
        }
    }

    if (ImGui::Button("Reset defaults"))
        *m_pipeline = plotproc::PlotPipeline::defaults();

    ImGui::Separator();
    if (ImGui::TreeNode("Advanced: vpype (optional)")) {
        ImGui::TextWrapped(
            "vpype is Python-based. Native merge/sort above covers most cases. "
            "Full vpype integration lives in the ofxVpype addon (subprocess now; "
            "ofxPython3 embed later). Add ofxVpype to addons.make to enable API calls.");
        ImGui::TextDisabled("Not linked in this example build.");
        ImGui::TreePop();
    }

    if (m_onRegenerate && ImGui::Button("Regenerate G-code", ImVec2(-1, 0)))
        m_onRegenerate();
}

void GcodeGeneratorPanel::drawInjectionTab()
{
    if (!m_zones) return;

    if (ImGui::Button("Add rule")) {
        plotter::InjectionRule r;
        if (!m_zones->zones.empty())
            r.zoneId = m_zones->zones.front().id;
        m_zones->injectionRules.push_back(r);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) m_zones->save();

    for (int ri = 0; ri < (int)m_zones->injectionRules.size(); ++ri) {
        ImGui::PushID(ri);
        auto& r = m_zones->injectionRules[ri];
        ImGui::Checkbox("##en", &r.enabled);
        ImGui::SameLine();

        int modeIdx = r.mode == plotter::InjectionMode::Inline ? 1 : 0;
        if (ImGui::Combo("Mode", &modeIdx, "Detour\0Inline\0"))
            r.mode = modeIdx == 1 ? plotter::InjectionMode::Inline : plotter::InjectionMode::Detour;

        ImGui::DragFloat("Every (mm)", &r.intervalMm, 5.f, 1.f, 100000.f, "%.0f");
        ImGui::Checkbox("Count travel", &r.countTravel);

        if (!m_zones->zones.empty()) {
            std::vector<const char*> names;
            int cur = 0;
            for (int zi = 0; zi < (int)m_zones->zones.size(); ++zi) {
                names.push_back(m_zones->zones[zi].name.c_str());
                if (m_zones->zones[zi].id == r.zoneId) cur = zi;
            }
            if (ImGui::Combo("Zone", &cur, names.data(), (int)names.size()))
                r.zoneId = m_zones->zones[cur].id;
        }

        ImGui::InputInt("Position index", &r.positionIndex);

        if (ImGui::Button("Remove rule"))
            m_zones->injectionRules.erase(m_zones->injectionRules.begin() + ri);
        ImGui::Separator();
        ImGui::PopID();
    }

    rebuildInjectionMarkers();
    ImGui::TextDisabled("Injection markers on preview: %d", (int)m_injectionMarkers.size());

    if (m_onRegenerate && ImGui::Button("Regenerate G-code", ImVec2(-1, 0)))
        m_onRegenerate();
}

void GcodeGeneratorPanel::draw(const char* title, bool& visible)
{
    ImGui::SetNextWindowSize(ImVec2(360, 480), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title, &visible)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("GcodeGenTabs")) {
        if (ImGui::BeginTabItem("Zones")) {
            drawZonesTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Injection")) {
            drawInjectionTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Pipeline")) {
            drawPipelineTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}
