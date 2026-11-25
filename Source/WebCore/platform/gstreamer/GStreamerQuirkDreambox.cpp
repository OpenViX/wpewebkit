#include "config.h"
#include "GStreamerQuirkDreambox.h"

#if USE(GSTREAMER)

#include "GStreamerCommon.h"
#include "GStreamerHolePunchQuirkDreambox.h"

namespace WebCore {

bool GStreamerQuirkDreambox::isPlatformSupported() const
{
    GRefPtr videoSink = adoptGRef(gst_element_factory_find("dvbvideosink"));
    GRefPtr audioSink = adoptGRef(gst_element_factory_find("dvbaudiosink"));
    return videoSink && audioSink;
}

GstElement* GStreamerQuirkDreambox::createAudioSink()
{
    // denied (or missing) decoder: nullptr lets playbin pick its own sink, i.e. software audio
    if (!acquireDreamboxDecoder())
        return nullptr;
    GstElement* sink = makeGStreamerElement("dvbaudiosink"_s);
    if (!sink)
        return nullptr;
    return leaseDreamboxDecoder(sink);
}

// The hardware decoders cannot drop the frames that lead from a keyframe to an accurate seek target the way a software
// decoder does: after a FLUSH | ACCURATE seek the output stalled. enigma2's own player (eServiceMP3) seeks to the nearest
// keyframe with FLUSH | KEY_UNIT, which the sinks handle: the decoder restarts at the keyframe and pause/resume work.
GstSeekFlags GStreamerQuirkDreambox::adjustSeekFlags(GstSeekFlags flags) const
{
    return static_cast<GstSeekFlags>((flags & ~GST_SEEK_FLAG_ACCURATE) | GST_SEEK_FLAG_KEY_UNIT);
}

// What the sinks report to position queries is what has been pushed into the driver, which for a small clip is the whole
// stream a second after it started. The real position of the decoder is available as an action signal, and enigma2's
// own player (eServiceMP3::getPlayPosition) takes it from the audio sink when there is one, from the video sink otherwise.
std::optional<GstClockTime> GStreamerQuirkDreambox::decoderPositionFromSinks(GstElement* audioSink, GstElement* videoSink, GstClockTime maxPosition) const
{
    for (GstElement* sink : { audioSink, videoSink }) {
        if (!sink || !g_signal_lookup("get-decoder-time", G_OBJECT_TYPE(sink)))
            continue;
        // Before the first frame the driver reports garbage (hours, a different value every time: maxPosition, the duration
        // plus some slack, tells it from a real position); while paused the plain pipeline query is wrong and the decoder
        // time is frozen at the right value.
        if (GST_STATE(sink) < GST_STATE_PAUSED)
            continue;
        gint64 position = GST_CLOCK_TIME_NONE;
        g_signal_emit_by_name(sink, "get-decoder-time", &position);
        if (GST_CLOCK_TIME_IS_VALID(position) && static_cast<GstClockTime>(position) <= maxPosition)
            return static_cast<GstClockTime>(position);
    }
    return std::nullopt;
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
