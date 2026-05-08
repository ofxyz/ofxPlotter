#ifndef NOMINMAX
#define NOMINMAX
#endif
#define _USE_MATH_DEFINES
#include <cmath>

#include "ImageToPath.h"
#include "ofxSvg.h"
#include <algorithm>
#include <random>
#include <limits>

// Paper size lookup (width x height in mm, portrait orientation)
static glm::vec2 paperDimensionsMM(PaperSize size) {
    switch (size) {
        case PaperSize::A4: return { 210.0f, 297.0f };
        case PaperSize::A3: return { 297.0f, 420.0f };
        case PaperSize::A2: return { 420.0f, 594.0f };
        case PaperSize::A1: return { 594.0f, 841.0f };
        case PaperSize::A0: return { 841.0f, 1189.0f };
        default:            return { 210.0f, 297.0f };
    }
}

ImageToPath::ImageToPath() {
    // Create default brush library
    brushes.push_back({"0.3mm Round",  BrushShape::Round,  0.3f, 0, 0.3f, 0, ofColor(0)});
    brushes.push_back({"0.5mm Round",  BrushShape::Round,  0.5f, 0, 0.3f, 0, ofColor(0)});
    brushes.push_back({"1.0mm Round",  BrushShape::Round,  1.0f, 0, 0.3f, 0, ofColor(0)});
    brushes.push_back({"0.5mm Square", BrushShape::Square, 0.5f, 0, 0.3f, 0, ofColor(0)});
    brushes.push_back({"1.5mm Flat",   BrushShape::Flat,   1.5f, 45.0f, 0.2f, 0, ofColor(0)});
    brushes.push_back({"2.0mm Nib",    BrushShape::Nib,    2.0f, 30.0f, 0.15f, 0, ofColor(0)});

    // Create default layer
    addLayer("Layer 1");
}

PlotterLayer& ImageToPath::getActiveLayer() {
    if (layers.empty()) addLayer("Layer 1");
    currentLayer = std::clamp(currentLayer, 0, (int)layers.size() - 1);
    return layers[currentLayer];
}

const PlotterLayer& ImageToPath::getActiveLayer() const {
    return layers[std::clamp(currentLayer, 0, std::max(0, (int)layers.size() - 1))];
}

int ImageToPath::addLayer(const std::string& name) {
    PlotterLayer layer;
    layer.name = name.empty() ? ("Layer " + std::to_string(layers.size() + 1)) : name;
    layer.brushIndex = 0;
    layer.color = ofColor(0);
    layers.push_back(layer);
    return (int)layers.size() - 1;
}

void ImageToPath::removeLayer(int idx) {
    if (idx < 0 || idx >= (int)layers.size()) return;
    if (layers.size() <= 1) return; // Keep at least one layer
    layers.erase(layers.begin() + idx);
    if (currentLayer >= (int)layers.size()) currentLayer = (int)layers.size() - 1;
}

int ImageToPath::addBrush(const BrushPreset& b) {
    brushes.push_back(b);
    return (int)brushes.size() - 1;
}

void ImageToPath::rebuildFlatPaths() {
    m_flatPaths.clear();
    m_flatPathLayerIdx.clear();
    for (int i = 0; i < (int)layers.size(); i++) {
        if (!layers[i].visible) continue;
        for (const auto& p : layers[i].paths) {
            m_flatPaths.push_back(p);
            m_flatPathLayerIdx.push_back(i);
        }
    }
}

ofColor ImageToPath::getPathColor(int flatIdx) const {
    if (flatIdx < 0 || flatIdx >= (int)m_flatPathLayerIdx.size()) return ofColor(0);
    int li = m_flatPathLayerIdx[flatIdx];
    if (li < 0 || li >= (int)layers.size()) return ofColor(0);
    return layers[li].color;
}

glm::vec2 ImageToPath::getPaperSizeMM() const {
    glm::vec2 dim;
    if (paperSize == PaperSize::Custom) {
        dim = { customWidth, customHeight };
    } else {
        dim = paperDimensionsMM(paperSize);
    }
    if (paperOrientation == PaperOrientation::Landscape) {
        dim = { std::max(dim.x, dim.y), std::min(dim.x, dim.y) };
    } else {
        dim = { std::min(dim.x, dim.y), std::max(dim.x, dim.y) };
    }
    return dim;
}

bool ImageToPath::loadImage(const std::string& path) {
    if (!m_sourceImage.load(path)) {
        ofLogError("ImageToPath") << "Failed to load image: " << path;
        return false;
    }
    m_sourceLoaded = true;
    ofLogNotice("ImageToPath") << "Loaded image: " << path
        << " (" << m_sourceImage.getWidth() << "x" << m_sourceImage.getHeight() << ")";
    return true;
}

void ImageToPath::setImage(const ofPixels& pixels) {
    m_sourceImage.setFromPixels(pixels);
    m_sourceLoaded = true;
}

