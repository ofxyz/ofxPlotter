#include "PlotterJogWindow.h"
#include "ImageToPath.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <sstream>

namespace {

bool parseGrblStatusLine(const std::string& line, float& x, float& y, float& z)
{
    if (line.empty() || line.front() != '<') return false;
    auto tryField = [&](const char* key) -> bool {
        size_t p = line.find(key);
        if (p == std::string::npos) return false;
        p += std::strlen(key);
        size_t end = line.find_first_of("|>", p);
        if (end == std::string::npos) return false;
        std::string body = line.substr(p, end - p);
        float px = 0, py = 0, pz = 0;
        int n = std::sscanf(body.c_str(), "%f,%f,%f", &px, &py, &pz);
        if (n < 2) return false;
        x = px;
        y = py;
        z = (n >= 3) ? pz : z;
        return true;
    };
    if (tryField("MPos:")) return true;
    if (tryField("WPos:")) return true;
    return false;
}

} // namespace

void PlotterJogWindow::resetMachineCoordinates()
{
    if (!m_prefs) return;
    m_machX = m_prefs->envelope.minX;
    m_machY = m_prefs->envelope.minY;
    m_machZ = m_engine ? m_engine->pen.penUpZ : 5.f;
    m_prefs->envelope.clampXYZ(m_machX, m_machY, m_machZ);
}

float PlotterJogWindow::clampDeltaAlongAxis(float current, float delta, float lo, float hi)
{
    float t = current + delta;
    if (t < lo) return lo - current;
    if (t > hi) return hi - current;
    return delta;
}

void PlotterJogWindow::sendCommand(const std::string& cmd)
{
    if (cmd.empty() || !m_sender || !m_sender->isConnected()) return;
    std::string c = cmd;
    size_t start = c.find_first_not_of(" \t");
    if (start != std::string::npos && c[start] == '?') {
        m_sender->sendRealtimeStatusQuery();
        return;
    }
    m_sender->enqueueLine(cmd);
}

void PlotterJogWindow::pollAndParseLiveStatus()
{
    if (!m_sender || !m_sender->isConnected()) {
        m_livePosFresh = false;
        return;
    }

    const float nowSec = ofGetElapsedTimef();
    if (nowSec - m_lastStatusQuerySec >= std::max(0.02f, statusPollIntervalSec)) {
        m_sender->sendRealtimeStatusQuery();
        m_lastStatusQuerySec = nowSec;
    }

    const uint32_t seq = m_sender->getStatusReportSeq();
    if (seq != m_lastSeenStatusSeq) {
        m_lastSeenStatusSeq = seq;
        const std::string report = m_sender->getLastStatusReport();
        float x = m_machX, y = m_machY, z = m_machZ;
        if (parseGrblStatusLine(report, x, y, z)) {
            m_lastStatusReplySec = nowSec;
            m_machX = x;
            m_machY = y;
            m_machZ = z;
        }
    }

    m_livePosFresh = (nowSec - m_lastStatusReplySec) < std::max(0.05f, statusStaleAfterSec);
}

void PlotterJogWindow::update()
{
    pollAndParseLiveStatus();
}

void PlotterJogWindow::beginContinuousJog(JogDir dir)
{
    if (!m_sender || !m_sender->isConnected() || dir == JogDir::None) return;
    if (m_activeJog != JogDir::None) return;

    float reach = 500.f;
    if (m_prefs) {
        reach = std::max({
            m_prefs->envelope.maxX - m_prefs->envelope.minX,
            m_prefs->envelope.maxY - m_prefs->envelope.minY,
            m_prefs->envelope.maxZ - m_prefs->envelope.minZ,
            100.f});
    }

    const float feed = std::max(10.f, jogFeed);
    std::ostringstream cmd;
    cmd << "$J=G91 G21 ";
    switch (dir) {
        case JogDir::XPlus:  cmd << "X" << reach; break;
        case JogDir::XMinus: cmd << "X-" << reach; break;
        case JogDir::YPlus:  cmd << "Y" << reach; break;
        case JogDir::YMinus: cmd << "Y-" << reach; break;
        case JogDir::ZPlus:  cmd << "Z" << reach; break;
        case JogDir::ZMinus: cmd << "Z-" << reach; break;
        default: return;
    }
    cmd << " F" << (int)feed;

    if (m_sender->pendingLines() == 0)
        m_sender->enqueueLine(cmd.str());
    m_activeJog = dir;
}

