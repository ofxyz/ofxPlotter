#pragma once

#include "ImageToPath.h"
#include <ofxPlotProcessors/src/ofxPlotProcessors.h>

namespace plotter {

/// Build a flat stroke document from engine paths (ofPath tessellated).
plotproc::StrokeDocument strokeDocumentFromEngine(const ImageToPath& engine,
                                                  float curveResolution = 20.f);

/// Apply processed polylines back into layer ofPath storage and rebuild flat cache.
void writeStrokeDocumentToEngine(ImageToPath& engine,
                                 const plotproc::StrokeDocument& doc,
                                 bool recomputeStats = true);

} // namespace plotter