bool ImageToPath::loadVectorSVG(const std::string& path) {
    ofxSvg svg;
    if (!svg.load(path)) {
        ofLogError("ImageToPath") << "Failed to load SVG: " << path;
        return false;
    }

    // Gather all path outlines as polylines in SVG user-unit space.
    // ofPath::getOutline() returns one polyline per sub-path, curves tessellated.
    std::vector<ofPolyline> svgPolys;
    const auto& ofPaths = svg.getPaths();
    for (const auto& op : ofPaths) {
        const auto& outlines = op.getOutline();
        for (const auto& pl : outlines) {
            if (pl.size() >= 2) svgPolys.push_back(pl);
        }
    }

    if (svgPolys.empty()) {
        ofLogWarning("ImageToPath") << "SVG has no drawable paths: " << path;
        return false;
    }

    // Compute content bounding box in SVG units. Prefer the viewbox when it's
    // sensible, otherwise fall back to the actual polyline extents -- lots of
    // designer-made SVGs have oversized viewboxes with content clustered in
    // one corner, and fitting that to paper wastes the drawable area.
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = -std::numeric_limits<float>::max();
    float maxY = -std::numeric_limits<float>::max();
    for (const auto& pl : svgPolys) {
        for (const auto& v : pl.getVertices()) {
            minX = std::min(minX, v.x);
            minY = std::min(minY, v.y);
            maxX = std::max(maxX, v.x);
            maxY = std::max(maxY, v.y);
        }
    }
    float contentW = std::max(1e-4f, maxX - minX);
    float contentH = std::max(1e-4f, maxY - minY);

    // Fit to paper drawable area, preserving aspect ratio.
    glm::vec2 paper = getPaperSizeMM();
    float availW = std::max(1.0f, paper.x - 2.0f * marginMM);
    float availH = std::max(1.0f, paper.y - 2.0f * marginMM);
    float scale = std::min(availW / contentW, availH / contentH);
    float drawW = contentW * scale;
    float drawH = contentH * scale;
    float offX  = marginMM + (availW - drawW) * 0.5f;
    float offY  = marginMM + (availH - drawH) * 0.5f;

    // Stash the same working dimensions the raster pipeline uses so pixelToMM
    // / sampleBrightness still return sane values if anything queries them,
    // and so the viewer's overlay logic can use a consistent draw rect.
    m_drawWidth   = drawW;
    m_drawHeight  = drawH;
    m_drawOffsetX = offX;
    m_drawOffsetY = offY;
    m_workW       = (int)std::max(1.0f, contentW);
    m_workH       = (int)std::max(1.0f, contentH);

    // Replace the active layer's paths with the SVG content in mm.
    PlotterLayer& layer = getActiveLayer();
    layer.paths.clear();
    layer.paths.reserve(svgPolys.size());
    for (const auto& pl : svgPolys) {
        ofPolyline out;
        out.getVertices().reserve(pl.size());
        for (const auto& v : pl.getVertices()) {
            float mmX = offX + (v.x - minX) * scale;
            float mmY = offY + (v.y - minY) * scale;
            out.addVertex(mmX, mmY, 0);
        }
        if (pl.isClosed()) out.setClosed(true);
        layer.paths.push_back(std::move(out));
    }

    layer.totalPaths = (int)layer.paths.size();
    layer.totalPoints = 0;
    layer.totalDistance = 0;
    for (const auto& p : layer.paths) {
        layer.totalPoints += (int)p.size();
        layer.totalDistance += p.getPerimeter();
    }
    layer.estimatedTime = layer.totalDistance / std::max(1.0f, pen.drawSpeed) * 60.0f;

    rebuildFlatPaths();
    computeStats();

    ofLogNotice("ImageToPath") << "Imported SVG: " << path
        << " -> " << layer.paths.size() << " polylines, "
        << (int)layer.totalDistance << " mm";
    return true;
}

glm::vec2 ImageToPath::pixelToMM(float px, float py) const {
    if (m_workW <= 0 || m_workH <= 0) return { 0, 0 };
    float mmX = m_drawOffsetX + (px / (float)m_workW) * m_drawWidth;
    float mmY = m_drawOffsetY + (py / (float)m_workH) * m_drawHeight;
    return { mmX, mmY };
}

float ImageToPath::sampleBrightness(float mmX, float mmY) const {
    if (m_workW <= 0 || m_workH <= 0) return 1.0f;
    int px = (int)((mmX - m_drawOffsetX) / m_drawWidth * m_workW);
    int py = (int)((mmY - m_drawOffsetY) / m_drawHeight * m_workH);
    px = std::clamp(px, 0, m_workW - 1);
    py = std::clamp(py, 0, m_workH - 1);
    return m_workingPixels[py * m_workW + px] / 255.0f;
}

// =============================================================
// Preprocessing
// =============================================================