void PlotterJogWindow::endContinuousJog()
{
    if (m_activeJog == JogDir::None) return;
    if (m_sender && m_sender->isConnected())
        m_sender->sendJogCancel();
    m_activeJog = JogDir::None;
}

void PlotterJogWindow::drawJogSection()
{
    if (!ImGui::CollapsingHeader("Jog Control", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    const bool haveEnv = m_prefs != nullptr;
    const bool connected = m_sender && m_sender->isConnected();

    if (!connected) {
        ImGui::TextDisabled("Connect in Serial / Machine to jog.");
        return;
    }

    ImGui::Text("Position: X %.2f  Y %.2f  Z %.2f%s",
                m_machX, m_machY, m_machZ,
                m_livePosFresh ? " (live)" : "");

    const float preBtnW = ImGui::GetFontSize() * 3.2f;
    const float preBtnH = ImGui::GetFrameHeight();
    auto stepBtn = [&](const char* lbl, float v) {
        const bool active = std::fabs(jogStep - v) < 1e-3f;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.45f, 0.70f, 1.f));
        if (ImGui::Button(lbl, ImVec2(preBtnW, preBtnH))) jogStep = v;
        if (active) ImGui::PopStyleColor();
        ImGui::SameLine();
    };
    stepBtn("0.1", 0.1f);
    stepBtn("1", 1.f);
    stepBtn("10", 10.f);
    stepBtn("50", 50.f);
    ImGui::NewLine();

    ImGui::DragFloat("Step (mm)", &jogStep, 0.5f, 0.01f, 500.f, "%.2f");
    ImGui::DragFloat("Jog feed (mm/min)", &jogFeed, 50.f, 10.f, 30000.f, "%.0f");
    ImGui::Checkbox("Hold to jog (continuous)", &jogContinuous);
    ImGui::Spacing();

    const float fs   = ImGui::GetFontSize();
    const float btnW = fs * 4.5f;
    const float btnH = fs * 3.f;
    drawJogPad(btnW, btnH);

    const float halfW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    const float penRowH = ImGui::GetFrameHeight() * 1.3f;
    if (ImGui::Button("Pen Up", ImVec2(halfW, penRowH))) {
        float z = m_engine ? m_engine->pen.penUpZ : 5.f;
        if (haveEnv) z = std::clamp(z, m_prefs->envelope.minZ, m_prefs->envelope.maxZ);
        sendCommand("G0 Z" + std::to_string(z));
        m_machZ = z;
    }
    ImGui::SameLine();
    if (ImGui::Button("Pen Down", ImVec2(halfW, penRowH))) {
        float z = m_engine ? m_engine->pen.penDownZ : 0.f;
        if (haveEnv) z = std::clamp(z, m_prefs->envelope.minZ, m_prefs->envelope.maxZ);
        sendCommand("G1 Z" + std::to_string(z) + " F1000");
        m_machZ = z;
    }
}

