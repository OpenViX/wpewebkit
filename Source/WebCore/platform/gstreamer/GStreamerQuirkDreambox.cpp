#include "config.h"
#include "GStreamerQuirkDreambox.h"

#if USE(GSTREAMER)

#include "GStreamerCommon.h"

namespace WebCore {

bool GStreamerQuirkDreambox::isPlatformSupported() const
{
    GRefPtr videoSink = adoptGRef(gst_element_factory_find("dvbvideosink"));
    GRefPtr audioSink = adoptGRef(gst_element_factory_find("dvbaudiosink"));
    return videoSink && audioSink;
}

GstElement* GStreamerQuirkDreambox::createAudioSink()
{
    return makeGStreamerElement("dvbaudiosink"_s);
}

std::optional<bool> GStreamerQuirkDreambox::isHardwareAccelerated(GstElementFactory* factory)
{
    auto view = StringView::fromLatin1(GST_OBJECT_NAME(factory));
    if (view.startsWith("dvb"_s))
        return true;

    return std::nullopt;
}

} // namespace WebCore

#endif // USE(GSTREAMER)