void ImageToPath::preprocessImage() {
    if (!m_sourceLoaded) return;

    // Work entirely in ofPixels — no GPU uploads, safe to call from any thread.
    // m_workingImage is updated via setWorkingPixels() after generation completes,
    // which must be called from the main thread if a texture display is needed.
    ofPixels pix = m_sourceImage.getPixels();
    pix.setImageType(OF_IMAGE_GRAYSCALE);

    // Apply resolution scaling
    float res = std::clamp(getActiveLayer().sketchLines.plotResolution, 0.1f, 2.0f);
    if (res < 0.99f) {
        pix.resize(
            (int)(pix.getWidth() * res),
            (int)(pix.getHeight() * res));
    }

    int w = (int)pix.getWidth();
    int h = (int)pix.getHeight();

    // Brightness / contrast
    if (preprocess.brightness != 0.0f || preprocess.contrast != 0.0f) {
        float b = preprocess.brightness * 128.0f;
        float c = 1.0f + preprocess.contrast;
        for (int i = 0; i < w * h; i++) {
            float v = pix[i];
            v = (v - 128.0f) * c + 128.0f + b;
            pix[i] = (unsigned char)std::clamp(v, 0.0f, 255.0f);
        }
    }

    // Invert
    if (preprocess.invert) {
        for (int i = 0; i < w * h; i++)
            pix[i] = 255 - pix[i];
    }

    // Threshold (threshold field: -1 = off, 0–1 normalised)
    if (preprocess.threshold >= 0.0f) {
        auto t = (unsigned char)(preprocess.threshold * 255.0f);
        for (int i = 0; i < w * h; i++)
            pix[i] = (pix[i] >= t) ? 255 : 0;
    }

    // Store processed pixels for PFM use — no GL calls
    m_workingPixels = pix;
    m_workW = w;
    m_workH = h;

    // Compute drawing area: fit image into paper with margin, preserving aspect ratio
    glm::vec2 paper = getPaperSizeMM();
    float availW = paper.x - 2.0f * marginMM;
    float availH = paper.y - 2.0f * marginMM;

    float imgAspect = (float)w / (float)h;
    float areaAspect = availW / availH;

    if (imgAspect > areaAspect) {
        m_drawWidth  = availW;
        m_drawHeight = availW / imgAspect;
    } else {
        m_drawHeight = availH;
        m_drawWidth  = availH * imgAspect;
    }

    // Centre on paper
    m_drawOffsetX = marginMM + (availW - m_drawWidth)  * 0.5f;
    m_drawOffsetY = marginMM + (availH - m_drawHeight) * 0.5f;
}

// =============================================================
// Main generate entry point
// =============================================================

void ImageToPath::generateLayer(int layerIdx,
    std::function<void(float, const std::string&)> onProgress) {
    if (!m_sourceLoaded || layerIdx < 0 || layerIdx >= (int)layers.size()) return;
    int prevLayer = currentLayer;
    currentLayer = layerIdx;
    preprocessImage();
    runLayerGeneration(layers[layerIdx], onProgress);
    currentLayer = prevLayer;
    rebuildFlatPaths();
    computeStats();
}

void ImageToPath::runLayerGeneration(PlotterLayer& layer,
    std::function<void(float, const std::string&)> onProgress) {
    // PFM methods push into m_paths (scratch buffer).
    // We also need to temporarily swap in the layer's PFM-specific settings
    // so the existing PFM code reads the right parameters.
    m_paths.clear();

    // Temporarily swap this layer's PFM settings into the "active" position
    // The PFM methods read sketchLines/crossHatch/etc from getActiveLayer()
    // but they actually access member fields directly. We swap the settings
    // into the active layer slot temporarily.
    int prevCurrent = currentLayer;
    for (int i = 0; i < (int)layers.size(); i++) {
        if (&layers[i] == &layer) { currentLayer = i; break; }
    }

    switch (layer.pfmType) {
        case PFMType::SketchLines:  runSketchLines(onProgress);  break;
        case PFMType::CrossHatch:   runCrossHatch(onProgress);   break;
        case PFMType::Spiral:       runSpiral(onProgress);       break;
        case PFMType::Stippling:    runStippling(onProgress);    break;
        case PFMType::Contours:     runContours(onProgress);     break;
    }

    currentLayer = prevCurrent;

    // Move generated paths into the layer
    layer.paths = std::move(m_paths);
    m_paths.clear();

    // Per-layer stats
    layer.totalPaths = (int)layer.paths.size();
    layer.totalPoints = 0;
    layer.totalDistance = 0;
    for (const auto& p : layer.paths) {
        layer.totalPoints += (int)p.size();
        layer.totalDistance += p.getPerimeter();
    }
    layer.estimatedTime = layer.totalDistance / pen.drawSpeed * 60.0f;
}

void ImageToPath::generate(std::function<void(float, const std::string&)> onProgress) {
    if (!m_sourceLoaded) {
        ofLogWarning("ImageToPath") << "No image loaded";
        return;
    }

    if (onProgress) onProgress(0.0f, "Preprocessing...");
    preprocessImage();

    int numLayers = (int)layers.size();
    for (int i = 0; i < numLayers; i++) {
        if (!layers[i].visible) continue;
        float layerBase = 0.1f + 0.85f * ((float)i / (float)numLayers);
        float layerEnd  = 0.1f + 0.85f * ((float)(i + 1) / (float)numLayers);

        if (onProgress) onProgress(layerBase,
            "Layer " + std::to_string(i + 1) + "/" + std::to_string(numLayers) + "...");

        // Wrap progress callback to remap per-layer 0..1 to overall range
        auto layerProgress = [&](float p, const std::string& msg) {
            if (onProgress)
                onProgress(layerBase + (layerEnd - layerBase) * p, msg);
        };
        runLayerGeneration(layers[i], layerProgress);
    }

    rebuildFlatPaths();

    if (onProgress) onProgress(0.97f, "Computing stats...");
    computeStats();
    
    if (onProgress) onProgress(1.0f, "Done");
    
    ofLogNotice("ImageToPath") << "Generated " << totalPaths << " paths, "
        << totalPoints << " points, " << (int)totalDistance << " mm total distance";
}

// =============================================================
// Sketch Lines PFM  (ported from DrawingBotV3 AbstractSketchPFM)
// =============================================================

