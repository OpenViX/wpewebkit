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

// playbin3 (always used for MSE) builds its stream chain with uridecodebin3/decodebin3, which keeps decoding until the stream is raw:
// avdec_h264/avdec_aac get plugged and their raw output is refused by dvbvideosink/dvbaudiosink (not-negotiated, "Failed to push buffer").
// The native-video/native-audio play flags do not change that for playbin3, so tell the decodebin to stop at the compressed (parsed)
// formats the dvb sinks take, as the legacy playbin does.
void GStreamerQuirkDreambox::configureElement(GstElement* element, const OptionSet<ElementRuntimeCharacteristics>&)
{
    auto name = GMallocString::unsafeAdoptFromUTF8(gst_element_get_name(element));
    if (!startsWith(name.span(), "uridecodebin3"_s) && !startsWith(name.span(), "decodebin3"_s))
        return;
    if (!g_object_class_find_property(G_OBJECT_GET_CLASS(element), "caps"))
        return;

    GstCaps* current = nullptr;
    g_object_get(element, "caps", &current, nullptr);
    GRefPtr<GstCaps> caps = adoptGRef(current ? gst_caps_make_writable(current) : gst_caps_new_empty());
    GRefPtr<GstCaps> compressed = adoptGRef(gst_caps_from_string("video/x-h264, parsed=(boolean)true; video/x-h265, parsed=(boolean)true; video/mpeg, mpegversion=(int){ 1, 2, 4 }, systemstream=(boolean)false; video/x-vp8; video/x-vp9; audio/mpeg, mpegversion=(int){ 2, 4 }, framed=(boolean)true; audio/x-ac3; audio/x-eac3; audio/x-dts"));
    gst_caps_append(caps.get(), gst_caps_copy(compressed.get()));
    g_object_set(element, "caps", caps.get(), nullptr);
    GST_INFO("Dreambox: %s stops decoding at compressed formats, caps %" GST_PTR_FORMAT, GST_ELEMENT_NAME(element), caps.get());
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
