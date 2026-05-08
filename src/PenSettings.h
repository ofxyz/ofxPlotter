#pragma once

/// Hardware / G-code output settings for a pen plotter Z-axis tool.
///
/// These values describe the physical machine rather than the artwork, so they
/// live in ofxPlotter (machine-aware) rather than ofxGCode (path representation).
struct PenSettings {
    float penDownZ    = 0.0f;   ///< Z height (mm) when pen touches paper.
    float penUpZ      = 5.0f;   ///< Z height (mm) for safe travel.
    float drawSpeed   = 3000.0f;///< XY feed rate (mm/min) while drawing.
    float travelSpeed = 6000.0f;///< Travel feed rate (mm/min) — only used when slowTravels is true.
    float penWidth    = 0.3f;   ///< Physical pen tip width (mm), used for path spacing estimates.

    /// GRBL executes G0 at its internal max rates ($110/$111/$112) and ignores
    /// any F word, so travelSpeed does nothing with standard rapids.
    /// Set true to emit G1 F<travelSpeed> instead, which forces the feed rate
    /// on travel moves (useful for machines that do not have properly tuned
    /// max-rate settings, or for Marlin-style firmware that honours G1 F).
    bool slowTravels = false;
};
