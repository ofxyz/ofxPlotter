#pragma once

#include <string>
#include <vector>

namespace plotter {

enum class ZoneKind { Cleaning, ToolChange, Custom };

struct ZonePosition {
    float       x = 0.f;
    float       y = 0.f;
    int         positionIndex = 0;
    std::string label;
};

struct MaintenanceZone {
    std::string              id;
    std::string              name;
    ZoneKind                 kind = ZoneKind::Cleaning;
    float                    x = 0.f, y = 0.f, w = 40.f, h = 40.f;
    std::vector<ZonePosition> positions;
    std::string              snippetPath;
};

enum class InjectionMode { Detour, Inline };

struct InjectionRule {
    bool          enabled = true;
    InjectionMode mode    = InjectionMode::Detour;
    float         intervalMm = 500.f;
    std::string   zoneId;
    int           positionIndex = 0;
    std::string   snippetPath;
    bool          countTravel = false;
};

struct PlotterZoneStore {
    std::vector<MaintenanceZone> zones;
    std::vector<InjectionRule>     injectionRules;

    static const char* defaultRelativePath() { return "settings/PlotterZones.json"; }

    void load(const std::string& jsonPath = "", bool jsonPathIsAbsolute = false);
    void save(const std::string& jsonPath = "", bool jsonPathIsAbsolute = false) const;

    MaintenanceZone*       findZone(const std::string& id);
    const MaintenanceZone* findZone(const std::string& id) const;

    std::string makeUniqueZoneId() const;
};

const char* zoneKindLabel(ZoneKind k);

} // namespace plotter
