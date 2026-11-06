#include "config.h"
#include "GStreamerHolePunchQuirkDreambox.h"

#if USE(GSTREAMER)

#include "GStreamerCommon.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace WebCore {

GstElement* GStreamerHolePunchQuirkDreambox::createHolePunchVideoSink(bool, const MediaPlayer*)
{
    return makeGStreamerElement("dvbvideosink"_s, "DreamboxVideoSink"_s);
}

static void writeVideoProc(const char* name, int value)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/stb/vmpeg/0/dst_%s", name);
    if (FILE* f = fopen(path, "w")) {
        fprintf(f, "%08x\n", static_cast<unsigned>(value));
        fclose(f);
    }
}

bool GStreamerHolePunchQuirkDreambox::setHolePunchVideoRectangle(GstElement*, const IntRect& rect)
{
    static int originX = 0, originY = 0, screenW = 1920, screenH = 1080;
    static bool configured = false;
    static IntRect lastRect;
    if (!configured) {
        configured = true;
        if (const char* origin = getenv("WPE_DREAM_VIDEO_ORIGIN"))
            sscanf(origin, "%d,%d", &originX, &originY);
        if (const char* screen = getenv("WPE_DREAM_VIDEO_SCREEN"))
            sscanf(screen, "%dx%d", &screenW, &screenH);
        if (screenW <= 0 || screenH <= 0) {
            screenW = 1920;
            screenH = 1080;
        }
    }
    if (rect == lastRect)
        return true;
    lastRect = rect;

    // view pixels -> screen pixels -> the driver's 720x576 output space (clamped to it)
    int left = (originX + rect.x()) * 720 / screenW;
    int top = (originY + rect.y()) * 576 / screenH;
    int right = (originX + rect.maxX()) * 720 / screenW;
    int bottom = (originY + rect.maxY()) * 576 / screenH;
    left = std::clamp(left, 0, 720);
    top = std::clamp(top, 0, 576);
    right = std::clamp(right, left, 720);
    bottom = std::clamp(bottom, top, 576);

    GST_DEBUG("Dreambox video plane: view rect %d,%d %dx%d -> dst %d,%d %dx%d", rect.x(), rect.y(), rect.width(), rect.height(), left, top, right - left, bottom - top);
    writeVideoProc("left", left);
    writeVideoProc("top", top);
    writeVideoProc("width", right - left);
    writeVideoProc("height", bottom - top);
    writeVideoProc("apply", 1);
    return true;
}

} // namespace WebCore

#endif // USE(GSTREAMER)
