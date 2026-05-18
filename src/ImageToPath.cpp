#ifndef NOMINMAX
#define NOMINMAX
#endif
#define _USE_MATH_DEFINES
#include <cmath>

#include "ImageToPath.h"
#include "ofxSvg.h"
#include "ofxSvgElements.h"
#include <algorithm>
#include <map>
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
    // Default brush library
    brushes.push_back({"0.3mm Round",  BrushShape::Round,  0.3f, 0, 0.3f, 0, ofColor(0)});
    brushes.push_back({"0.5mm Round",  BrushShape::Round,  0.5f, 0, 0.3f, 0, ofColor(0)});
    brushes.push_back({"1.0mm Round",  BrushShape::Round,  1.0f, 0, 0.3f, 0, ofColor(0)});
    brushes.push_back({"0.5mm Square", BrushShape::Square, 0.5f, 0, 0.3f, 0, ofColor(0)});
    brushes.push_back({"1.5mm Flat",   BrushShape::Flat,   1.5f, 45.0f, 0.2f, 0, ofColor(0)});
    brushes.push_back({"2.0mm Nib",    BrushShape::Nib,    2.0f, 30.0f, 0.15f, 0, ofColor(0)});

    // Create the initial default layer
    addLayer("Layer 1");
}

// =============================================================
// Relationship helpers (file-scope, forwarded to static methods)
// =============================================================

/*static*/
void ImageToPath::linkChild(entt::registry& reg,
                            entt::entity parent, entt::entity child)
{
    auto& rel = reg.get<ecs::Relationship>(child);
    rel.parent       = parent;
    rel.next_sibling = entt::null;
    rel.prev_sibling = entt::null;

    if (parent != entt::null) {
        auto& parentRel = reg.get<ecs::Relationship>(parent);
        parentRel.children_count++;
        if (parentRel.first_child == entt::null) {
            parentRel.first_child = child;
        } else {
            entt::entity last = parentRel.first_child;
            while (reg.get<ecs::Relationship>(last).next_sibling != entt::null)
                last = reg.get<ecs::Relationship>(last).next_sibling;
            reg.get<ecs::Relationship>(last).next_sibling = child;
            rel.prev_sibling = last;
        }
    } else {
        // Root-level: append after the current last root
        // (last root = root entity whose next_sibling == null)
        entt::entity lastRoot = entt::null;
        auto view = reg.view<ecs::layer_component, ecs::Relationship>();
        for (auto e : view) {
            if (e == child) continue;
            auto& er = reg.get<ecs::Relationship>(e);
            if (er.parent == entt::null && er.next_sibling == entt::null)
                lastRoot = e;
        }
        if (lastRoot != entt::null) {
            reg.get<ecs::Relationship>(lastRoot).next_sibling = child;
            rel.prev_sibling = lastRoot;
        }
    }
}

/*static*/
void ImageToPath::unlinkChild(entt::registry& reg, entt::entity e)
{
    if (!reg.valid(e)) return;
    auto& rel = reg.get<ecs::Relationship>(e);

    if (rel.prev_sibling != entt::null && reg.valid(rel.prev_sibling))
        reg.get<ecs::Relationship>(rel.prev_sibling).next_sibling = rel.next_sibling;
    if (rel.next_sibling != entt::null && reg.valid(rel.next_sibling))
        reg.get<ecs::Relationship>(rel.next_sibling).prev_sibling = rel.prev_sibling;

    if (rel.parent != entt::null && reg.valid(rel.parent)) {
        auto& parentRel = reg.get<ecs::Relationship>(rel.parent);
        if (parentRel.first_child == e)
            parentRel.first_child = rel.next_sibling;
        if (parentRel.children_count > 0)
            parentRel.children_count--;
    }

    rel.parent       = entt::null;
    rel.prev_sibling = entt::null;
    rel.next_sibling = entt::null;
}

// =============================================================
// Layer CRUD
// =============================================================

entt::entity ImageToPath::addLayer(const std::string& name, entt::entity parent) {
    entt::entity e = registry.create();

    // Count existing siblings to determine the display index
    int siblingCount = 0;
    if (parent != entt::null && registry.valid(parent)) {
        auto& pr = registry.get<ecs::Relationship>(parent);
        entt::entity sib = pr.first_child;
        while (sib != entt::null) {
            siblingCount++;
            sib = registry.get<ecs::Relationship>(sib).next_sibling;
        }
    } else {
        auto view = registry.view<ecs::layer_component, ecs::Relationship>();
        for (auto ex : view)
            if (registry.get<ecs::Relationship>(ex).parent == entt::null)
                siblingCount++;
    }

    ecs::layer_component lc;
    lc.index   = siblingCount;
    lc.visible = true;
    lc.locked  = false;
    lc.color   = ofColor(0, 0, 0);
    lc.name    = name.empty()
        ? ("Layer " + std::to_string(siblingCount + 1))
        : name;

    registry.emplace<ecs::layer_component>(e, lc);
    registry.emplace<ecs::Relationship>(e);
    registry.emplace<plotter::settings_component>(e);
    registry.emplace<plotter::paths_component>(e);
    registry.emplace<plotter::toolpath_stats_component>(e);
    registry.emplace<plotter::fill_raster_component>(e);

    linkChild(registry, parent, e);

    if (activeLayer == entt::null)
        activeLayer = e;

    rebuildLayerOrder();
    return e;
}

void ImageToPath::removeLayer(entt::entity e) {
    if (layerOrder.size() <= 1) return;
    if (!registry.valid(e)) return;

    bool wasActive = (activeLayer == e);
    int oldIdx = 0;
    for (int i = 0; i < (int)layerOrder.size(); i++)
        if (layerOrder[i] == e) { oldIdx = i; break; }

    // Collect all descendants for destruction
    std::vector<entt::entity> toDestroy;
    std::function<void(entt::entity)> collectDesc = [&](entt::entity ent) {
        toDestroy.push_back(ent);
        auto& r = registry.get<ecs::Relationship>(ent);
        entt::entity child = r.first_child;
        while (child != entt::null) {
            entt::entity next = registry.get<ecs::Relationship>(child).next_sibling;
            collectDesc(child);
            child = next;
        }
    };
    collectDesc(e);

    unlinkChild(registry, e);

    for (auto ent : toDestroy)
        if (registry.valid(ent)) registry.destroy(ent);

    rebuildLayerOrder();

    if (wasActive) {
        if (!layerOrder.empty())
            activeLayer = layerOrder[std::max(0, std::min(oldIdx, (int)layerOrder.size() - 1))];
        else
            activeLayer = entt::null;
    }
}

