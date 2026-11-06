/*
 * Copyright (C) 2023 Igalia S.L.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "PlatformDisplaySurfaceless.h"

#include "GLContext.h"
#include <epoxy/egl.h>

#if PLATFORM(WPE)
#include "NexusEGLPlatformQuirk.h"
#endif

namespace WebCore {

std::unique_ptr<PlatformDisplaySurfaceless> PlatformDisplaySurfaceless::create()
{
#if PLATFORM(WPE)
    // This is the FIRST EGL call of a WebProcess that gets its display through WebProcess::initializePlatformDisplayIfNeeded()
    // (m_rendererBufferTransportMode not empty), and eglQueryString(EGL_NO_DISPLAY, ...) below is rejected with EGL_BAD_DISPLAY. On a
    // Nexus based Broadcom box (VU+ Ultimo 4K confirmed) the vendor libEGL keeps its per-thread state under a pthread key that only the
    // Nexus join/register creates: before it, the key is still 0 and pthread_getspecific(0) returns mimalloc's slot (theap_main), so the
    // driver stores 0x3008 into the first word of mimalloc's main heap. The next big allocation (JSC::VM::create) then dies in
    // mi_arenas_page_regular_alloc. Join and register first, as PlatformDisplayLibWPE/Default already do.
    ensureNexusPlatformJoined();
#endif

    const char* extensions = eglQueryString(nullptr, EGL_EXTENSIONS);
    if (!GLContext::isExtensionSupported(extensions, "EGL_MESA_platform_surfaceless"))
        return nullptr;

    RefPtr<GLDisplay> glDisplay;
    if (GLContext::isExtensionSupported(extensions, "EGL_EXT_platform_base"))
        glDisplay = GLDisplay::create(eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr));
    else if (GLContext::isExtensionSupported(extensions, "EGL_KHR_platform_base"))
        glDisplay = GLDisplay::create(eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr));

    if (!glDisplay) {
        WTFLogAlways("Could not create surfaceless EGL display: %s. Aborting...", GLContext::lastErrorString());
        CRASH();
    }

    return std::unique_ptr<PlatformDisplaySurfaceless>(new PlatformDisplaySurfaceless(glDisplay.releaseNonNull()));
}

PlatformDisplaySurfaceless::PlatformDisplaySurfaceless(Ref<GLDisplay>&& glDisplay)
    : PlatformDisplay(WTF::move(glDisplay))
{
#if ENABLE(WEBGL)
    m_anglePlatform = EGL_PLATFORM_SURFACELESS_MESA;
    m_angleNativeDisplay = EGL_DEFAULT_DISPLAY;
#endif
}

PlatformDisplaySurfaceless::~PlatformDisplaySurfaceless()
{
#if PLATFORM(WPE)
    unregisterNexusPlatformIfJoined();
#endif
}

} // namespace WebCore
