#include "PlotPreviewDraw.h"
#include "ImageToPath.h"
#include "ofMain.h"
#include <algorithm>

namespace plotPreview {

void drawPaperGrid(float paperW, float paperH, float contentZoom)
{
    const float lineW = 0.2f;

    if (contentZoom > 3.f) {
        ofSetColor(200, 220, 240, 60);
        ofSetLineWidth(lineW * 0.5f);
        for (float x = 0.f; x <= paperW; x += 1.f) ofDrawLine(x, 0.f, x, paperH);
        for (float y = 0.f; y <= paperH; y += 1.f) ofDrawLine(0.f, y, paperW, y);
    }

    ofSetColor(180, 205, 230, 100);
    ofSetLineWidth(lineW);
    for (float x = 0.f; x <= paperW; x += 10.f) ofDrawLine(x, 0.f, x, paperH);
    for (float y = 0.f; y <= paperH; y += 10.f) ofDrawLine(0.f, y, paperW, y);

    ofSetColor(150, 185, 215, 140);
    ofSetLineWidth(lineW * 2.f);
    for (float x = 0.f; x <= paperW; x += 50.f) ofDrawLine(x, 0.f, x, paperH);
    for (float y = 0.f; y <= paperH; y += 50.f) ofDrawLine(0.f, y, paperW, y);

    ofSetColor(200, 100, 100, 80);
    ofSetLineWidth(lineW * 1.5f);
    ofDrawLine(paperW * 0.5f, 0.f, paperW * 0.5f, paperH);
    ofDrawLine(0.f, paperH * 0.5f, paperW, paperH * 0.5f);

    ofSetColor(220, 60, 60, 180);
    ofSetLineWidth(lineW * 3.f);
    const float ms = 5.f;
    ofDrawLine(0.f, 0.f, ms, 0.f);
    ofSetColor(60, 220, 60, 180);
    ofDrawLine(0.f, 0.f, 0.f, ms);

    ofSetLineWidth(1.f);
}

void drawBrushEstimation(const ImageToPath& engine, int maxPathIndex)
{
    const auto& paths = engine.getPaths();
    if (paths.empty() || maxPathIndex <= 0) return;

    const int maxPath = std::min(maxPathIndex, (int)paths.size());
    const float r = std::max(0.05f, engine.pen.penWidth * 0.5f);

    for (int p = 0; p < maxPath; ++p) {
        const auto& path = paths[p];
        if (path.size() < 1) continue;
        ofColor layerCol = engine.getPathColor(p);
        if (layerCol.a == 0) layerCol = ofColor(0);
        ofSetColor(layerCol.r, layerCol.g, layerCol.b, 60);
        for (const auto& v : path.getVertices())
            ofDrawCircle(v.x, v.y, r);
    }
}

} // namespace plotPreview
