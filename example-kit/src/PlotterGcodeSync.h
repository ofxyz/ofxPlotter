#pragma once

#include <algorithm>
#include <string>

namespace plotterGcodeSync {

inline int countLines(const std::string& text)
{
    if (text.empty()) return 1;
    int n = 1;
    for (char c : text) {
        if (c == '\n') ++n;
    }
    return n;
}

inline int playbackToLine(float playback01, int lineCount)
{
    if (lineCount <= 1) return 0;
    return std::clamp((int)(playback01 * (lineCount - 1) + 0.5f), 0, lineCount - 1);
}

inline float lineToPlayback(int line, int lineCount)
{
    if (lineCount <= 1) return 0.f;
    return std::clamp((float)line / (float)(lineCount - 1), 0.f, 1.f);
}

} // namespace plotterGcodeSync
