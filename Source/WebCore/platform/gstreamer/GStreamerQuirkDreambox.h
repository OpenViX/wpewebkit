/*
 * Dreambox (Broadcom, gst-plugin-dvbmediasink) quirk: audio goes to dvbaudiosink, the hardware decoder path is
 * used for video through GStreamerHolePunchQuirkDreambox. Enable with WEBKIT_GST_QUIRKS=dreambox.
 */

#pragma once

#if USE(GSTREAMER)

#include "GStreamerQuirks.h"

namespace WebCore {

class GStreamerQuirkDreambox final : public GStreamerQuirk {
public:
    GStreamerQuirkDreambox() = default;
    const ASCIILiteral identifier() const final { return "Dreambox"_s; }
    bool isPlatformSupported() const final;

    GstElement* createAudioSink() final;
    bool seekCompletesWithoutAsyncDone() const final { return true; }
    GstSeekFlags adjustSeekFlags(GstSeekFlags) const final;
    std::optional<GstClockTime> decoderPositionFromSinks(GstElement*, GstElement*, GstClockTime maxPosition) const final;
    std::optional<bool> isHardwareAccelerated(GstElementFactory*) final;
    unsigned getAdditionalPlaybinFlags() const final { return getGstPlayFlag("native-video") | getGstPlayFlag("native-audio"); }
};

} // namespace WebCore

#endif // USE(GSTREAMER)