// Block-based darkest area search (DrawingBotV3: findDarkestArea)
// Divides image into blocks, finds the block with the lowest average
// luminance, then returns the darkest pixel within that block.
static void findDarkestArea(const std::vector<int>& lum, int w, int h, int blockW, int blockH, int& outX, int& outY) {
    int blocksX = w / blockW;
    int blocksY = h / blockH;
    if (blocksX < 1) blocksX = 1;
    if (blocksY < 1) blocksY = 1;
    
    float bestBlockAvg = 1e9f;
    int bestPx = 0, bestPy = 0;
    
    for (int bx = 0; bx < blocksX; bx++) {
        for (int by = 0; by < blocksY; by++) {
            int sx = bx * blockW;
            int sy = by * blockH;
            int ex = std::min(sx + blockW, w);
            int ey = std::min(sy + blockH, h);
            
            float blockSum = 0;
            int darkestVal = 256;
            int darkX = sx, darkY = sy;
            
            for (int y = sy; y < ey; y++) {
                for (int x = sx; x < ex; x++) {
                    int v = lum[y * w + x];
                    blockSum += v;
                    if (v < darkestVal) {
                        darkestVal = v;
                        darkX = x;
                        darkY = y;
                    }
                }
            }
            
            float avg = blockSum / (float)((ex - sx) * (ey - sy));
            if (avg < bestBlockAvg) {
                bestBlockAvg = avg;
                bestPx = darkX;
                bestPy = darkY;
            }
        }
    }
    outX = bestPx;
    outY = bestPy;
}

// Sample average luminance along a line using Bresenham-like stepping.
// Returns average luminance (0-255). Also sets endX/endY to clamped endpoint.
static float sampleLineAvgLuminance(const std::vector<int>& lum, int w, int h,
                                     int x0, int y0, int x1, int y1,
                                     int& clampedX1, int& clampedY1) {
    // Bresenham walk
    int dx = abs(x1 - x0), dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    int cx = x0, cy = y0;
    float sum = 0;
    int count = 0;
    
    clampedX1 = x0;
    clampedY1 = y0;
    
    for (int i = 0; i < dx + dy + 1; i++) {
        if (cx < 0 || cx >= w || cy < 0 || cy >= h) break;
        sum += lum[cy * w + cx];
        count++;
        clampedX1 = cx;
        clampedY1 = cy;
        
        if (cx == x1 && cy == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; cx += sx; }
        if (e2 <  dx) { err += dx; cy += sy; }
    }
    return count > 0 ? sum / (float)count : 255.0f;
}

