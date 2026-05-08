# ofxPlotter

Pen-plotter toolchain for openFrameworks. Converts raster images into mm-space toolpaths using several artistic Path Finding Modules (PFMs), manages multi-layer pen jobs, and exports plotter-aware G-code for GRBL-based machines.

## Features

- **Five PFMs** — Sketch Lines, Cross-Hatch, Spiral, Stippling, Contours
- **SVG import** — load vector outlines directly, skipping the raster step
- **Multi-layer jobs** — each layer has its own PFM, settings, brush, and colour
- **G-code export** — machine-oriented output with pen Z, feed rates, slow-travel mode
- **Per-layer stats** — path count, total distance (mm), estimated time

## Dependencies

| Addon | Role |
|-------|------|
| [ofxGCode](https://github.com/ofxyz/ofxGCode) | Path-to-G-code serialisation |
| [ofxSvg](https://github.com/openframeworks/openFrameworks/tree/master/addons/ofxSvg) | SVG vector import (bundled with OF) |
| [ofxGrbl](https://github.com/ofxyz/ofxGrbl) | Machine connection (examples / apps) |

## Quick start

### Sketch lines

```cpp
#include "ofxPlotter.h"

ImageToPath engine;
engine.paperSize        = PaperSize::A4;
engine.paperOrientation = PaperOrientation::Portrait;
engine.marginMM         = 10.0f;

engine.getActiveLayer().pfmType = PFMType::SketchLines;

engine.loadImage("photo.jpg");
engine.generate([](float progress, const std::string& msg) {
    ofLog() << msg << " " << int(progress * 100) << "%";
});

std::string gcode = plotter::toGCode(engine);
ofSaveFile("output.gcode", gcode);
```

### Parallel line fill at a fixed angle

`CrossHatch` with `useSecondary = false` gives a single family of evenly-spaced parallel lines at any angle — closer to a traditional line fill than a hatch.

```cpp
#include "ofxPlotter.h"

ImageToPath engine;
engine.paperSize        = PaperSize::A4;
engine.paperOrientation = PaperOrientation::Portrait;
engine.marginMM         = 10.0f;

PlotterLayer& layer = engine.getActiveLayer();
layer.pfmType = PFMType::CrossHatch;
layer.crossHatch.angle1        = 30.0f;  // degrees — change to taste
layer.crossHatch.useSecondary  = false;  // single direction only
layer.crossHatch.lineSpacing   = 1.5f;   // mm between lines
layer.crossHatch.minBrightness = 0.1f;   // skip near-white areas

engine.loadImage("photo.jpg");
engine.generate();

std::string gcode = plotter::toGCode(engine);
ofSaveFile("output.gcode", gcode);
```

To add a second pass at a different angle (true cross-hatch), call `addLayer()`, set `angle2` on it, and generate again — or simply set `useSecondary = true` and both `angle1`/`angle2` are drawn in one pass.

## API reference

### `ImageToPath`

The main engine. All settings are public members for easy UI binding.

#### I/O

```cpp
bool loadImage(const std::string& path);
void setImage(const ofPixels& pixels);
bool loadVectorSVG(const std::string& path); // skips PFM; paths are the SVG outlines
bool hasImage() const;
const ofImage& getSourceImage() const;
const ofImage& getWorkingImage() const;
```

#### Generation

```cpp
// Generate all visible layers
void generate(std::function<void(float, const std::string&)> onProgress = nullptr);

// Regenerate a single layer
void generateLayer(int layerIdx, std::function<void(float, const std::string&)> onProgress = nullptr);
```

#### Path access

```cpp
const std::vector<ofPolyline>& getPaths() const; // flattened across visible layers
void rebuildFlatPaths();
glm::vec2 getPaperSizeMM() const;
ofColor getPathColor(int flatIdx) const;
```

#### Paper settings

```cpp
PaperSize        paperSize;        // A4, A3, A2, A1, A0, Custom
PaperOrientation paperOrientation; // Portrait, Landscape
float            customWidth;      // mm, used when paperSize == Custom
float            customHeight;
float            marginMM;
```

#### Preprocessing

```cpp
PreprocessSettings preprocess;
// .brightness  [-1, 1]
// .contrast    [-1, 1]
// .threshold   [-1 = off, 0–1]
// .invert
// .blur        [0 = off]
// .edgeDetect
```

#### Layers

```cpp
std::vector<PlotterLayer> layers;
int currentLayer;

PlotterLayer& getActiveLayer();
int  addLayer(const std::string& name = "");
void removeLayer(int idx); // always keeps ≥1 layer
```

#### Brush library

```cpp
std::vector<BrushPreset> brushes; // default nib presets loaded on construction
int addBrush(const BrushPreset& b);
```

#### Aggregate stats

```cpp
int   totalPaths;
int   totalPoints;
float totalDistance;  // mm
float estimatedTime;  // seconds
```

---

### `PenSettings`

Machine / Z-axis parameters. Assign to `engine.pen`.

| Field | Default | Description |
|-------|---------|-------------|
| `penDownZ` | `0.0` | Z height (mm) when pen touches paper |
| `penUpZ` | `5.0` | Z height (mm) for safe travel |
| `drawSpeed` | `3000` | XY feed rate (mm/min) while drawing |
| `travelSpeed` | `6000` | Travel feed rate — only used when `slowTravels` is true |
| `penWidth` | `0.3` | Tip width (mm), used for spacing estimates |
| `slowTravels` | `false` | Emit `G1 F<travelSpeed>` for travel moves instead of `G0` |

`slowTravels` is useful for machines with poorly-tuned `$110/$111/$112` max-rate settings, or for Marlin-style firmware that honours `F` on travel.

---

### `plotter::toGCode`

```cpp
// All visible layers merged into draw order
std::string plotter::toGCode(const ImageToPath& engine);

// Single layer (returns "" if index is out of range)
std::string plotter::toGCodeForLayer(const ImageToPath& engine, int layerIdx);
```

Emitted programs begin with `G21` (mm mode) + `G90` (absolute), lift the pen on the first move, and include comments with path counts, total distance, and estimated time.

---

### PFM settings

Each `PlotterLayer` carries one settings struct per PFM; only the active one is used during generation.

#### `SketchLinesSettings`
Darkest-block search + squiggle segments with incremental luminance erase (DrawingBotV3-inspired).

| Field | Default | Description |
|-------|---------|-------------|
| `lineMinLength` | `5.0` | Shortest segment (mm) |
| `lineMaxLength` | `80.0` | Longest segment (mm) |
| `angleTests` | `18` | Directions tested per block |
| `lineDensity` | `75.0` | Target path density |
| `eraseMin/Max` | `50/125` | Luminance erase range |
| `squiggleMin/Max` | `3/200` | Segments per squiggle |
| `shouldLiftPen` | `true` | Lift between squiggles |
| `plotResolution` | `0.5` | mm per sample step |

#### `CrossHatchSettings`
Angled parallel lines sampled in mm; splits polylines in bright regions.

| Field | Default |
|-------|---------|
| `angle1` | `45°` |
| `angle2` | `135°` |
| `useSecondary` | `true` |
| `lineSpacing` | `2.0 mm` |
| `minBrightness` | `0.0` |

#### `SpiralSettings`
Archimedean spiral with brightness-modulated perpendicular zigzag.

| Field | Default |
|-------|---------|
| `ringSpacing` | `3.0 mm` |
| `amplitude` | `1.0` |
| `velocity` | `5.0` |
| `centreX/Y` | `50%` |
| `spiralSize` | `100%` |
| `ignoreWhite` | `true` |

#### `StipplingSettings`
Importance-sampled dots drawn as small closed polylines.

| Field | Default |
|-------|---------|
| `dotSpacingMin/Max` | `0.5 / 5.0 mm` |
| `dotRadius` | `0.2 mm` |
| `iterations` | `50` |

#### `ContourSettings`
Sobel edge detection → non-maximum suppression → hysteresis → chain trace → Douglas–Peucker simplify.

| Field | Default |
|-------|---------|
| `cannyLow` | `50` |
| `cannyHigh` | `150` |
| `minContourLen` | `5.0 mm` |

---

## Examples

### `example-kit`

Full image-to-plotter workflow with an ofxKit ImGui UI. Three dockable panels:

- **Serial / Machine** — connect to the GRBL machine (via `ofxGrblKit`), configure port and baud.
- **Image & Settings** — load a raster image or SVG, set paper size / orientation / margins, tweak preprocessing (brightness, contrast, threshold, blur, edge detect), pick a PFM and adjust its parameters, configure pen Z heights and feed rates, generate paths, and send the job.
- **Preview** — live vector preview of the generated paths, scaled to fit, drawn directly into an ImGui DrawList. Paths are colour-coded per layer.

The **Plotter** menu bar group provides feed hold, queue pause/resume, and clear, mirroring the controls in the serial window.

Dependencies: `ofxKit`, `ofxPlotter`, `ofxGrbl`, `ofxGrblKit`.

### `example_random_walk`

Live streaming random walk to a connected GRBL plotter (or simulation mode). Demonstrates `PenSettings`, `ofxGrbl` queue management, and `ofxGrblKit`'s serial/machine UI window. The pen never lifts between whole segments — it just picks a new heading at each boundary and carries on, keeping the GRBL queue topped up to avoid stalls.

Dependencies: `ofxKit`, `ofxPlotter`, `ofxGrbl`, `ofxGrblKit`.