void PlotterJogWindow::drawJogPad(float btnW, float btnH)
{
    const bool haveEnv = m_prefs != nullptr;
    const float gap    = ImGui::GetStyle().ItemSpacing.x;

    auto jogY = [&](float dy) {
        if (!haveEnv) return;
        float d = clampDeltaAlongAxis(m_machY, dy, m_prefs->envelope.minY, m_prefs->envelope.maxY);
        if (std::fabs(d) < 1e-5f) return;
        sendCommand("G91");
        sendCommand("G0 Y" + std::to_string(d));
        sendCommand("G90");
        m_machY += d;
    };

    auto jogX = [&](float dx) {
        if (!haveEnv) return;
        float d = clampDeltaAlongAxis(m_machX, dx, m_prefs->envelope.minX, m_prefs->envelope.maxX);
        if (std::fabs(d) < 1e-5f) return;
        sendCommand("G91");
        sendCommand("G0 X" + std::to_string(d));
        sendCommand("G90");
        m_machX += d;
    };

    auto jogButton = [&](const char* label, JogDir dir, std::function<void()> stepFn) {
        ImGui::Button(label, ImVec2(btnW, btnH));
        if (jogContinuous) {
            if (ImGui::IsItemActivated()) beginContinuousJog(dir);
        } else if (ImGui::IsItemActivated() && stepFn) {
            stepFn();
        }
        if (ImGui::IsItemDeactivated() && m_activeJog == dir)
            endContinuousJog();
    };

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + btnW + gap);
    jogButton("Up", JogDir::YPlus, [&] { jogY(jogStep); });
    jogButton("Left", JogDir::XMinus, [&] { jogX(-jogStep); });
    ImGui::SameLine();
    if (ImGui::Button("Home", ImVec2(btnW, btnH)))
        ImGui::OpenPopup("plotter_kit_g28_warn");
    if (ImGui::BeginPopupModal("plotter_kit_g28_warn", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(
            "G28 seeks machine origin. Many plotters have no limit switches.");
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (ImGui::Button("Send G28", ImVec2(120, 0))) {
            sendCommand("G28");
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    jogButton("Right", JogDir::XPlus, [&] { jogX(jogStep); });
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + btnW + gap);
    jogButton("Down", JogDir::YMinus, [&] { jogY(-jogStep); });
}

void PlotterJogWindow::drawStatusBar()
{
    const bool connected = m_sender && m_sender->isConnected();
    const bool haveEnv   = m_prefs != nullptr;
    if (!connected) {
        ImGui::TextDisabled("Jog: not connected");
        return;
    }

    auto jogX = [&](float dx) {
        if (!haveEnv) return;
        float d = clampDeltaAlongAxis(m_machX, dx, m_prefs->envelope.minX, m_prefs->envelope.maxX);
        if (std::fabs(d) < 1e-5f) return;
        sendCommand("G91");
        sendCommand("G0 X" + std::to_string(d));
        sendCommand("G90");
        m_machX += d;
    };
    auto jogY = [&](float dy) {
        if (!haveEnv) return;
        float d = clampDeltaAlongAxis(m_machY, dy, m_prefs->envelope.minY, m_prefs->envelope.maxY);
        if (std::fabs(d) < 1e-5f) return;
        sendCommand("G91");
        sendCommand("G0 Y" + std::to_string(d));
        sendCommand("G90");
        m_machY += d;
    };

    ImGui::TextDisabled("Jog");
    ImGui::SameLine();
    if (ImGui::SmallButton("Y+")) jogY(jogStep);
    ImGui::SameLine();
    if (ImGui::SmallButton("X-")) jogX(-jogStep);
    ImGui::SameLine();
    if (ImGui::SmallButton("X+")) jogX(jogStep);
    ImGui::SameLine();
    if (ImGui::SmallButton("Y-")) jogY(-jogStep);

    ImGui::SameLine(0, 8.f);
    if (ImGui::SmallButton("Z+")) {
        float z = m_engine ? m_engine->pen.penUpZ : 5.f;
        if (m_prefs) z = std::clamp(z, m_prefs->envelope.minZ, m_prefs->envelope.maxZ);
        sendCommand("G0 Z" + std::to_string(z));
        m_machZ = z;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pen up (Z)");
    ImGui::SameLine();
    if (ImGui::SmallButton("Z-")) {
        float z = m_engine ? m_engine->pen.penDownZ : 0.f;
        if (m_prefs) z = std::clamp(z, m_prefs->envelope.minZ, m_prefs->envelope.maxZ);
        sendCommand("G1 Z" + std::to_string(z) + " F1000");
        m_machZ = z;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pen down (Z)");
}

void PlotterJogWindow::draw(bool& visible)
{
    ImGui::SetNextWindowSize(ImVec2(340, 420), ImGuiCond_FirstUseEver);
    const char* title = m_imguiTitle.empty() ? "Jog Control" : m_imguiTitle.c_str();
    if (!ImGui::Begin(title, &visible)) {
        ImGui::End();
        return;
    }
    drawJogSection();
    ImGui::End();
}