void ImageToPath::runSketchLines(std::function<void(float, const std::string&)> onProgress) {
    if (m_workW <= 0 || m_workH <= 0) return;
    auto& sketchLines = getActiveLayer().sketchLines;

    // Work on a mutable luminance buffer (0-255 int, like DrawingBotV3)
    std::vector<int> lum(m_workW * m_workH);
    for (int i = 0; i < m_workW * m_workH; i++) {
        lum[i] = m_workingPixels[i];
    }
    
    std::mt19937 rng(42);
    
    // Compute initial average luminance (tracked incrementally)
    double lumSum = 0;
    for (auto v : lum) lumSum += v;
    double pixelCount = (double)lum.size();
    float initialAvgLum = (float)(lumSum / pixelCount);
    float currentAvgLum = initialAvgLum;
    
    // DrawingBotV3 progress formula:
    // lumProgress = (avgLum - initialLum) / ((desiredLum - initialLum) * lineDensity)
    // desiredLum = 253.5 (nearly white)
    // Progress >= 1.0 means done.
    const float desiredLum = 253.5f;
    float lineDensity = sketchLines.lineDensity / 100.0f;
    
    // Segment lengths in pixels
    float pxPerMmX = (float)m_workW / m_drawWidth;
    float pxPerMmY = (float)m_workH / m_drawHeight;
    float pxPerMm = (pxPerMmX + pxPerMmY) * 0.5f;
    int minLenPx = std::max(2, (int)(sketchLines.lineMinLength * pxPerMm));
    int maxLenPx = std::max(minLenPx + 1, (int)(sketchLines.lineMaxLength * pxPerMm));
    
    int angleTests = std::max(3, sketchLines.angleTests);
    float deltaAngle = 360.0f / (float)angleTests;
    int blockW = std::max(4, m_workW / 10);
    int blockH = std::max(4, m_workH / 10);
    
    int consecutiveFails = 0;
    static constexpr int kMaxFails = 1000;
    int maxSquiggles = 500000; // Safety cap
    
    while (consecutiveFails < kMaxFails && (int)m_paths.size() < maxSquiggles) {
        // Check progress (DrawingBotV3 formula)
        float lumProgress = (currentAvgLum - initialAvgLum) / std::max(0.001f, (desiredLum - initialAvgLum) * lineDensity);
        if (lumProgress >= 1.0f) break;
        
        if (onProgress && ((int)m_paths.size() % 50 == 0)) {
            float progress = 0.1f + 0.8f * std::clamp(lumProgress, 0.0f, 1.0f);
            onProgress(progress, "Sketch Lines: " + std::to_string(m_paths.size()) + " paths");
        }
        
        // 1) Find the darkest area and the darkest pixel within it
        int startX, startY;
        findDarkestArea(lum, m_workW, m_workH, blockW, blockH, startX, startY);
        
        // If the darkest pixel is already very bright, we're done
        if (lum[startY * m_workW + startX] > 250) break;
        
        // 2) Build a squiggle (sequence of connected segments)
        ofPolyline squiggle;
        int curX = startX, curY = startY;
        glm::vec2 mmStart = pixelToMM((float)curX, (float)curY);
        squiggle.addVertex(mmStart.x, mmStart.y, 0);
        
        int segCount = 0;
        bool failed = false;
        
        for (int seg = 0; seg < sketchLines.squiggleMax; seg++) {
            // Random start angle for this step
            float startAngle = std::uniform_real_distribution<float>(0.0f, 360.0f)(rng);
            int lineLen = std::uniform_int_distribution<int>(minLenPx, maxLenPx)(rng);
            
            // 3) Test multiple angles, find the darkest line
            float bestAvgLum = 256.0f;
            int bestEndX = curX, bestEndY = curY;
            bool foundLine = false;
            
            for (int a = 0; a < angleTests; a++) {
                float angle = glm::radians(startAngle + deltaAngle * (float)a);
                int testEndX = curX + (int)(cos(angle) * lineLen);
                int testEndY = curY + (int)(sin(angle) * lineLen);
                
                int clampedEndX, clampedEndY;
                float avgL = sampleLineAvgLuminance(lum, m_workW, m_workH,
                    curX, curY, testEndX, testEndY, clampedEndX, clampedEndY);
                
                if (avgL < bestAvgLum) {
                    bestAvgLum = avgL;
                    bestEndX = clampedEndX;
                    bestEndY = clampedEndY;
                    foundLine = true;
                }
            }
            
            if (!foundLine || (bestEndX == curX && bestEndY == curY)) {
                failed = true;
                break;
            }
            
            // 4) Erase (brighten) pixels along the winning line, using tone curve
            //    DrawingBotV3: eraseAmount = eraseMin + toneCurve(luminance/255) * (eraseMax - eraseMin)
            //    We track lumSum incrementally to avoid full-image rescan.
            {
                int dx = abs(bestEndX - curX), dy = abs(bestEndY - curY);
                int sx = (curX < bestEndX) ? 1 : -1;
                int sy = (curY < bestEndY) ? 1 : -1;
                int err = dx - dy;
                int ex = curX, ey = curY;
                
                for (int i = 0; i < dx + dy + 1; i++) {
                    if (ex >= 0 && ex < m_workW && ey >= 0 && ey < m_workH) {
                        int oldVal = lum[ey * m_workW + ex];
                        float normalized = oldVal / 255.0f;
                        float tone = 0.5f;
                        float curved = normalized * normalized * normalized;
                        float toned = curved * tone + normalized * (1.0f - tone);
                        int eraseAmt = (int)(sketchLines.eraseMin + toned * (sketchLines.eraseMax - sketchLines.eraseMin));
                        int newVal = std::min(255, oldVal + eraseAmt);
                        lum[ey * m_workW + ex] = newVal;
                        lumSum += (newVal - oldVal);
                    }
                    if (ex == bestEndX && ey == bestEndY) break;
                    int e2 = 2 * err;
                    if (e2 > -dy) { err -= dy; ex += sx; }
                    if (e2 <  dx) { err += dx; ey += sy; }
                }
            }
            
            curX = bestEndX;
            curY = bestEndY;
            glm::vec2 mm = pixelToMM((float)curX, (float)curY);
            squiggle.addVertex(mm.x, mm.y, 0);
            segCount++;
            
            // Check squiggle deviation: if current area is much brighter than start, end squiggle
            if (sketchLines.shouldLiftPen && segCount >= sketchLines.squiggleMin) {
                if (bestAvgLum > currentAvgLum) break;
            }
        }
        
        if (failed || segCount < sketchLines.squiggleMin) {
            // Erase the isolated dark pixel so we don't get stuck
            int oldVal = lum[startY * m_workW + startX];
            lum[startY * m_workW + startX] = 255;
            lumSum += (255 - oldVal);
            consecutiveFails++;
        } else {
            m_paths.push_back(squiggle);
            consecutiveFails = 0;
        }
        
        // Update average from incremental sum
        currentAvgLum = (float)(lumSum / pixelCount);
    }
}

// =============================================================
// Cross Hatch PFM  (brightness-adaptive hatching)
// Lines are drawn across the image. In dark areas, lines are kept;
// in bright areas, lines are broken. The spacing between lines
// determines the overall density.
// =============================================================

