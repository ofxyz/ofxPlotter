#pragma once

#include <MachinePrefs.h>
#include <glm/vec2.hpp>

namespace plotter {

/// Viewport/content coordinate helpers for machine-bed preview and export.
struct BedView {
    grbl::Envelope  envelope;
    grbl::BedLayout bed;

    glm::vec2 contentSize() const {
        return {envelope.spanX(), envelope.spanY()};
    }

    glm::vec2 paperOriginContent() const {
        return {bed.paperOriginX - envelope.minX, bed.paperOriginY - envelope.minY};
    }

    glm::vec2 paperToContent(float px, float py) const {
        return paperOriginContent() + glm::vec2(px, py);
    }

    glm::vec2 paperToMachine(float px, float py) const {
        return {bed.paperOriginX + px, bed.paperOriginY + py};
    }

    glm::vec2 machineToContent(float mx, float my) const {
        return {mx - envelope.minX, my - envelope.minY};
    }

    glm::vec2 contentToMachine(float cx, float cy) const {
        return {cx + envelope.minX, cy + envelope.minY};
    }

    static BedView fromPrefs(const grbl::MachinePrefs& prefs) {
        return {prefs.envelope, prefs.bed};
    }
};

} // namespace plotter
