#pragma once

#include "ofMain.h"
#include "ofxKit.h"
#include "ofxPlotter.h"
#include "ofxGrbl.h"
#include "ofxGrblKit.h"

#include <atomic>
#include <thread>

enum class PlotterPreviewMode {
    Image,
    Paths,
};

/// Image-to-plotter example using ofxKit.
///
/// Workflow:
///   1. Load an image (or SVG) using the Image & Settings panel.
///   2. Pick a paper size, margins, and PFM with its settings.
///   3. Click Generate — Paths preview shows toolpaths; Image preview shows the
///      source bitmap until Generate finishes, then the post-process raster.
///   4. With Edit mode on (default here; Ctrl/Cmd+E toggles), dock panels in the
///      main layout. Use Preview ▸ Image or Preview ▸ Paths for the canvas mode.
///   5. Connect to the machine via the Serial / Machine panel.
///   6. Click Send to plotter.

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
    std::atomic<bool>    m_generating         {false};
    std::atomic<bool>    m_needsTextureUpload {false};
    std::atomic<float>   m_progress           {0.f};
    std::string          m_progressMsg;   ///< written only by generate thread
    std::string          m_imageName;     ///< display name of the loaded file
    std::thread          m_generateThread;

    // -----------------------------------------------------------------------
    // Machine
    // -----------------------------------------------------------------------
    grbl::GrblSender               m_sender;
    grbl::MachinePrefs             m_prefs;
    grbl::kit::PlotterSerialWindow m_serialWin;

    // -----------------------------------------------------------------------
    // UI
    // -----------------------------------------------------------------------
    void buildMenuBar();
    void drawSettingsPanel(bool& visible);
    void drawPreviewPanel(bool& visible);
    void onPlotterSourceChanged();
    const ofTexture* previewRasterTextureOrNull() const;

    PlotterPreviewMode m_previewMode {PlotterPreviewMode::Paths};
    bool               m_workingImageSynced {false};
    void startGenerate();
    void sendToPlotter();
};