void ImageToPath::runCrossHatch(std::function<void(float, const std::string&)> onProgress) {
    if (m_workW <= 0 || m_workH <= 0) return;
    auto& crossHatch = getActiveLayer().crossHatch;

    float baseAngleProgress = 0.0f;
    float progressPerAngle = crossHatch.useSecondary ? 0.4f : 0.8f;
    
    auto generateHatchAtAngle = [&](float angleDeg) {
        float angleRad = glm::radians(angleDeg);
        float cosA = cos(angleRad), sinA = sin(angleRad);
        
        // Perpendicular direction for line sweep
        float perpX = -sinA, perpY = cosA;
        
        float diagonal = sqrt(m_drawWidth * m_drawWidth + m_drawHeight * m_drawHeight);
        float spacing = crossHatch.lineSpacing;
        int numLines = (int)(diagonal / spacing) + 1;
        
        float cx = m_drawOffsetX + m_drawWidth * 0.5f;
        float cy = m_drawOffsetY + m_drawHeight * 0.5f;
        
        // Sample step: every 0.5mm along each line
        float sampleStep = std::max(0.3f, spacing * 0.3f);
        
        for (int i = -numLines / 2; i <= numLines / 2; i++) {
            float offset = (float)i * spacing;
            float ox = cx + perpX * offset;
            float oy = cy + perpY * offset;
            
            float halfLen = diagonal * 0.6f;
            int numSamples = (int)(diagonal / sampleStep);
            
            ofPolyline line;
            bool penIsDown = false;
            int gapLength = 0; // Track consecutive bright pixels
            
            for (int s = 0; s <= numSamples; s++) {
                float t = (float)s / (float)numSamples;
                float px = (ox - cosA * halfLen) + cosA * diagonal * 1.2f * t;
                float py = (oy - sinA * halfLen) + sinA * diagonal * 1.2f * t;
                
                // Bounds check
                bool inBounds = (px >= m_drawOffsetX && px <= m_drawOffsetX + m_drawWidth &&
                                 py >= m_drawOffsetY && py <= m_drawOffsetY + m_drawHeight);
                
                if (!inBounds) {
                    if (penIsDown && line.size() >= 2) {
                        m_paths.push_back(line);
                        line.clear();
                    }
                    penIsDown = false;
                    continue;
                }
                
                float brightness = sampleBrightness(px, py);
                
                // Draw in areas darker than the threshold.
                // Use a small hysteresis to avoid very short segments.
                bool shouldDraw = (brightness < (1.0f - crossHatch.minBrightness));
                
                if (shouldDraw) {
                    line.addVertex(px, py, 0);
                    penIsDown = true;
                    gapLength = 0;
                } else {
                    gapLength++;
                    // Allow a small gap tolerance (2 samples) to avoid fragmented lines
                    if (gapLength > 2) {
                        if (penIsDown && line.size() >= 2) {
                            m_paths.push_back(line);
                            line.clear();
                        }
                        penIsDown = false;
                    }
                }
            }
            
            if (penIsDown && line.size() >= 2) {
                m_paths.push_back(line);
            }
            
            if (onProgress && (i % 10 == 0)) {
                float lineProgress = (float)(i + numLines / 2) / (float)numLines;
                float progress = 0.1f + (baseAngleProgress + progressPerAngle * lineProgress) * 0.8f;
                onProgress(std::clamp(progress, 0.1f, 0.9f),
                    "Hatch: " + std::to_string(m_paths.size()) + " lines");
            }
        }
        baseAngleProgress += progressPerAngle;
    };
    
    generateHatchAtAngle(crossHatch.angle1);
    if (crossHatch.useSecondary) {
        generateHatchAtAngle(crossHatch.angle2);
    }
}

// =============================================================
// Spiral PFM
// =============================================================

// =============================================================
// Spiral PFM  (ported from DrawingBotV3 PFMSpiralBasic)
// Archimedean spiral where line displacement is modulated by
// the image brightness at each point.
// =============================================================

void ImageToPath::runSpiral(std::function<void(float, const std::string&)> onProgress) {
    if (m_workW <= 0 || m_workH <= 0) return;
    auto& spiral = getActiveLayer().spiral;

    float cx = m_drawOffsetX + m_drawWidth * (spiral.centreX / 100.0f);
    float cy = m_drawOffsetY + m_drawHeight * (spiral.centreY / 100.0f);
    
    float maxRadius = std::max(m_drawWidth, m_drawHeight) * (spiral.spiralSize / 100.0f) * 0.5f;
    float ringSpacing = spiral.ringSpacing;
    float ampScale = spiral.amplitude;
    
    // Total number of rings
    float totalRings = maxRadius / ringSpacing;
    float totalAngleDeg = totalRings * 360.0f;
    float velDeg = spiral.velocity;
    
    ofPolyline spiralPath;
    
    // Sawtooth frequency (oscillations per degree)
    // Higher velocity = fewer oscillations = faster plotting
    float sawFreq = 1.0f / velDeg;
    
    for (float angleDeg = 0; angleDeg < totalAngleDeg; angleDeg += velDeg) {
        float angleRad = glm::radians(angleDeg);
        
        // Base radius on the Archimedean spiral
        float r = ringSpacing * angleDeg / 360.0f;
        
        // Position on the base spiral
        float baseX = cx + cos(angleRad) * r;
        float baseY = cy + sin(angleRad) * r;
        
        // Sample brightness at this point
        float brightness = sampleBrightness(baseX, baseY);
        
        if (spiral.ignoreWhite && brightness > 0.95f) {
            if (spiralPath.size() >= 2) {
                m_paths.push_back(spiralPath);
                spiralPath.clear();
            }
            continue;
        }
        
        // Darkness drives displacement amplitude (dark = large, white = zero)
        float darkness = 1.0f - brightness;
        float maxAmp = ringSpacing * ampScale * 0.5f;
        float amp = darkness * maxAmp;
        
        // Sawtooth/triangle wave perpendicular to the spiral
        // This creates the characteristic "zigzag" look of spiral plotters
        float wave = fmod(angleDeg * sawFreq, 1.0f);
        float triangleWave = (wave < 0.5f) ? (wave * 4.0f - 1.0f) : (3.0f - wave * 4.0f);
        
        // Perpendicular direction (outward from spiral center)
        float perpX = cos(angleRad);
        float perpY = sin(angleRad);
        
        float px = baseX + perpX * amp * triangleWave;
        float py = baseY + perpY * amp * triangleWave;
        
        // Clamp to drawing area
        if (px < m_drawOffsetX || px > m_drawOffsetX + m_drawWidth ||
            py < m_drawOffsetY || py > m_drawOffsetY + m_drawHeight) {
            if (spiralPath.size() >= 2) {
                m_paths.push_back(spiralPath);
                spiralPath.clear();
            }
            continue;
        }
        
        spiralPath.addVertex(px, py, 0);
        
        if (onProgress && ((int)angleDeg % 3600 == 0)) {
            float progress = 0.1f + 0.8f * (angleDeg / totalAngleDeg);
            onProgress(progress, "Spiral: " + std::to_string((int)(r / ringSpacing)) + " rings");
        }
    }
    
    if (spiralPath.size() >= 2) {
        m_paths.push_back(spiralPath);
    }
}

