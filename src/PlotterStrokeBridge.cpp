#include "PlotterStrokeBridge.h"
#include <ofxEnTTKit/src/components/layer_components.h>
#include <map>

namespace plotter {

namespace {

ofPath polylineToOfPath(const ofPolyline& poly) {
	ofPath p;
	if (poly.size() == 0) return p;
	p.moveTo(poly[0]);
	for (size_t vi = 1; vi < poly.size(); vi++) {
		p.lineTo(poly[vi]);
	}
	if (poly.isClosed()) p.close();
	p.getOutline();
	return p;
}

} // namespace

plotproc::StrokeDocument strokeDocumentFromEngine(const ImageToPath& engine, float curveResolution) {
	plotproc::StrokeDocument doc;
	static constexpr int kDefaultCurveResolution = 20;
	ofSetCurveResolution(std::max(3, (int)curveResolution));

	int layerIndex = 0;
	for (entt::entity e : engine.layerOrder) {
		if (!engine.registry.valid(e)) continue;
		if (!engine.isEffectivelyVisible(e)) continue;
		if (!engine.registry.all_of<plotter::paths_component>(e)) continue;

		const auto& pc = engine.registry.get<plotter::paths_component>(e);
		ofColor layerColor = ofColor::white;
		if (engine.registry.all_of<ecs::layer_component>(e)) {
			layerColor = engine.registry.get<ecs::layer_component>(e).color;
		}

		for (size_t pi = 0; pi < pc.paths.size(); ++pi) {
			for (const auto& outline : pc.paths[pi].getOutline()) {
				if (outline.size() < 2) continue;
				doc.paths.push_back(outline);
				plotproc::StrokeMeta meta;
				meta.closed = outline.isClosed();
				meta.layerId = layerIndex;
				meta.layerEntityRaw = (uint32_t)e;
				meta.sourcePathIndex = (int)pi;
				if (pi < pc.pathColors.size()) {
					meta.color = pc.pathColors[pi];
				} else {
					meta.color = layerColor;
				}
				doc.meta.push_back(meta);
			}
		}
		++layerIndex;
	}

	ofSetCurveResolution(kDefaultCurveResolution);
	doc.rebuildBounds();
	return doc;
}

void writeStrokeDocumentToEngine(ImageToPath& engine,
                                 const plotproc::StrokeDocument& doc,
                                 bool recomputeStats) {
	std::map<uint32_t, std::vector<ofPath>> layerPaths;
	std::map<uint32_t, std::vector<ofColor>> layerColors;

	for (size_t i = 0; i < doc.paths.size(); ++i) {
		const uint32_t key = (i < doc.meta.size()) ? doc.meta[i].layerEntityRaw : 0;
		layerPaths[key].push_back(polylineToOfPath(doc.paths[i]));
		const ofColor c = (i < doc.meta.size()) ? doc.meta[i].color : ofColor::white;
		layerColors[key].push_back(c);
	}

	for (auto& kv : layerPaths) {
		const entt::entity e = (entt::entity)kv.first;
		if (!engine.registry.valid(e)) continue;
		if (!engine.registry.all_of<plotter::paths_component>(e)) continue;

		auto& pc = engine.registry.get<plotter::paths_component>(e);
		pc.paths = std::move(kv.second);
		pc.pathColors = std::move(layerColors[kv.first]);

		auto& sc = engine.registry.get<plotter::toolpath_stats_component>(e);
		sc.totalPaths = (int)pc.paths.size();
		sc.totalPoints = 0;
		sc.totalDistance = 0;
		for (auto& p : pc.paths) {
			for (const auto& outline : p.getOutline()) {
				sc.totalPoints += (int)outline.size();
				sc.totalDistance += outline.getPerimeter();
			}
		}
		sc.estimatedTime = sc.totalDistance / std::max(1.0f, engine.pen.drawSpeed) * 60.0f;
	}

	engine.rebuildFlatPaths();
	if (recomputeStats) engine.refreshStats();
}

} // namespace plotter
