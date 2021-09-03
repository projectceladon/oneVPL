/*############################################################################
  # Copyright (C) Intel Corporation
  #
  # SPDX-License-Identifier: MIT
  ############################################################################*/

#if defined(LIBVA_X11_SUPPORT)

    #include "vaapi_utils_x11.h"
    #include "sample_defs.h"

    #include <dlfcn.h>
    #if defined(X11_DRI3_SUPPORT)
        #include <fcntl.h>
    #endif

    #define VAAPI_X_DEFAULT_DISPLAY ":0.0"

X11LibVA::X11LibVA(void)
        : CLibVA(MFX_LIBVA_X11),
          m_display(0),
          m_configID(VA_INVALID_ID),
          m_contextID(VA_INVALID_ID) {
    char* currentDisplay = getenv("DISPLAY");

    m_display = (currentDisplay) ? m_x11lib.XOpenDisplay(currentDisplay)
                                 : m_x11lib.XOpenDisplay(VAAPI_X_DEFAULT_DISPLAY);

    if (!m_display) {
        msdk_printf(MSDK_STRING(
            "Failed to open X Display: try to check/set DISPLAY environment variable.\n"));
        throw std::bad_alloc();
    }

    m_va_dpy = m_vax11lib.vaGetDisplay(m_display);
    if (!m_va_dpy) {
        m_x11lib.XCloseDisplay(m_display);
        msdk_printf(MSDK_STRING("Failed to get VA Display\n"));
        throw std::bad_alloc();
    }

    int major_version = 0, minor_version = 0;
    VAStatus sts = m_libva.vaInitialize(m_va_dpy, &major_version, &minor_version);

    if (VA_STATUS_SUCCESS != sts) {
        m_x11lib.XCloseDisplay(m_display);
        msdk_printf(MSDK_STRING("Failed to initialize VAAPI: %d\n"), sts);
        throw std::bad_alloc();
    }

    #if !defined(X11_DRI3_SUPPORT)
    VAConfigAttrib cfgAttrib{};
    if (VA_STATUS_SUCCESS == sts) {
        cfgAttrib.type = VAConfigAttribRTFormat;
        sts            = m_libva.vaGetConfigAttributes(m_va_dpy,
                                            VAProfileNone,
                                            VAEntrypointVideoProc,
                                            &cfgAttrib,
                                            1);
    }
    if (VA_STATUS_SUCCESS == sts) {
        sts = m_libva.vaCreateConfig(m_va_dpy,
                                     VAProfileNone,
                                     VAEntrypointVideoProc,
                                     &cfgAttrib,
                                     1,
                                     &m_configID);
    }
    if (VA_STATUS_SUCCESS == sts) {
        sts =
            m_libva.vaCreateContext(m_va_dpy, m_configID, 0, 0, VA_PROGRESSIVE, 0, 0, &m_contextID);
    }
    if (VA_STATUS_SUCCESS != sts) {
        Close();
        msdk_printf(MSDK_STRING("Failed to initialize VP: %d\n"), sts);
        throw std::bad_alloc();
    }
    #endif // X11_DRI3_SUPPORT
}

X11LibVA::~X11LibVA(void) {
    Close();
}

void X11LibVA::Close() {
    VAStatus sts;

    if (m_contextID != VA_INVALID_ID) {
        sts = m_libva.vaDestroyContext(m_va_dpy, m_contextID);
        if (sts != VA_STATUS_SUCCESS)
            msdk_printf(MSDK_STRING("Failed to destroy VA context: %d\n"), sts);
    }
    if (m_configID != VA_INVALID_ID) {
        sts = m_libva.vaDestroyConfig(m_va_dpy, m_configID);
        if (sts != VA_STATUS_SUCCESS)
            msdk_printf(MSDK_STRING("Failed to destroy VA config: %d\n"), sts);
    }
    sts = m_libva.vaTerminate(m_va_dpy);
    if (sts != VA_STATUS_SUCCESS)
        msdk_printf(MSDK_STRING("Failed to close VAAPI library: %d\n"), sts);

    m_x11lib.XCloseDisplay(m_display);
}

#endif // #if defined(LIBVA_X11_SUPPORT)
