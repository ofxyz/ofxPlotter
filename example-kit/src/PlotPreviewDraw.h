#pragma once

class ImageToPath;

/// Paper-space drawing helpers for Plot Preview (grid, playback paths).
namespace plotPreview {

void drawPaperGrid(float paperW, float paperH, float contentZoom);

/// Semi-transparent brush-width dots along paths (up to @p maxPathIndex polylines).
void drawBrushEstimation(const ImageToPath& engine, int maxPathIndex);

} // namespace plotPreview