void ImageToPath::reparentLayer(entt::entity child,
                                entt::entity newParent,
                                entt::entity insertBefore)
{
    if (!registry.valid(child)) return;
    if (newParent != entt::null && !registry.valid(newParent)) return;
    if (child == newParent) return;

    // Cycle prevention: don't reparent into a descendant
    {
        entt::entity check = newParent;
        while (check != entt::null && registry.valid(check)) {
            if (check == child) return;
            check = registry.get<ecs::Relationship>(check).parent;
        }
    }

    unlinkChild(registry, child);

    if (insertBefore != entt::null && registry.valid(insertBefore)) {
        auto& ibRel    = registry.get<ecs::Relationship>(insertBefore);
        auto& childRel = registry.get<ecs::Relationship>(child);

        childRel.parent       = newParent;
        childRel.next_sibling = insertBefore;
        childRel.prev_sibling = ibRel.prev_sibling;

        if (ibRel.prev_sibling != entt::null && registry.valid(ibRel.prev_sibling))
            registry.get<ecs::Relationship>(ibRel.prev_sibling).next_sibling = child;
        ibRel.prev_sibling = child;

        if (newParent != entt::null && registry.valid(newParent)) {
            auto& parentRel = registry.get<ecs::Relationship>(newParent);
            parentRel.children_count++;
            if (parentRel.first_child == insertBefore)
                parentRel.first_child = child;
        }
    } else {
        linkChild(registry, newParent, child);
    }

    rebuildLayerOrder();
}

void ImageToPath::rebuildLayerOrder()
{
    layerOrder.clear();

    // Find the first root: parent == null && prev_sibling == null
    entt::entity firstRoot = entt::null;
    auto view = registry.view<ecs::layer_component, ecs::Relationship>();
    for (auto e : view) {
        auto& rel = registry.get<ecs::Relationship>(e);
        if (rel.parent == entt::null && rel.prev_sibling == entt::null) {
            firstRoot = e;
            break;
        }
    }
    if (firstRoot == entt::null) return;

    // DFS: follow root sibling chain, recurse into first_child for each
    std::function<void(entt::entity)> collect = [&](entt::entity e) {
        layerOrder.push_back(e);
        entt::entity ch = registry.get<ecs::Relationship>(e).first_child;
        while (ch != entt::null) {
            collect(ch);
            ch = registry.get<ecs::Relationship>(ch).next_sibling;
        }
    };

    entt::entity root = firstRoot;
    while (root != entt::null) {
        collect(root);
        root = registry.get<ecs::Relationship>(root).next_sibling;
    }
}

bool ImageToPath::isEffectivelyVisible(entt::entity e) const
{
    while (e != entt::null && registry.valid(e)) {
        if (!registry.get<ecs::layer_component>(e).visible) return false;
        e = registry.get<ecs::Relationship>(e).parent;
    }
    return true;
}

int ImageToPath::addBrush(const BrushPreset& b) {
    brushes.push_back(b);
    return (int)brushes.size() - 1;
}

// =============================================================
// Active layer convenience accessors
// =============================================================

plotter::settings_component& ImageToPath::getActiveSettings() {
    return registry.get<plotter::settings_component>(activeLayer);
}

const plotter::settings_component& ImageToPath::getActiveSettings() const {
    return registry.get<plotter::settings_component>(activeLayer);
}

ecs::layer_component& ImageToPath::getActiveLayerComponent() {
    return registry.get<ecs::layer_component>(activeLayer);
}

const ecs::layer_component& ImageToPath::getActiveLayerComponent() const {
    return registry.get<ecs::layer_component>(activeLayer);
}

// =============================================================
// Flat path list
// =============================================================

void ImageToPath::rebuildFlatPaths() {
    m_flatPaths.clear();
    m_flatPathLayerEntity.clear();
    m_flatPathColors.clear();
    for (entt::entity e : layerOrder) {
        if (!registry.valid(e)) continue;
        if (!isEffectivelyVisible(e)) continue;
        const ofColor& layerCol = registry.get<ecs::layer_component>(e).color;
        auto& pc = registry.get<plotter::paths_component>(e);
        for (int pi = 0; pi < (int)pc.paths.size(); ++pi) {
            // Resolve this path's colour: per-path override takes priority, else layer colour.
            ofColor col = (pi < (int)pc.pathColors.size()) ? pc.pathColors[pi] : layerCol;
            for (const auto& outline : pc.paths[pi].getOutline()) {
                m_flatPaths.push_back(outline);
                m_flatPathLayerEntity.push_back(e);
                m_flatPathColors.push_back(col);
            }
        }
    }
}

