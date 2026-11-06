/*
 * Dreambox hole-punch quirk: the video element's area is punched transparent in the page and the decoded video is
 * shown by the hardware video plane (dvbvideosink). The plane is positioned through /proc/stb/vmpeg/0/dst_*, which,
 * like in enigma2's eVideoWidget, take coordinates in a 720x576 space that spans the whole output.
 *
 * The page does not know where it sits on the screen, so the embedder tells it:
 *   WPE_DREAM_VIDEO_ORIGIN=x,y     screen position of the web view's top-left corner (default 0,0)
 *   WPE_DREAM_VIDEO_SCREEN=WxH     size of the screen/OSD the view is placed on (default 1920x1080)
 * Enable with WEBKIT_GST_HOLE_PUNCH_QUIRK=dreambox (plus WEBKIT_GST_QUIRKS=dreambox for the audio sink).
 */

#pragma once

#if USE(GSTREAMER)

#include "GStreamerQuirks.h"

namespace WebCore {

class GStreamerHolePunchQuirkDreambox final : public GStreamerHolePunchQuirk {
public:
    const ASCIILiteral identifier() const final { return "DreamboxHolePunch"_s; }

    GstElement* createHolePunchVideoSink(bool, const MediaPlayer*) final;
    bool setHolePunchVideoRectangle(GstElement*, const IntRect&) final;
    bool requiresClockSynchronization() const final { return false; }
};

} // namespace WebCore

#endif // USE(GSTREAMER)
