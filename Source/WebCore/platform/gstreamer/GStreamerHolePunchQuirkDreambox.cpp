#include "config.h"
#include "GStreamerHolePunchQuirkDreambox.h"

#if USE(GSTREAMER)

#include "GStreamerCommon.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <wtf/Lock.h>
#include <wtf/Locker.h>

namespace WebCore {

static Lock decoderLeaseLock;
static int decoderLeaseFd WTF_GUARDED_BY_LOCK(decoderLeaseLock) = -1;
static unsigned decoderLeaseCount WTF_GUARDED_BY_LOCK(decoderLeaseLock) = 0;

bool acquireDreamboxDecoder()
{
    const char* path = getenv("WPE_DREAM_HANDOVER");
    if (!path || !*path)
        return true; // nothing to negotiate

    Locker locker { decoderLeaseLock };
    if (decoderLeaseFd >= 0) {
        decoderLeaseCount++;
        return true;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return false;
    struct sockaddr_un address = { };
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, path, sizeof(address.sun_path) - 1);
    char request[48];
    int requestLength = snprintf(request, sizeof(request), "acquire %d\n", static_cast<int>(getpid()));
    if (connect(fd, reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) < 0 || write(fd, request, requestLength) != requestLength) {
        close(fd);
        return false;
    }
    // The embedder has to get enigma2 to stop the live service first: give it time, but not forever.
    struct pollfd pfd = { fd, POLLIN, 0 };
    char reply[8] = { };
    if (poll(&pfd, 1, 10000) <= 0 || read(fd, reply, sizeof(reply) - 1) <= 0 || strncmp(reply, "ok", 2)) {
        GST_WARNING("Dreambox decoder was not granted by the embedder");
        close(fd);
        return false;
    }
    decoderLeaseFd = fd;
    decoderLeaseCount = 1;
    return true;
}

static void releaseDreamboxDecoder()
{
    Locker locker { decoderLeaseLock };
    if (!decoderLeaseCount || --decoderLeaseCount)
        return;
    close(decoderLeaseFd); // the embedder sees the end of the connection and gives the live service back
    decoderLeaseFd = -1;
}

GstElement* leaseDreamboxDecoder(GstElement* sink)
{
    if (!sink)
        return nullptr;
    if (getenv("WPE_DREAM_HANDOVER"))
        g_object_weak_ref(G_OBJECT(sink), [](gpointer, GObject*) { releaseDreamboxDecoder(); }, nullptr);
    return sink;
}

GstElement* GStreamerHolePunchQuirkDreambox::createHolePunchVideoSink(bool, const MediaPlayer*)
{
    if (!acquireDreamboxDecoder())
        return nullptr;
    GstElement* sink = makeGStreamerElement("dvbvideosink"_s, "DreamboxVideoSink"_s);
    if (!sink) {
        releaseDreamboxDecoder();
        return nullptr;
    }
    return leaseDreamboxDecoder(sink);
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
    static double scale = 1.0;
    static bool configured = false;
    static IntRect lastRect;
    if (!configured) {
        configured = true;
        if (const char* origin = getenv("WPE_DREAM_VIDEO_ORIGIN"))
            sscanf(origin, "%d,%d", &originX, &originY);
        if (const char* screen = getenv("WPE_DREAM_VIDEO_SCREEN"))
            sscanf(screen, "%dx%d", &screenW, &screenH);
        if (const char* factor = getenv("WPE_DREAM_VIDEO_SCALE")) {
            scale = atof(factor);
            if (scale <= 0)
                scale = 1.0;
        }
        if (screenW <= 0 || screenH <= 0) {
            screenW = 1920;
            screenH = 1080;
        }
    }
    if (rect == lastRect)
        return true;
    lastRect = rect;

    // view pixels -> screen pixels (the embedder may show the view scaled up, e.g. a 1280x720 page on a 1920x1080 screen) -> the
    // driver's 720x576 output space (clamped to it)
    int left = static_cast<int>((originX + rect.x() * scale) * 720 / screenW);
    int top = static_cast<int>((originY + rect.y() * scale) * 576 / screenH);
    int right = static_cast<int>((originX + rect.maxX() * scale) * 720 / screenW);
    int bottom = static_cast<int>((originY + rect.maxY() * scale) * 576 / screenH);
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
