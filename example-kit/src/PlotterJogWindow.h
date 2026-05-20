#pragma once

#include "ofMain.h"
#include "ofxGrbl.h"
#include <cstdint>
#include <string>

class ImageToPath;

/// Manual jog / pen control (ported from ofxBapp example_plotter).
class PlotterJogWindow {
public:
    void setEngine(ImageToPath* engine) { m_engine = engine; }
    void setSender(grbl::GrblSender* sender) { m_sender = sender; }
    void setPrefs(grbl::MachinePrefs* prefs) { m_prefs = prefs; }
    void setImguiWindowTitle(const std::string& title) { m_imguiTitle = title; }

    void resetMachineCoordinates();
    void update();

    glm::vec3 getMachinePosition() const { return {m_machX, m_machY, m_machZ}; }
    bool      isMachinePositionLive() const { return m_livePosFresh; }

    void draw(bool& visible);
    /// Compact XY jog + pen for the status bar (always visible in edit mode).
    void drawStatusBar();

    float jogStep       = 10.f;
    float jogFeed       = 3000.f;
    bool  jogContinuous = false;

private:
    enum class JogDir { None, XPlus, XMinus, YPlus, YMinus, ZPlus, ZMinus };
    void beginContinuousJog(JogDir dir);
    void endContinuousJog();
    void drawJogSection();
    void drawJogPad(float btnW, float btnH);
    void pollAndParseLiveStatus();
    void sendCommand(const std::string& cmd);

    static float clampDeltaAlongAxis(float current, float delta, float lo, float hi);

    ImageToPath*        m_engine = nullptr;
    grbl::GrblSender*   m_sender = nullptr;
    grbl::MachinePrefs* m_prefs  = nullptr;
    std::string         m_imguiTitle;

    JogDir m_activeJog = JogDir::None;
    float  m_machX = 0.f, m_machY = 0.f, m_machZ = 0.f;
    float  m_lastStatusQuerySec = 0.f;
    float  m_lastStatusReplySec = 0.f;
    uint32_t m_lastSeenStatusSeq = 0;
    bool   m_livePosFresh = false;
    float  statusPollIntervalSec = 0.1f;
    float  statusStaleAfterSec   = 1.0f;
};