// =============================================================
// Stippling PFM  (importance-sampled dot placement)
// Uses a CDF built from image darkness for efficient sampling.
// =============================================================

void ImageToPath::runStippling(std::function<void(float, const std::string&)> onProgress) {
    if (m_workW <= 0 || m_workH <= 0) return;
    auto& stippling = getActiveLayer().stippling;

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
    
    int targetDots = (int)((m_drawWidth * m_drawHeight) /
        (stippling.dotSpacingMin * stippling.dotSpacingMin));
    targetDots = std::min(targetDots, 100000);
    
    // Build row weights from image darkness (importance sampling)
    std::vector<float> rowWeights(m_workH, 0.0f);
    float totalWeight = 0;
    for (int y = 0; y < m_workH; y++) {
        for (int x = 0; x < m_workW; x++) {
            float darkness = 1.0f - (m_workingPixels[y * m_workW + x] / 255.0f);
            rowWeights[y] += darkness * darkness; // Square for more contrast
        }
        totalWeight += rowWeights[y];
    }
    
    // Build row CDF
    std::vector<float> rowCDF(m_workH);
    float cumul = 0;
    for (int y = 0; y < m_workH; y++) {
        cumul += rowWeights[y];
        rowCDF[y] = cumul / std::max(1.0f, totalWeight);
    }
    
    int placed = 0;
    int attempts = 0;
    int maxAttempts = targetDots * 10;
    
    while (placed < targetDots && attempts < maxAttempts) {
        attempts++;
        
        // Importance-sample: pick row weighted by darkness
        float r = dist01(rng);
        int y = (int)(std::lower_bound(rowCDF.begin(), rowCDF.end(), r) - rowCDF.begin());
        y = std::clamp(y, 0, m_workH - 1);
        
        // Within the row, rejection-sample by column darkness
        int x = std::uniform_int_distribution<int>(0, m_workW - 1)(rng);
        float darkness = 1.0f - (m_workingPixels[y * m_workW + x] / 255.0f);
        if (dist01(rng) > darkness) continue;
        
        // Convert pixel to mm
        float px = m_drawOffsetX + (float)x / (float)m_workW * m_drawWidth;
        float py = m_drawOffsetY + (float)y / (float)m_workH * m_drawHeight;
        
        // Draw a small filled circle (pen plotter dot)
        ofPolyline dot;
        float dotR = stippling.dotRadius;
        int segments = std::max(6, (int)(dotR * 8));
        for (int s = 0; s <= segments; s++) {
            float a = TWO_PI * (float)s / (float)segments;
            dot.addVertex(px + cos(a) * dotR, py + sin(a) * dotR, 0);
        }
        dot.close();
        m_paths.push_back(dot);
        placed++;
        
        if (onProgress && (placed % 200 == 0)) {
            float progress = 0.1f + 0.8f * ((float)placed / (float)targetDots);
            onProgress(std::clamp(progress, 0.1f, 0.9f),
                "Stippling: " + std::to_string(placed) + " / " + std::to_string(targetDots) + " dots");
        }
    }
}

// =============================================================
// Contours PFM  (Canny-like edge detection + chain tracing)
// Steps: Sobel gradient -> non-maximum suppression -> dual
// threshold hysteresis -> chain trace connected edges.
// =============================================================

