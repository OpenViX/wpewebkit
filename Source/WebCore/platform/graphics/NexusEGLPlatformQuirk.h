/*
 * Copyright (C) 2026 xcentaurix
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

#pragma once

#if PLATFORM(WPE)

#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <wtf/Assertions.h>

namespace WebCore {

// Nexus-based Broadcom boxes (GigaBlue gbquad4kpro confirmed 2026-09-23; likely other Nexus-middleware boxes too, e.g. VU+
// Ultimo 4K - same "Memory interface not registered" signature, not yet retested with this exact fix) need two calls before
// any EGL call can succeed: NxClient_Join() (join the Nexus client/server IPC bus - necessary but, confirmed by testing, NOT
// sufficient alone) and NXPL_RegisterNexusDisplayPlatform() (libnxpl.so; its own doc comment says it "registers its own
// memory, h/w and display APIs" for the display - the literal, exact cause of "Memory interface not registered" at
// gmem_abstract.c:132 otherwise). Passing NULL for the display argument works: in multi-process client mode
// NEXUS_DISPLAYHANDLE is just void* (not a real handle from the single-process-only NEXUS_Display_Open), and this box's
// vendor driver resolves it against the already-joined default display.
//
// Pure runtime detection via dlopen/dlsym, no Nexus SDK headers and no link-time dependency on libnxclient.so/libnxpl.so, so
// this safely no-ops on every platform that doesn't have them (e.g. the DM900's libvc5dream stack, which has no
// Nexus/NxClient/NXPL at all). Root-caused and verified end-to-end (including that the live picture is unaffected) via
// tools/vuplus-probe in the hbbtv2 repo before being written here - see that tool's vu_egl_probe.c for the exact sequence
// this mirrors, and project memory (project_hbbtv2_wpe_orb_plan.md) for the full trace through Broadcom's own header dump
// that found NXPL_RegisterNexusDisplayPlatform in the first place.
//
// Called from every PlatformDisplay subclass this fork's WebProcess might actually instantiate for its EGL display -
// PlatformDisplayLibWPE (the real path when a WPE Platform API renderer backend is in use, e.g. this project's own wpe_fb
// embedder) and PlatformDisplayDefault (the generic eglGetDisplay(EGL_DEFAULT_DISPLAY) fallback) both need it: which one
// runs is decided by WebProcess::platformInitializeWebProcess() based on m_rendererBufferTransportMode, not something either
// class controls itself, so the quirk has to live at this shared, lower level rather than in just one of them.
inline void* nexusPlatformHandle;
inline void (*nexusPlatformUnregister)(void*);

inline void ensureNexusPlatformJoined()
{
    static bool attempted = false;
    if (attempted)
        return;
    attempted = true;

    // WPE_NO_NEXUS_JOIN=1: do not join/register (wpe_fb --egl-sink experiment; any EGL call in this process then aborts on a Nexus box)
    if (const char* skip = getenv("WPE_NO_NEXUS_JOIN"); skip && *skip == '1')
        return;

    // 2026-09-23 GigaBlue history: r8 (v2) got NxClient_Join() running and succeeding (logged "returned 0") but the
    // expected "NXPL_RegisterNexusDisplayPlatform() -> handle=..." line never appeared, with no diagnostics at the time to
    // say why. r9 (v3) added dlerror()-based logging to every failure branch below, which caught the real answer on the
    // very next test: "dlopen(\"libnxpl.so\") failed: /usr/lib/libnxpl.so: undefined symbol: BEGL_PlatformAboutToShutdown"
    // - see the comment further down, right before the libEGL preload loop, for the actual explanation and fix (r10).
    // VU+ (ARM, libgles-<machine> + libvupl images; confirmed by the "dlopen(libnxclient.so) failed / libnexus.so failed / libnxpl.so:
    // undefined symbol: NxClient_GetDefaultAllocSettings" log): there is no libnxclient.so and no libnexus.so at all - the NxClient_*/
    // NEXUS_* symbols libnxpl.so imports come from libdvb_base.so / libdvb_client.so (shipped next to libnxpl.so), and the join is
    // NEXUS_Platform_AuthenticatedJoin() when NxClient_Join() is absent. This is the same sequence the Kodi VU+ port uses (and
    // enigma2's own VU+ window provider goes through libvupl): load every optional carrier RTLD_LAZY|RTLD_GLOBAL, look the entry
    // points up in the global scope, and never let a missing one stop the others. Each handle stays open for the process lifetime.
    for (const char* name : { "libnexus.so", "libdvb_base.so", "libdvb_client.so" })
        dlopen(name, RTLD_LAZY | RTLD_GLOBAL);

    if (!dlopen("libnxclient.so", RTLD_LAZY | RTLD_GLOBAL))
        WTFLogAlways("dlopen(\"libnxclient.so\") failed: %s (using the symbols of the other Nexus libraries, if any)", dlerror());

    auto getDefaultJoinSettings = reinterpret_cast<void (*)(void*)>(dlsym(RTLD_DEFAULT, "NxClient_GetDefaultJoinSettings"));
    auto join = reinterpret_cast<int (*)(const void*)>(dlsym(RTLD_DEFAULT, "NxClient_Join"));
    auto authenticatedJoin = reinterpret_cast<int (*)(const void*)>(dlsym(RTLD_DEFAULT, "NEXUS_Platform_AuthenticatedJoin"));
    bool joined = false;
    if (getDefaultJoinSettings && join) {
        // The real NxClient_JoinSettings layout needs the proprietary Nexus SDK headers, not available at build time here.
        // Safe without them: GetDefaultJoinSettings() only WRITES into this buffer and Join() only reads back what was
        // just written, so any buffer at least as large as the real struct works (known Broadcom SDKs: well under 200
        // bytes; 1024 is generous headroom).
        unsigned char joinSettings[1024];
        memset(joinSettings, 0, sizeof(joinSettings));
        getDefaultJoinSettings(joinSettings);
        int rc = join(joinSettings);
        WTFLogAlways("NxClient_Join() returned %d (0 = NEXUS_SUCCESS)", rc);
        joined = !rc;
    }
    if (!joined && authenticatedJoin) {
        int rc = authenticatedJoin(nullptr);
        WTFLogAlways("NEXUS_Platform_AuthenticatedJoin() returned %d (0 = NEXUS_SUCCESS)", rc);
        joined = !rc;
    }
    if (!joined)
        WTFLogAlways("could not join Nexus: NxClient_Join=%p NEXUS_Platform_AuthenticatedJoin=%p", (void*)join, (void*)authenticatedJoin);

    // libnxpl.so imports libnexus.so/libdvb_*.so symbols too, which the preload loop above made visible (RTLD_GLOBAL).
    // 2026-09-23 GigaBlue, r9: confirmed the actual cause - "dlopen(\"libnxpl.so\") failed: /usr/lib/libnxpl.so: undefined
    // symbol: BEGL_PlatformAboutToShutdown". That symbol is exported by libv3ddriver.so (= libEGL.so/libGLESv2.so on this
    // box), which libnxpl.so also imports - but this build uses libepoxy (see USE(LIBEPOXY) in PlatformDisplayLibWPE.cpp),
    // which lazily dlopens the real vendor libEGL.so/libGLESv2.so on the FIRST actual EGL call, not at process startup.
    // ensureNexusPlatformJoined() runs BEFORE any EGL call, by design (that's the whole point), so libv3ddriver.so simply
    // isn't loaded yet at this point - explaining why NxClient_Join (needing only libnxclient.so's own, self-contained
    // symbols) succeeded while dlopen(RTLD_NOW) of libnxpl.so, needing BEGL_* symbols from a library nothing has loaded
    // yet, failed. This mirrors exactly what tools/vuplus-probe/vu_egl_probe.c does (and which DID work end-to-end,
    // verified 2026-09-23): its own very first step dlopens an EGL-providing library, before any Nexus join/register call
    // - so by the time it reaches NXPL_RegisterNexusDisplayPlatform, libv3ddriver.so is already loaded as a side effect.
    // Same fallback name list as that probe, for the same reason (some Nexus-based Broadcom images have no libEGL.so at
    // all, per the probe's own doc comment).
    static const char* const eglLibraryNames[] = { "libEGL.so.1", "libEGL.so", "libv3ddriver.so", nullptr };
    bool eglLibraryLoaded = false;
    for (int i = 0; eglLibraryNames[i] && !eglLibraryLoaded; ++i)
        eglLibraryLoaded = dlopen(eglLibraryNames[i], RTLD_NOW | RTLD_GLOBAL);
    if (!eglLibraryLoaded)
        WTFLogAlways("could not preload any EGL-providing library (tried libEGL.so.1, libEGL.so, libv3ddriver.so) before libnxpl.so: %s", dlerror());

    // RTLD_LAZY (was RTLD_NOW): on VU+ libnxpl.so also imports NxClient_* functions that only some images provide; lazy binding
    // resolves them on first call instead of failing the whole dlopen.
    if (void* nxpl = dlopen("libnxpl.so", RTLD_LAZY | RTLD_GLOBAL)) {
        auto registerDisplayPlatform = reinterpret_cast<void (*)(void**, void*)>(dlsym(nxpl, "NXPL_RegisterNexusDisplayPlatform"));
        nexusPlatformUnregister = reinterpret_cast<void (*)(void*)>(dlsym(nxpl, "NXPL_UnregisterNexusDisplayPlatform"));
        if (registerDisplayPlatform && nexusPlatformUnregister) {
            registerDisplayPlatform(&nexusPlatformHandle, nullptr);
            WTFLogAlways("NXPL_RegisterNexusDisplayPlatform() -> handle=%p", nexusPlatformHandle);
        } else {
            WTFLogAlways("libnxpl.so loaded but dlsym failed: Register=%p Unregister=%p",
                (void*)registerDisplayPlatform, (void*)nexusPlatformUnregister);
            nexusPlatformUnregister = nullptr;
        }
    } else
        WTFLogAlways("dlopen(\"libnxpl.so\") failed: %s", dlerror());
}

inline void unregisterNexusPlatformIfJoined()
{
    if (nexusPlatformUnregister && nexusPlatformHandle) {
        nexusPlatformUnregister(nexusPlatformHandle);
        nexusPlatformHandle = nullptr;
    }
}

} // namespace WebCore

#endif // PLATFORM(WPE)