ofColor ImageToPath::getPathColor(int flatIdx) const {
    if (flatIdx >= 0 && flatIdx < (int)m_flatPathColors.size())
        return m_flatPathColors[flatIdx];
    if (flatIdx >= 0 && flatIdx < (int)m_flatPathLayerEntity.size()) {
        entt::entity e = m_flatPathLayerEntity[flatIdx];
        if (registry.valid(e)) return registry.get<ecs::layer_component>(e).color;
    }
    return ofColor(0);
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

// =============================================================
// Image loading
// =============================================================

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

bool ImageToPath::loadVectorSVG(const std::string& path, SvgImportMode mode) {
    ofxSvg svg;
    if (!svg.load(path)) {
        ofLogError("ImageToPath") << "Failed to load SVG: " << path;
        return false;
    }

    // -----------------------------------------------------------------------
    // Build per-layer polyline collections based on the import mode.
    // -----------------------------------------------------------------------
    struct SvgLayerData {
        std::string             name;
        ofColor                 layerColor {0, 0, 0};  ///< representative (first-path) colour
        std::vector<ofPolyline> polys;
        std::vector<ofColor>    polyColors;            ///< per-polyline colour
    };

    // Helper: extract polylines (world-space) + per-poly colour from path elements.
    // The path vertices live in element-local space; the element's ofNode transform
    // (including its parent group chain) must be applied to get SVG document coordinates.
    auto extractPolys = [](const std::vector<std::shared_ptr<ofxSvgPath>>& eles,
                           std::vector<ofPolyline>& out,
                           std::vector<ofColor>& outColors,
                           ofColor& layerColorOut) {
        bool hasLayerColor = false;
        for (auto& pe : eles) {
            if (!pe) continue;
            ofPath& op = pe->getPath();

            // Pick the representative colour for this path element.
            ofColor pathColor;
            if (op.hasOutline()) {
                pathColor = op.getStrokeColor();
            } else if (op.isFilled()) {
                pathColor = op.getFillColor();
            } else {
                pathColor = ofColor(0);
            }
            if (!hasLayerColor) { layerColorOut = pathColor; hasLayerColor = true; }

            // Bump resolution before tessellating — SVG bezier curves (how Inkscape/Illustrator
            // encode circles and splines) default to ofPath's curveResolution of 20, which looks
            // very coarse. 128 gives smooth curves at any plotter-relevant scale.
            op.setCurveResolution(128);
            op.setCircleResolution(128);
            // Apply the full hierarchy transform so group translate/rotate/scale is baked in.
            // Also account for mOffsetPos (used for circles/ellipses/rectangles — their
            // geometry center is stored as an additional local offset applied in customDraw).
            glm::mat4 globalMat = pe->getGlobalTransformMatrix();
            glm::vec3 off       = pe->getOffsetPathPosition();
            bool isIdentity = (globalMat == glm::mat4(1.f)) && (off.x == 0.f && off.y == 0.f);

            for (const auto& pl : op.getOutline()) {
                if (pl.size() < 2) continue;
                if (isIdentity) {
                    out.push_back(pl);
                } else {
                    ofPolyline world;
                    const auto& verts = pl.getVertices();
                    world.getVertices().reserve(verts.size());
                    for (const auto& v3 : verts) {
                        glm::vec4 v(v3.x + off.x, v3.y + off.y, 0.f, 1.f);
                        glm::vec3 w = glm::vec3(globalMat * v);
                        world.addVertex(w.x, w.y, 0.f);
                    }
                    if (pl.isClosed()) world.close();
                    out.push_back(std::move(world));
                }
                outColors.push_back(pathColor);
            }
        }
    };

    std::vector<SvgLayerData> layerData;

    if (mode == SvgImportMode::SingleLayer) {
        // Legacy: all paths into one layer
        SvgLayerData ld;
        ld.name = "SVG";
        auto eles = svg.getAllElementsWithPath();
        extractPolys(eles, ld.polys, ld.polyColors, ld.layerColor);
        if (!ld.polys.empty()) layerData.push_back(std::move(ld));

    } else if (mode == SvgImportMode::ColorsAsLayers) {
        // Group by stroke/fill colour — reuse extractPolys so resolution + transform are applied.
        auto eles = svg.getAllElementsWithPath();
        std::vector<ofColor> keyOrder;
        std::map<uint32_t, SvgLayerData> byColor;

        auto colorKey = [](const ofColor& c) -> uint32_t {
            return ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
        };

        // First pass: collect all polys with their colours via extractPolys.
        std::vector<ofPolyline> allPolys;
        std::vector<ofColor>    allColors;
        ofColor dummy;
        extractPolys(eles, allPolys, allColors, dummy);

        for (size_t i = 0; i < allPolys.size(); ++i) {
            ofColor col = (i < allColors.size()) ? allColors[i] : ofColor(0);
            uint32_t key = colorKey(col);
            if (byColor.find(key) == byColor.end()) {
                keyOrder.push_back(col);
                char hex[10];
                snprintf(hex, sizeof(hex), "#%02X%02X%02X", col.r, col.g, col.b);
                byColor[key].name       = hex;
                byColor[key].layerColor = col;
            }
            byColor[key].polys.push_back(allPolys[i]);
            byColor[key].polyColors.push_back(col);
        }
        for (auto& c : keyOrder) {
            auto& ld = byColor[colorKey(c)];
            if (!ld.polys.empty()) layerData.push_back(std::move(ld));
        }

    } else {
        // GroupsAsLayers (default): one layer per top-level <g>
        auto& topChildren = svg.getChildren();
        bool hasGroups = false;
        for (auto& child : topChildren)
            if (child && child->isGroup()) { hasGroups = true; break; }

        if (hasGroups) {
            SvgLayerData defaultLayer;
            defaultLayer.name = "Default";
            for (auto& child : topChildren) {
                if (!child) continue;
                if (child->isGroup()) {
                    auto group = std::dynamic_pointer_cast<ofxSvgGroup>(child);
                    if (!group) continue;
                    SvgLayerData ld;
                    ld.name = group->getName().empty() ? "Layer" : group->getName();
                    auto eles = group->getAllElementsWithPath();
                    extractPolys(eles, ld.polys, ld.polyColors, ld.layerColor);
                    if (!ld.polys.empty()) layerData.push_back(std::move(ld));
                } else {
                    auto pe = std::dynamic_pointer_cast<ofxSvgPath>(child);
                    if (pe) {
                        std::vector<std::shared_ptr<ofxSvgPath>> single { pe };
                        extractPolys(single, defaultLayer.polys, defaultLayer.polyColors, defaultLayer.layerColor);
                    }
                }
            }
            if (!defaultLayer.polys.empty())
                layerData.insert(layerData.begin(), std::move(defaultLayer));
        } else {
            SvgLayerData ld;
            ld.name = "SVG";
            auto eles = svg.getAllElementsWithPath();
            extractPolys(eles, ld.polys, ld.polyColors, ld.layerColor);
            if (!ld.polys.empty()) layerData.push_back(std::move(ld));
        }
    }

    if (layerData.empty()) {
        ofLogWarning("ImageToPath") << "SVG has no drawable paths: " << path;
        return false;
    }

    // -----------------------------------------------------------------------
    // Compute global bounds across all layers for fit-to-canvas scaling.
    // -----------------------------------------------------------------------
    float minX =  std::numeric_limits<float>::max();
    float minY =  std::numeric_limits<float>::max();
    float maxX = -std::numeric_limits<float>::max();
    float maxY = -std::numeric_limits<float>::max();
    for (const auto& ld : layerData)
        for (const auto& pl : ld.polys)
            for (const auto& v : pl.getVertices()) {
                minX = std::min(minX, v.x); minY = std::min(minY, v.y);
                maxX = std::max(maxX, v.x); maxY = std::max(maxY, v.y);
            }

    float contentW = std::max(1e-4f, maxX - minX);
    float contentH = std::max(1e-4f, maxY - minY);

    glm::vec2 paper = getPaperSizeMM();
    float availW = std::max(1.0f, paper.x - 2.0f * marginMM);
    float availH = std::max(1.0f, paper.y - 2.0f * marginMM);
    float scale  = std::min(availW / contentW, availH / contentH);
    float drawW  = contentW * scale;
    float drawH  = contentH * scale;
    float offX   = marginMM + (availW - drawW) * 0.5f;
    float offY   = marginMM + (availH - drawH) * 0.5f;

    m_drawWidth   = drawW;
    m_drawHeight  = drawH;
    m_drawOffsetX = offX;
    m_drawOffsetY = offY;
    m_workW       = (int)std::max(1.0f, contentW);
    m_workH       = (int)std::max(1.0f, contentH);

    // -----------------------------------------------------------------------
    // Helper: convert a source polyline to a scaled mm-space ofPath.
    // -----------------------------------------------------------------------
    auto polyToMmPath = [&](const ofPolyline& pl) -> ofPath {
        ofPath out;
        const auto& verts = pl.getVertices();
        if (verts.empty()) return out;
        out.moveTo(offX + (verts[0].x - minX) * scale,
                   offY + (verts[0].y - minY) * scale);
        for (size_t vi = 1; vi < verts.size(); ++vi)
            out.lineTo(offX + (verts[vi].x - minX) * scale,
                       offY + (verts[vi].y - minY) * scale);
        if (pl.isClosed()) out.close();
        out.getOutline(); // pre-warm tessellation
        return out;
    };

    // -----------------------------------------------------------------------
    // Populate ECS layers: always create fresh layers so multiple SVG imports
    // stack onto the canvas without overwriting existing content.
    // -----------------------------------------------------------------------
    entt::entity firstNew = entt::null;
    for (size_t li = 0; li < layerData.size(); ++li) {
        const auto& ld = layerData[li];
        entt::entity target = addLayer(ld.name);
        if (li == 0) firstNew = target;

        registry.get<ecs::layer_component>(target).color = ld.layerColor;

        auto& pc = registry.get<plotter::paths_component>(target);
        auto& sc = registry.get<plotter::toolpath_stats_component>(target);
        pc.paths.reserve(ld.polys.size());
        pc.pathColors.reserve(ld.polyColors.size());
        for (size_t pi = 0; pi < ld.polys.size(); ++pi) {
            pc.paths.push_back(polyToMmPath(ld.polys[pi]));
            if (pi < ld.polyColors.size())
                pc.pathColors.push_back(ld.polyColors[pi]);
        }

        sc.totalPaths    = (int)pc.paths.size();
        sc.totalPoints   = 0;
        sc.totalDistance = 0.f;
        for (auto& p : pc.paths)
            for (const auto& outline : p.getOutline()) {
                sc.totalPoints   += (int)outline.size();
                sc.totalDistance += outline.getPerimeter();
            }
        sc.estimatedTime = sc.totalDistance / std::max(1.0f, pen.drawSpeed) * 60.0f;
    }

    // Select the first newly-added layer.
    if (firstNew != entt::null) activeLayer = firstNew;

    // Sync image overlay rect to the draw area.
    imageOverlayX = offX;  imageOverlayY = offY;
    imageOverlayW = drawW; imageOverlayH = drawH;

    rebuildFlatPaths();
    computeStats();

    ofLogNotice("ImageToPath") << "Imported SVG: " << path
        << " -> " << layerData.size() << " layer(s), "
        << m_flatPaths.size() << " total paths";
    return true;
}

void ImageToPath::scaleToFit() {
    // Compute bounding box of all paths across all layers.
    float minX =  std::numeric_limits<float>::max();
    float minY =  std::numeric_limits<float>::max();
    float maxX = -std::numeric_limits<float>::max();
    float maxY = -std::numeric_limits<float>::max();
    for (entt::entity e : layerOrder) {
        if (!registry.valid(e)) continue;
        for (auto& p : registry.get<plotter::paths_component>(e).paths)
            for (const auto& outline : p.getOutline())
                for (const auto& v : outline.getVertices()) {
                    minX = std::min(minX, v.x); minY = std::min(minY, v.y);
                    maxX = std::max(maxX, v.x); maxY = std::max(maxY, v.y);
                }
    }
    if (minX >= maxX || minY >= maxY) return;

    glm::vec2 paper  = getPaperSizeMM();
    float     availW = std::max(1.0f, paper.x - 2.0f * marginMM);
    float     availH = std::max(1.0f, paper.y - 2.0f * marginMM);
    float     cw     = maxX - minX;
    float     ch     = maxY - minY;
    float     scale  = std::min(availW / cw, availH / ch);
    float     offX   = marginMM + (availW - cw * scale) * 0.5f;
    float     offY   = marginMM + (availH - ch * scale) * 0.5f;

    // Rescale every vertex in every layer.
    for (entt::entity e : layerOrder) {
        if (!registry.valid(e)) continue;
        auto& pc = registry.get<plotter::paths_component>(e);
        for (auto& p : pc.paths) {
            // Rebuild the path from its outline with new coordinates.
            std::vector<ofPolyline> outlines = p.getOutline();
            p.clear();
            for (const auto& pl : outlines) {
                const auto& verts = pl.getVertices();
                if (verts.empty()) continue;
                p.moveTo(offX + (verts[0].x - minX) * scale,
                         offY + (verts[0].y - minY) * scale);
                for (size_t vi = 1; vi < verts.size(); ++vi)
                    p.lineTo(offX + (verts[vi].x - minX) * scale,
                             offY + (verts[vi].y - minY) * scale);
                if (pl.isClosed()) p.close();
            }
            p.getOutline(); // re-warm cache
        }
    }

    // Update draw area fields and overlay rect.
    m_drawOffsetX = offX;  m_drawOffsetY = offY;
    m_drawWidth   = cw * scale; m_drawHeight = ch * scale;
    imageOverlayX = offX;  imageOverlayY = offY;
    imageOverlayW = m_drawWidth; imageOverlayH = m_drawHeight;

    rebuildFlatPaths();
    computeStats();
}

// =============================================================
// SVG fill rasterisation
// =============================================================

// =============================================================
// Post-load path transforms
// =============================================================

void ImageToPath::applyPathTransform(std::function<glm::vec2(glm::vec2)> xform) {
    for (auto e : layerOrder) {
        if (!registry.valid(e)) continue;
        auto& pc = registry.get<plotter::paths_component>(e);
        for (auto& path : pc.paths) {
            ofPath newPath;
            for (const auto& outline : path.getOutline()) {
                const auto& verts = outline.getVertices();
                if (verts.empty()) continue;
                auto p0 = xform({ verts[0].x, verts[0].y });
                newPath.moveTo(p0.x, p0.y);
                for (size_t i = 1; i < verts.size(); ++i) {
                    auto p = xform({ verts[i].x, verts[i].y });
                    newPath.lineTo(p.x, p.y);
                }
                if (outline.isClosed()) newPath.close();
            }
            newPath.getOutline();
            path = std::move(newPath);
        }
        // Refresh per-layer stats
        auto& sc = registry.get<plotter::toolpath_stats_component>(e);
        sc.totalPaths    = (int)pc.paths.size();
        sc.totalPoints   = 0;
        sc.totalDistance = 0.f;
        for (auto& p : pc.paths)
            for (const auto& ol : p.getOutline()) {
                sc.totalPoints   += (int)ol.size();
                sc.totalDistance += ol.getPerimeter();
            }
        sc.estimatedTime = sc.totalDistance / std::max(1.f, pen.drawSpeed) * 60.f;
    }
    rebuildFlatPaths();
    computeStats();
}

void ImageToPath::flipHorizontal() {
    float mid = m_drawOffsetX + m_drawWidth * 0.5f;
    applyPathTransform([mid](glm::vec2 v) {
        return glm::vec2(2.f * mid - v.x, v.y);
    });
}

void ImageToPath::flipVertical() {
    float mid = m_drawOffsetY + m_drawHeight * 0.5f;
    applyPathTransform([mid](glm::vec2 v) {
        return glm::vec2(v.x, 2.f * mid - v.y);
    });
}

void ImageToPath::rotate90CW() {
    // Rotate each point 90° CW around the current draw-area centre,
    // then re-fit the (now-transposed) draw area to the paper.
    float oldCx = m_drawOffsetX + m_drawWidth  * 0.5f;
    float oldCy = m_drawOffsetY + m_drawHeight * 0.5f;
    std::swap(m_drawWidth, m_drawHeight);
    glm::vec2 paper = getPaperSizeMM();
    m_drawOffsetX = marginMM + (std::max(1.f, paper.x - 2.f * marginMM) - m_drawWidth)  * 0.5f;
    m_drawOffsetY = marginMM + (std::max(1.f, paper.y - 2.f * marginMM) - m_drawHeight) * 0.5f;
    float newCx = m_drawOffsetX + m_drawWidth  * 0.5f;
    float newCy = m_drawOffsetY + m_drawHeight * 0.5f;
    float dx = newCx - oldCx, dy = newCy - oldCy;
    applyPathTransform([=](glm::vec2 v) {
        // 90° CW: (dx,dy) → (dy,-dx), then shift to new centre
        return glm::vec2(oldCx + (v.y - oldCy) + dx,
                         oldCy - (v.x - oldCx) + dy);
    });
    imageOverlayX = m_drawOffsetX;  imageOverlayY = m_drawOffsetY;
    imageOverlayW = m_drawWidth;    imageOverlayH = m_drawHeight;
}

void ImageToPath::rotate90CCW() {
    float oldCx = m_drawOffsetX + m_drawWidth  * 0.5f;
    float oldCy = m_drawOffsetY + m_drawHeight * 0.5f;
    std::swap(m_drawWidth, m_drawHeight);
    glm::vec2 paper = getPaperSizeMM();
    m_drawOffsetX = marginMM + (std::max(1.f, paper.x - 2.f * marginMM) - m_drawWidth)  * 0.5f;
    m_drawOffsetY = marginMM + (std::max(1.f, paper.y - 2.f * marginMM) - m_drawHeight) * 0.5f;
    float newCx = m_drawOffsetX + m_drawWidth  * 0.5f;
    float newCy = m_drawOffsetY + m_drawHeight * 0.5f;
    float dx = newCx - oldCx, dy = newCy - oldCy;
    applyPathTransform([=](glm::vec2 v) {
        // 90° CCW: (dx,dy) → (-dy,dx), then shift to new centre
        return glm::vec2(oldCx - (v.y - oldCy) + dx,
                         oldCy + (v.x - oldCx) + dy);
    });
    imageOverlayX = m_drawOffsetX;  imageOverlayY = m_drawOffsetY;
    imageOverlayW = m_drawWidth;    imageOverlayH = m_drawHeight;
}

bool ImageToPath::hasFillLayers() const {
    for (auto e : layerOrder) {
        if (!registry.valid(e)) continue;
        if (!registry.all_of<plotter::fill_raster_component>(e)) continue;
        const auto& frc = registry.get<plotter::fill_raster_component>(e);
        if (frc.enabled && frc.rasterW > 0) return true;
    }
    return false;
}

void ImageToPath::renderSvgColorPreview(float pixPerMM) {
    m_svgPreview.clear();
    if (m_drawWidth <= 0.f || m_drawHeight <= 0.f) return;

    int imgW = std::max(1, (int)(m_drawWidth  * pixPerMM));
    int imgH = std::max(1, (int)(m_drawHeight * pixPerMM));

    ofFbo fbo;
    ofFbo::Settings s;
    s.width          = imgW;
    s.height         = imgH;
    s.internalformat = GL_RGBA;
    fbo.allocate(s);

    fbo.begin();
    ofClear(0, 0, 0, 0);   // transparent — let the paper background show through

    for (auto e : layerOrder) {
        if (!registry.valid(e)) continue;
        if (!registry.all_of<ecs::layer_component>(e)) continue;
        if (!registry.get<ecs::layer_component>(e).visible) continue;
        auto& pc = registry.get<plotter::paths_component>(e);

        for (size_t pi = 0; pi < pc.paths.size(); ++pi) {
            ofColor col = (pi < pc.pathColors.size())
                ? pc.pathColors[pi]
                : registry.get<ecs::layer_component>(e).color;

            ofPath& p = pc.paths[pi];
            bool hasClosed = false;
            for (const auto& ol : p.getOutline())
                if (ol.isClosed()) { hasClosed = true; break; }

            if (hasClosed) {
                // Render filled, using original colour.
                ofSetColor(col);
                for (const auto& outline : p.getOutline()) {
                    if (!outline.isClosed() || outline.size() < 3) continue;
                    ofBeginShape();
                    for (const auto& v : outline.getVertices()) {
                        float px = (v.x - m_drawOffsetX) / m_drawWidth  * (float)imgW;
                        float py = (v.y - m_drawOffsetY) / m_drawHeight * (float)imgH;
                        ofVertex(px, py);
                    }
                    ofEndShape(true);
                }
            } else {
                // Open path — draw as a stroke in its colour.
                ofSetColor(col);
                ofSetLineWidth(1.5f);
                for (const auto& outline : p.getOutline()) {
                    if (outline.size() < 2) continue;
                    ofBeginShape();
                    for (const auto& v : outline.getVertices()) {
                        float px = (v.x - m_drawOffsetX) / m_drawWidth  * (float)imgW;
                        float py = (v.y - m_drawOffsetY) / m_drawHeight * (float)imgH;
                        ofVertex(px, py);
                    }
                    ofEndShape(false);
                }
            }
        }
    }

    fbo.end();

    ofPixels pix;
    fbo.readToPixels(pix);
    m_svgPreview.setFromPixels(pix);
}

void ImageToPath::rasterizeLayerFills(float pixPerMM) {
    if (m_drawWidth <= 0.f || m_drawHeight <= 0.f) return;

    int imgW = std::max(1, (int)(m_drawWidth  * pixPerMM));
    int imgH = std::max(1, (int)(m_drawHeight * pixPerMM));

    for (auto e : layerOrder) {
        if (!registry.valid(e)) continue;
        if (!registry.all_of<plotter::fill_raster_component>(e)) continue;
        auto& frc = registry.get<plotter::fill_raster_component>(e);
        if (!frc.enabled) continue;

        auto& pc = registry.get<plotter::paths_component>(e);

        ofFbo fbo;
        ofFbo::Settings s;
        s.width          = imgW;
        s.height         = imgH;
        s.internalformat = GL_RGBA;
        fbo.allocate(s);

        fbo.begin();
        ofClear(255, 255, 255, 255);

        for (size_t pi = 0; pi < pc.paths.size(); ++pi) {
            ofColor col = (pi < pc.pathColors.size())
                ? pc.pathColors[pi]
                : registry.get<ecs::layer_component>(e).color;

            // Map fill colour to greyscale luminance: darker = denser strokes.
            int grey = (int)(0.299f * col.r + 0.587f * col.g + 0.114f * col.b);
            ofSetColor(grey, grey, grey);

            for (const auto& outline : pc.paths[pi].getOutline()) {
                if (!outline.isClosed() || outline.size() < 3) continue;
                ofBeginShape();
                for (const auto& v : outline.getVertices()) {
                    float px = (v.x - m_drawOffsetX) / m_drawWidth  * (float)imgW;
                    float py = (v.y - m_drawOffsetY) / m_drawHeight * (float)imgH;
                    ofVertex(px, py);
                }
                ofEndShape(true);
            }
        }

        fbo.end();

        ofPixels pix;
        fbo.readToPixels(pix);
        pix.setImageType(OF_IMAGE_GRAYSCALE);

        frc.pixels  = pix;
        frc.rasterW = imgW;
        frc.rasterH = imgH;
        frc.drawX   = m_drawOffsetX;
        frc.drawY   = m_drawOffsetY;
        frc.drawW   = m_drawWidth;
        frc.drawH   = m_drawHeight;
    }
}

glm::vec2 ImageToPath::pixelToMM(float px, float py) const {
    if (m_workW <= 0 || m_workH <= 0) return { 0, 0 };
    return {
        m_drawOffsetX + (px / (float)m_workW) * m_drawWidth,
        m_drawOffsetY + (py / (float)m_workH) * m_drawHeight
    };
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
    // If the current generation layer has fill raster data, use it instead of
    // the source image so the PFM operates on the rasterised SVG fills.
    if (m_genEntity != entt::null && registry.valid(m_genEntity) &&
        registry.all_of<plotter::fill_raster_component>(m_genEntity)) {
        auto& frc = registry.get<plotter::fill_raster_component>(m_genEntity);
        if (frc.enabled && frc.rasterW > 0 && frc.rasterH > 0) {
            float res = 0.5f;
            if (registry.all_of<plotter::settings_component>(m_genEntity))
                res = registry.get<plotter::settings_component>(m_genEntity).sketchLines.plotResolution;
            res = std::clamp(res, 0.1f, 2.0f);

            ofPixels pix = frc.pixels;
            if (res < 0.99f)
                pix.resize((int)(pix.getWidth() * res), (int)(pix.getHeight() * res));

            int w = (int)pix.getWidth();
            int h = (int)pix.getHeight();

            if (preprocess.brightness != 0.0f || preprocess.contrast != 0.0f) {
                float b = preprocess.brightness * 128.0f;
                float c = 1.0f + preprocess.contrast;
                for (int i = 0; i < w * h; i++) {
                    float v = pix[i];
                    v = (v - 128.0f) * c + 128.0f + b;
                    pix[i] = (unsigned char)std::clamp(v, 0.0f, 255.0f);
                }
            }
            if (preprocess.invert) {
                for (int i = 0; i < w * h; i++) pix[i] = 255 - pix[i];
            }

            m_workingPixels = pix;
            m_workW       = w;
            m_workH       = h;
            m_drawOffsetX = frc.drawX;
            m_drawOffsetY = frc.drawY;
            m_drawWidth   = frc.drawW;
            m_drawHeight  = frc.drawH;
            return;
        }
    }

    if (!m_sourceLoaded) return;

    ofPixels pix = m_sourceImage.getPixels();
    pix.setImageType(OF_IMAGE_GRAYSCALE);

    // Use the active generation entity's plotResolution
    float res = 0.5f;
    if (m_genEntity != entt::null && registry.valid(m_genEntity))
        res = registry.get<plotter::settings_component>(m_genEntity).sketchLines.plotResolution;
    res = std::clamp(res, 0.1f, 2.0f);
    if (res < 0.99f) {
        pix.resize(
            (int)(pix.getWidth() * res),
            (int)(pix.getHeight() * res));
    }

    int w = (int)pix.getWidth();
    int h = (int)pix.getHeight();

    if (preprocess.brightness != 0.0f || preprocess.contrast != 0.0f) {
        float b = preprocess.brightness * 128.0f;
        float c = 1.0f + preprocess.contrast;
        for (int i = 0; i < w * h; i++) {
            float v = pix[i];
            v = (v - 128.0f) * c + 128.0f + b;
            pix[i] = (unsigned char)std::clamp(v, 0.0f, 255.0f);
        }
    }

    if (preprocess.invert) {
        for (int i = 0; i < w * h; i++)
            pix[i] = 255 - pix[i];
    }

    if (preprocess.threshold >= 0.0f) {
        auto t = (unsigned char)(preprocess.threshold * 255.0f);
        for (int i = 0; i < w * h; i++)
            pix[i] = (pix[i] >= t) ? 255 : 0;
    }

    m_workingPixels = pix;
    m_workW = w;
    m_workH = h;

    glm::vec2 paper = getPaperSizeMM();
    float availW = paper.x - 2.0f * marginMM;
    float availH = paper.y - 2.0f * marginMM;

    float imgAspect  = (float)w / (float)h;
    float areaAspect = availW / availH;

    if (imgAspect > areaAspect) {
        m_drawWidth  = availW;
        m_drawHeight = availW / imgAspect;
    } else {
        m_drawHeight = availH;
        m_drawWidth  = availH * imgAspect;
    }

    m_drawOffsetX = marginMM + (availW - m_drawWidth)  * 0.5f;
    m_drawOffsetY = marginMM + (availH - m_drawHeight) * 0.5f;

    // Sync image overlay rect to the draw area (set once on image load;
    // can be adjusted later via the 2D transform handle in the preview).
    imageOverlayX = m_drawOffsetX; imageOverlayY = m_drawOffsetY;
    imageOverlayW = m_drawWidth;   imageOverlayH = m_drawHeight;
}

// =============================================================
// Generation entry points
// =============================================================

void ImageToPath::generateLayer(entt::entity layerEntity,
    std::function<void(float, const std::string&)> onProgress) {
    if (!registry.valid(layerEntity)) return;
    bool hasFillRaster = registry.all_of<plotter::fill_raster_component>(layerEntity) &&
                         registry.get<plotter::fill_raster_component>(layerEntity).enabled &&
                         registry.get<plotter::fill_raster_component>(layerEntity).rasterW > 0;
    if (!m_sourceLoaded && !hasFillRaster) return;
    m_genEntity = layerEntity;
    preprocessImage();
    runLayerGeneration(layerEntity, onProgress);
    m_genEntity = entt::null;
    rebuildFlatPaths();
    computeStats();
}

void ImageToPath::runLayerGeneration(entt::entity layerEntity,
    std::function<void(float, const std::string&)> onProgress) {
    m_paths.clear();
    m_genEntity = layerEntity;

    auto& settings = registry.get<plotter::settings_component>(layerEntity);
    switch (settings.pfmType) {
        case PFMType::SketchLines:  runSketchLines(onProgress);  break;
        case PFMType::CrossHatch:   runCrossHatch(onProgress);   break;
        case PFMType::Spiral:       runSpiral(onProgress);       break;
        case PFMType::Stippling:    runStippling(onProgress);    break;
        case PFMType::Contours:     runContours(onProgress);     break;
    }

    // Convert scratch ofPolyline buffer → ofPath, store on layer entity
    auto& pc = registry.get<plotter::paths_component>(layerEntity);
    auto& sc = registry.get<plotter::toolpath_stats_component>(layerEntity);

    pc.paths.clear();
    pc.paths.reserve(m_paths.size());
    for (auto& poly : m_paths) {
        ofPath p;
        if (poly.size() > 0) {
            p.moveTo(poly[0]);
            for (size_t vi = 1; vi < poly.size(); vi++)
                p.lineTo(poly[vi]);
            if (poly.isClosed()) p.close();
        }
        p.getOutline(); // pre-warm tessellation cache
        pc.paths.push_back(std::move(p));
    }
    m_paths.clear();

    sc.totalPaths    = (int)pc.paths.size();
    sc.totalPoints   = 0;
    sc.totalDistance = 0;
    for (auto& p : pc.paths) {
        for (const auto& outline : p.getOutline()) {
            sc.totalPoints    += (int)outline.size();
            sc.totalDistance  += outline.getPerimeter();
        }
    }
    sc.estimatedTime = sc.totalDistance / pen.drawSpeed * 60.0f;

    m_genEntity = entt::null;
}

void ImageToPath::generate(std::function<void(float, const std::string&)> onProgress) {
    if (!m_sourceLoaded && !hasFillLayers()) {
        ofLogWarning("ImageToPath") << "No image loaded";
        return;
    }

    if (onProgress) onProgress(0.0f, "Preprocessing...");

    int numLayers = (int)layerOrder.size();
    for (int i = 0; i < numLayers; i++) {
        entt::entity e = layerOrder[i];
        if (!registry.valid(e)) continue;
        if (!isEffectivelyVisible(e)) continue;

        float layerBase = 0.1f + 0.85f * ((float)i / (float)numLayers);
        float layerEnd  = 0.1f + 0.85f * ((float)(i + 1) / (float)numLayers);

        if (onProgress) onProgress(layerBase,
            "Layer " + std::to_string(i + 1) + "/" + std::to_string(numLayers) + "...");

        // m_genEntity drives preprocessImage + PFM settings reads.
        // activeLayer is NOT changed here to avoid a data race with the UI thread.
        m_genEntity = e;
        preprocessImage();

        auto layerProgress = [&](float p, const std::string& msg) {
            if (onProgress)
                onProgress(layerBase + (layerEnd - layerBase) * p, msg);
        };
        runLayerGeneration(e, layerProgress);
        m_genEntity = entt::null;
    }

    rebuildFlatPaths();

    if (onProgress) onProgress(0.97f, "Computing stats...");
    computeStats();

    if (onProgress) onProgress(1.0f, "Done");

    ofLogNotice("ImageToPath") << "Generated " << totalPaths << " paths, "
        << totalPoints << " points, " << (int)totalDistance << " mm total distance";
}

// =============================================================
// Sketch Lines PFM
// =============================================================

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

static float sampleLineAvgLuminance(const std::vector<int>& lum, int w, int h,
                                     int x0, int y0, int x1, int y1,
                                     int& clampedX1, int& clampedY1) {
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
    auto& sketchLines = registry.get<plotter::settings_component>(m_genEntity).sketchLines;

    std::vector<int> lum(m_workW * m_workH);
    for (int i = 0; i < m_workW * m_workH; i++) lum[i] = m_workingPixels[i];

    std::mt19937 rng(42);

    double lumSum = 0;
    for (auto v : lum) lumSum += v;
    double pixelCount = (double)lum.size();
    float initialAvgLum = (float)(lumSum / pixelCount);
    float currentAvgLum = initialAvgLum;

    const float desiredLum = 253.5f;
    float lineDensity = sketchLines.lineDensity / 100.0f;

    float pxPerMmX = (float)m_workW / m_drawWidth;
    float pxPerMmY = (float)m_workH / m_drawHeight;
    float pxPerMm  = (pxPerMmX + pxPerMmY) * 0.5f;
    int minLenPx = std::max(2, (int)(sketchLines.lineMinLength * pxPerMm));
    int maxLenPx = std::max(minLenPx + 1, (int)(sketchLines.lineMaxLength * pxPerMm));

    int angleTests = std::max(3, sketchLines.angleTests);
    float deltaAngle = 360.0f / (float)angleTests;
    int blockW = std::max(4, m_workW / 10);
    int blockH = std::max(4, m_workH / 10);

    int consecutiveFails = 0;
    static constexpr int kMaxFails = 1000;
    int maxSquiggles = 500000;

    while (consecutiveFails < kMaxFails && (int)m_paths.size() < maxSquiggles) {
        float lumProgress = (currentAvgLum - initialAvgLum) / std::max(0.001f, (desiredLum - initialAvgLum) * lineDensity);
        if (lumProgress >= 1.0f) break;

        if (onProgress && ((int)m_paths.size() % 50 == 0)) {
            float progress = 0.1f + 0.8f * std::clamp(lumProgress, 0.0f, 1.0f);
            onProgress(progress, "Sketch Lines: " + std::to_string(m_paths.size()) + " paths");
        }

        int startX, startY;
        findDarkestArea(lum, m_workW, m_workH, blockW, blockH, startX, startY);

        if (lum[startY * m_workW + startX] > 250) break;

        ofPolyline squiggle;
        int curX = startX, curY = startY;
        glm::vec2 mmStart = pixelToMM((float)curX, (float)curY);
        squiggle.addVertex(mmStart.x, mmStart.y, 0);

        int segCount = 0;
        bool failed = false;

        for (int seg = 0; seg < sketchLines.squiggleMax; seg++) {
            float startAngle = std::uniform_real_distribution<float>(0.0f, 360.0f)(rng);
            int lineLen = std::uniform_int_distribution<int>(minLenPx, maxLenPx)(rng);

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

            if (sketchLines.shouldLiftPen && segCount >= sketchLines.squiggleMin) {
                if (bestAvgLum > currentAvgLum) break;
            }
        }

        if (failed || segCount < sketchLines.squiggleMin) {
            int oldVal = lum[startY * m_workW + startX];
            lum[startY * m_workW + startX] = 255;
            lumSum += (255 - oldVal);
            consecutiveFails++;
        } else {
            m_paths.push_back(squiggle);
            consecutiveFails = 0;
        }

        currentAvgLum = (float)(lumSum / pixelCount);
    }
}

// =============================================================
// Cross Hatch PFM
// =============================================================

void ImageToPath::runCrossHatch(std::function<void(float, const std::string&)> onProgress) {
    if (m_workW <= 0 || m_workH <= 0) return;
    auto& crossHatch = registry.get<plotter::settings_component>(m_genEntity).crossHatch;

    float baseAngleProgress = 0.0f;
    float progressPerAngle = crossHatch.useSecondary ? 0.4f : 0.8f;

    auto generateHatchAtAngle = [&](float angleDeg) {
        float angleRad = glm::radians(angleDeg);
        float cosA = cos(angleRad), sinA = sin(angleRad);
        float perpX = -sinA, perpY = cosA;

        float diagonal = sqrt(m_drawWidth * m_drawWidth + m_drawHeight * m_drawHeight);
        float spacing = crossHatch.lineSpacing;
        int numLines = (int)(diagonal / spacing) + 1;

        float cx = m_drawOffsetX + m_drawWidth * 0.5f;
        float cy = m_drawOffsetY + m_drawHeight * 0.5f;
        float sampleStep = std::max(0.3f, spacing * 0.3f);

        for (int i = -numLines / 2; i <= numLines / 2; i++) {
            float offset = (float)i * spacing;
            float ox = cx + perpX * offset;
            float oy = cy + perpY * offset;

            float halfLen = diagonal * 0.6f;
            int numSamples = (int)(diagonal / sampleStep);

            ofPolyline line;
            bool penIsDown = false;
            int gapLength = 0;

            for (int s = 0; s <= numSamples; s++) {
                float t = (float)s / (float)numSamples;
                float px = (ox - cosA * halfLen) + cosA * diagonal * 1.2f * t;
                float py = (oy - sinA * halfLen) + sinA * diagonal * 1.2f * t;

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
                bool shouldDraw = (brightness < (1.0f - crossHatch.minBrightness));

                if (shouldDraw) {
                    line.addVertex(px, py, 0);
                    penIsDown = true;
                    gapLength = 0;
                } else {
                    gapLength++;
                    if (gapLength > 2) {
                        if (penIsDown && line.size() >= 2) {
                            m_paths.push_back(line);
                            line.clear();
                        }
                        penIsDown = false;
                    }
                }
            }

            if (penIsDown && line.size() >= 2)
                m_paths.push_back(line);

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
    if (crossHatch.useSecondary)
        generateHatchAtAngle(crossHatch.angle2);
}

// =============================================================
// Spiral PFM
// =============================================================

void ImageToPath::runSpiral(std::function<void(float, const std::string&)> onProgress) {
    if (m_workW <= 0 || m_workH <= 0) return;
    auto& spiral = registry.get<plotter::settings_component>(m_genEntity).spiral;

    float cx = m_drawOffsetX + m_drawWidth * (spiral.centreX / 100.0f);
    float cy = m_drawOffsetY + m_drawHeight * (spiral.centreY / 100.0f);

    float maxRadius = std::max(m_drawWidth, m_drawHeight) * (spiral.spiralSize / 100.0f) * 0.5f;
    float ringSpacing = spiral.ringSpacing;
    float ampScale = spiral.amplitude;
    float totalRings = maxRadius / ringSpacing;
    float totalAngleDeg = totalRings * 360.0f;
    float velDeg = spiral.velocity;
    float sawFreq = 1.0f / velDeg;

    ofPolyline spiralPath;

    for (float angleDeg = 0; angleDeg < totalAngleDeg; angleDeg += velDeg) {
        float angleRad = glm::radians(angleDeg);
        float r = ringSpacing * angleDeg / 360.0f;
        float baseX = cx + cos(angleRad) * r;
        float baseY = cy + sin(angleRad) * r;
        float brightness = sampleBrightness(baseX, baseY);

        if (spiral.ignoreWhite && brightness > 0.95f) {
            if (spiralPath.size() >= 2) {
                m_paths.push_back(spiralPath);
                spiralPath.clear();
            }
            continue;
        }

        float darkness = 1.0f - brightness;
        float maxAmp = ringSpacing * ampScale * 0.5f;
        float amp = darkness * maxAmp;
        float wave = fmod(angleDeg * sawFreq, 1.0f);
        float triangleWave = (wave < 0.5f) ? (wave * 4.0f - 1.0f) : (3.0f - wave * 4.0f);
        float perpX = cos(angleRad);
        float perpY = sin(angleRad);
        float px = baseX + perpX * amp * triangleWave;
        float py = baseY + perpY * amp * triangleWave;

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

    if (spiralPath.size() >= 2)
        m_paths.push_back(spiralPath);
}

// =============================================================
// Stippling PFM
// =============================================================

void ImageToPath::runStippling(std::function<void(float, const std::string&)> onProgress) {
    if (m_workW <= 0 || m_workH <= 0) return;
    auto& stippling = registry.get<plotter::settings_component>(m_genEntity).stippling;

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

    int targetDots = (int)((m_drawWidth * m_drawHeight) /
        (stippling.dotSpacingMin * stippling.dotSpacingMin));
    targetDots = std::min(targetDots, 100000);

    std::vector<float> rowWeights(m_workH, 0.0f);
    float totalWeight = 0;
    for (int y = 0; y < m_workH; y++) {
        for (int x = 0; x < m_workW; x++) {
            float darkness = 1.0f - (m_workingPixels[y * m_workW + x] / 255.0f);
            rowWeights[y] += darkness * darkness;
        }
        totalWeight += rowWeights[y];
    }

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
        float r = dist01(rng);
        int y = (int)(std::lower_bound(rowCDF.begin(), rowCDF.end(), r) - rowCDF.begin());
        y = std::clamp(y, 0, m_workH - 1);
        int x = std::uniform_int_distribution<int>(0, m_workW - 1)(rng);
        float darkness = 1.0f - (m_workingPixels[y * m_workW + x] / 255.0f);
        if (dist01(rng) > darkness) continue;

        float px = m_drawOffsetX + (float)x / (float)m_workW * m_drawWidth;
        float py = m_drawOffsetY + (float)y / (float)m_workH * m_drawHeight;

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
// Contours PFM
// =============================================================

void ImageToPath::runContours(std::function<void(float, const std::string&)> onProgress) {
    if (m_workW <= 0 || m_workH <= 0) return;
    auto& contours = registry.get<plotter::settings_component>(m_genEntity).contours;
    int W = m_workW, H = m_workH;

    if (onProgress) onProgress(0.12f, "Contours: computing gradients...");

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

    if (maxMag > 0) {
        float scale = 255.0f / maxMag;
        for (auto& v : mag) v *= scale;
    }

    if (onProgress) onProgress(0.2f, "Contours: non-max suppression...");

    std::vector<float> nms(W * H, 0.0f);
    for (int y = 1; y < H - 1; y++) {
        for (int x = 1; x < W - 1; x++) {
            float angle = dir[y * W + x];
            float a = fmod(angle + M_PI, (float)M_PI);
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

    std::vector<bool> visited(W * H, false);

    for (int y = 1; y < H - 1; y++) {
        for (int x = 1; x < W - 1; x++) {
            if (edgeMap[y * W + x] != kStrong) continue;
            if (visited[y * W + x]) continue;

            ofPolyline contour;
            int cx = x, cy = y;

            while (cx >= 1 && cx < W - 1 && cy >= 1 && cy < H - 1
                   && !visited[cy * W + cx]
                   && edgeMap[cy * W + cx] == kStrong) {
                visited[cy * W + cx] = true;
                glm::vec2 mm = pixelToMM((float)cx, (float)cy);
                contour.addVertex(mm.x, mm.y, 0);

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
    totalPaths    = 0;
    totalPoints   = 0;
    totalDistance = 0;
    estimatedTime = 0;

    for (entt::entity e : layerOrder) {
        if (!registry.valid(e)) continue;
        if (!isEffectivelyVisible(e)) continue;
        const auto& sc = registry.get<plotter::toolpath_stats_component>(e);
        totalPaths    += sc.totalPaths;
        totalPoints   += sc.totalPoints;
        totalDistance += sc.totalDistance;
        estimatedTime += sc.estimatedTime;
    }

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
