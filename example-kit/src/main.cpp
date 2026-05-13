#include "ofMain.h"
#include "ofApp.h"
#include <ofxKit.h>

int main()
{
    ofGLWindowSettings settings;
    settings.setSize(1280, 800);
    settings.windowMode = OF_WINDOW;
    auto window = ofCreateWindow(settings);
    auto app    = std::make_shared<ofApp>();
    ofkitty::Runtime::attach(window, app);
    ofRunApp(window, std::move(app));
    ofRunMainLoop();
}
