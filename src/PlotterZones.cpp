#include "PlotterZones.h"

#include "ofFileUtils.h"
#include "ofJson.h"
#include "ofLog.h"
#include "ofUtils.h"

#include <algorithm>

namespace plotter {

namespace {

std::string resolveJsonPath(const std::string& jsonPath, bool jsonPathIsAbsolute) {
    if (jsonPath.empty()) {
        return ofToDataPath(PlotterZoneStore::defaultRelativePath(), true);
    }
    return jsonPathIsAbsolute ? jsonPath : ofToDataPath(jsonPath, true);
}

ZoneKind kindFromString(const std::string& s) {
    const std::string l = ofToLower(s);
    if (l == "toolchange" || l == "tool_change") return ZoneKind::ToolChange;
    if (l == "custom") return ZoneKind::Custom;
    return ZoneKind::Cleaning;
}

std::string kindToString(ZoneKind k) {
    switch (k) {
        case ZoneKind::ToolChange: return "toolchange";
        case ZoneKind::Custom:     return "custom";
        default:                   return "cleaning";
    }
}

InjectionMode modeFromString(const std::string& s) {
    return ofToLower(s) == "inline" ? InjectionMode::Inline : InjectionMode::Detour;
}

std::string modeToString(InjectionMode m) {
    return m == InjectionMode::Inline ? "inline" : "detour";
}

} // namespace

const char* zoneKindLabel(ZoneKind k) {
    switch (k) {
        case ZoneKind::ToolChange: return "Tool change";
        case ZoneKind::Custom:     return "Custom";
        default:                   return "Cleaning";
    }
}

MaintenanceZone* PlotterZoneStore::findZone(const std::string& id) {
    for (auto& z : zones) {
        if (z.id == id) return &z;
    }
    return nullptr;
}

const MaintenanceZone* PlotterZoneStore::findZone(const std::string& id) const {
    for (const auto& z : zones) {
        if (z.id == id) return &z;
    }
    return nullptr;
}

std::string PlotterZoneStore::makeUniqueZoneId() const {
    int n = (int)zones.size();
    std::string id;
    do {
        id = "zone_" + std::to_string(++n);
    } while (findZone(id));
    return id;
}

void PlotterZoneStore::load(const std::string& jsonPath, bool jsonPathIsAbsolute) {
    const std::string resolved = resolveJsonPath(jsonPath, jsonPathIsAbsolute);
    if (!ofFile::doesFileExist(resolved, false)) {
        ofLogNotice("plotter::PlotterZoneStore") << "no file at " << resolved << ", using defaults";
        return;
    }

    const ofJson j = ofLoadJson(resolved);
    if (!j.is_object()) {
        ofLogWarning("plotter::PlotterZoneStore") << "invalid JSON at " << resolved;
        return;
    }

    zones.clear();
    if (j.contains("zones") && j["zones"].is_array()) {
        for (const auto& zj : j["zones"]) {
            if (!zj.is_object()) continue;
            MaintenanceZone z;
            if (zj.contains("id") && zj["id"].is_string()) z.id = zj["id"];
            if (zj.contains("name") && zj["name"].is_string()) z.name = zj["name"];
            if (zj.contains("kind") && zj["kind"].is_string())
                z.kind = kindFromString(zj["kind"]);
            auto getf = [&](const char* key, float& out) {
                if (zj.contains(key) && zj[key].is_number()) out = zj[key].get<float>();
            };
            getf("x", z.x); getf("y", z.y); getf("w", z.w); getf("h", z.h);
            if (zj.contains("snippetPath") && zj["snippetPath"].is_string())
                z.snippetPath = zj["snippetPath"].get<std::string>();
            if (zj.contains("positions") && zj["positions"].is_array()) {
                for (const auto& pj : zj["positions"]) {
                    if (!pj.is_object()) continue;
                    ZonePosition p;
                    if (pj.contains("x") && pj["x"].is_number()) p.x = pj["x"];
                    if (pj.contains("y") && pj["y"].is_number()) p.y = pj["y"];
                    if (pj.contains("positionIndex") && pj["positionIndex"].is_number_integer())
                        p.positionIndex = pj["positionIndex"];
                    if (pj.contains("label") && pj["label"].is_string())
                        p.label = pj["label"];
                    z.positions.push_back(p);
                }
            }
            if (z.id.empty()) z.id = "zone_" + std::to_string(zones.size());
            if (z.name.empty()) z.name = z.id;
            zones.push_back(std::move(z));
        }
    }

    injectionRules.clear();
    if (j.contains("injectionRules") && j["injectionRules"].is_array()) {
        for (const auto& rj : j["injectionRules"]) {
            if (!rj.is_object()) continue;
            InjectionRule r;
            if (rj.contains("enabled") && rj["enabled"].is_boolean())
                r.enabled = rj["enabled"];
            if (rj.contains("mode") && rj["mode"].is_string())
                r.mode = modeFromString(rj["mode"]);
            if (rj.contains("intervalMm") && rj["intervalMm"].is_number())
                r.intervalMm = rj["intervalMm"];
            if (rj.contains("zoneId") && rj["zoneId"].is_string())
                r.zoneId = rj["zoneId"];
            if (rj.contains("positionIndex") && rj["positionIndex"].is_number_integer())
                r.positionIndex = rj["positionIndex"];
            if (rj.contains("snippetPath") && rj["snippetPath"].is_string())
                r.snippetPath = rj["snippetPath"];
            if (rj.contains("countTravel") && rj["countTravel"].is_boolean())
                r.countTravel = rj["countTravel"];
            injectionRules.push_back(std::move(r));
        }
    }
}

void PlotterZoneStore::save(const std::string& jsonPath, bool jsonPathIsAbsolute) const {
    const std::string resolved = resolveJsonPath(jsonPath, jsonPathIsAbsolute);
    ofFilePath::createEnclosingDirectory(ofFilePath::getEnclosingDirectory(resolved), false, true);

    ofJson j;
    ofJson zonesJ = ofJson::array();
    for (const auto& z : zones) {
        ofJson zj;
        zj["id"]   = z.id;
        zj["name"] = z.name;
        zj["kind"] = kindToString(z.kind);
        zj["x"]    = z.x;
        zj["y"]    = z.y;
        zj["w"]    = z.w;
        zj["h"]    = z.h;
        zj["snippetPath"] = z.snippetPath;
        ofJson posJ = ofJson::array();
        for (const auto& p : z.positions) {
            ofJson pj;
            pj["x"] = p.x;
            pj["y"] = p.y;
            pj["positionIndex"] = p.positionIndex;
            pj["label"]         = p.label;
            posJ.push_back(pj);
        }
        zj["positions"] = posJ;
        zonesJ.push_back(zj);
    }
    j["zones"] = zonesJ;

    ofJson rulesJ = ofJson::array();
    for (const auto& r : injectionRules) {
        ofJson rj;
        rj["enabled"]        = r.enabled;
        rj["mode"]           = modeToString(r.mode);
        rj["intervalMm"]     = r.intervalMm;
        rj["zoneId"]         = r.zoneId;
        rj["positionIndex"]  = r.positionIndex;
        rj["snippetPath"]    = r.snippetPath;
        rj["countTravel"]    = r.countTravel;
        rulesJ.push_back(rj);
    }
    j["injectionRules"] = rulesJ;

    if (!ofSavePrettyJson(resolved, j)) {
        ofLogError("plotter::PlotterZoneStore") << "failed to save " << resolved;
    }
}

} // namespace plotter