void ImageToPath::runContours(std::function<void(float, const std::string&)> onProgress) {
    if (m_workW <= 0 || m_workH <= 0) return;
    auto& contours = getActiveLayer().contours;
    int W = m_workW, H = m_workH;
    
    if (onProgress) onProgress(0.12f, "Contours: computing gradients...");
    
    // 1) Sobel gradient magnitude + direction
    std::vector<float> mag(W * H, 0.0f);
    std::vector<float> dir(W * H, 0.0f);
    float maxMag = 0;
    
    for (int y = 1; y < H - 1; y++) {
        for (int x = 1; x < W - 1; x++) {
            float gx = -m_workingPixels[(y-1)*W + (x-1)] - 2*m_workingPixels[y*W + (x-1)] - m_workingPixels[(y+1)*W + (x-1)]
                       + m_workingPixels[(y-1)*W + (x+1)] + 2*m_workingPixels[y*W + (x+1)] + m_workingPixels[(y+1)*W + (x+1)];
            float gy = -m_workingPixels[(y-1)*W + (x-1)] - 2*m_workingPixels[(y-1)*W + x] - m_workingPixels[(y-1)*W + (x+1)]
                       + m_workingPixels[(y+1)*W + (x-1)] + 2*m_workingPixels[(y+1)*W + x] + m_workingPixels[(y+1)*W + (x+1)];
            float m = sqrt(gx * gx + gy * gy);
            mag[y * W + x] = m;
            dir[y * W + x] = atan2(gy, gx);
            if (m > maxMag) maxMag = m;
        }
    }
    
    // Normalize magnitude to 0-255
    if (maxMag > 0) {
        float scale = 255.0f / maxMag;
        for (auto& v : mag) v *= scale;
    }
    
    if (onProgress) onProgress(0.2f, "Contours: non-max suppression...");
    
    // 2) Non-maximum suppression
    std::vector<float> nms(W * H, 0.0f);
    for (int y = 1; y < H - 1; y++) {
        for (int x = 1; x < W - 1; x++) {
            float angle = dir[y * W + x];
            // Quantize angle to 4 directions
            float a = fmod(angle + M_PI, (float)M_PI); // 0 to PI
            int dx1, dy1, dx2, dy2;
            if      (a < M_PI * 0.125f || a >= M_PI * 0.875f) { dx1= 1; dy1= 0; dx2=-1; dy2= 0; }
            else if (a < M_PI * 0.375f)                        { dx1= 1; dy1= 1; dx2=-1; dy2=-1; }
            else if (a < M_PI * 0.625f)                        { dx1= 0; dy1= 1; dx2= 0; dy2=-1; }
            else                                                { dx1=-1; dy1= 1; dx2= 1; dy2=-1; }
            
            float v = mag[y * W + x];
            float n1 = mag[(y + dy1) * W + (x + dx1)];
            float n2 = mag[(y + dy2) * W + (x + dx2)];
            nms[y * W + x] = (v >= n1 && v >= n2) ? v : 0;
        }
    }
    
    if (onProgress) onProgress(0.3f, "Contours: hysteresis thresholding...");
    
    // 3) Dual-threshold hysteresis
    float threshHigh = contours.cannyHigh;
    float threshLow  = contours.cannyLow;
    enum EdgeType : uint8_t { kNone = 0, kWeak = 1, kStrong = 2 };
    std::vector<uint8_t> edgeMap(W * H, kNone);
    
    for (int y = 1; y < H - 1; y++) {
        for (int x = 1; x < W - 1; x++) {
            float v = nms[y * W + x];
            if      (v >= threshHigh) edgeMap[y * W + x] = kStrong;
            else if (v >= threshLow)  edgeMap[y * W + x] = kWeak;
        }
    }
    
    // Promote weak edges connected to strong edges (flood fill)
    bool changed = true;
    while (changed) {
        changed = false;
        for (int y = 1; y < H - 1; y++) {
            for (int x = 1; x < W - 1; x++) {
                if (edgeMap[y * W + x] != kWeak) continue;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        if (edgeMap[(y + dy) * W + (x + dx)] == kStrong) {
                            edgeMap[y * W + x] = kStrong;
                            changed = true;
                            goto next_pixel;
                        }
                    }
                }
                next_pixel:;
            }
        }
    }
    
    if (onProgress) onProgress(0.5f, "Contours: tracing edges...");
    
    // 4) Chain-trace connected strong edge pixels into polylines
    std::vector<bool> visited(W * H, false);
    
    for (int y = 1; y < H - 1; y++) {
        for (int x = 1; x < W - 1; x++) {
            if (edgeMap[y * W + x] != kStrong) continue;
            if (visited[y * W + x]) continue;
            
            // Trace forward from this pixel
            ofPolyline contour;
            int cx = x, cy = y;
            
            while (cx >= 1 && cx < W - 1 && cy >= 1 && cy < H - 1
                   && !visited[cy * W + cx]
                   && edgeMap[cy * W + cx] == kStrong) {
                visited[cy * W + cx] = true;
                glm::vec2 mm = pixelToMM((float)cx, (float)cy);
                contour.addVertex(mm.x, mm.y, 0);
                
                // Follow the strongest unvisited strong-edge neighbor
                int bestNx = -1, bestNy = -1;
                float bestM = 0;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        int nx = cx + dx, ny = cy + dy;
                        if (visited[ny * W + nx]) continue;
                        if (edgeMap[ny * W + nx] != kStrong) continue;
                        if (nms[ny * W + nx] > bestM) {
                            bestM = nms[ny * W + nx];
                            bestNx = nx;
                            bestNy = ny;
                        }
                    }
                }
                if (bestNx < 0) break;
                cx = bestNx;
                cy = bestNy;
            }
            
            if ((int)contour.size() >= 3) {
                float lenMM = contour.getPerimeter();
                if (lenMM >= contours.minContourLen) {
                    // Simplify to reduce point count (Douglas-Peucker)
                    contour.simplify(0.3f);
                    m_paths.push_back(contour);
                }
            }
        }
        
        if (onProgress && (y % 50 == 0)) {
            float progress = 0.5f + 0.4f * ((float)y / (float)H);
            onProgress(std::clamp(progress, 0.5f, 0.9f),
                "Contours: " + std::to_string(m_paths.size()) + " paths");
        }
    }
}

// =============================================================
// Stats
// =============================================================

void ImageToPath::computeStats() {
    totalPaths = 0;
    totalPoints = 0;
    totalDistance = 0;
    estimatedTime = 0;

    for (const auto& layer : layers) {
        if (!layer.visible) continue;
        totalPaths += layer.totalPaths;
        totalPoints += layer.totalPoints;
        totalDistance += layer.totalDistance;
        estimatedTime += layer.estimatedTime;
    }

    // Add inter-path travel time
    float travelDist = 0;
    glm::vec3 lastEnd(0);
    for (const auto& path : m_flatPaths) {
        if (path.size() > 0) {
            travelDist += glm::length(path[0] - lastEnd);
            lastEnd = path[path.size() - 1];
        }
    }
    estimatedTime += travelDist / pen.travelSpeed * 60.0f;
}

