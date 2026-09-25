/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "WinSystemPS5GLContext.h"

#include "cores/VideoPlayer/DVDCodecs/DVDFactoryCodec.h"
#include "cores/VideoPlayer/VideoRenderers/LinuxRendererGL.h"
#include "cores/VideoPlayer/VideoRenderers/RenderFactory.h"
#include "utils/log.h"
#include "windowing/WindowSystemFactory.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>

#include <EGL/egl.h>
#include <EGL/eglext.h>

using namespace KODI::WINDOWING::PS5;

// No EGL platform extension: the SDK's legacy eglGetDisplay() is the path.
CWinSystemPS5GLContext::CWinSystemPS5GLContext() : LINUX::CWinSystemEGL(EGL_NONE, "")
{
}

void CWinSystemPS5GLContext::Register()
{
  CWindowSystemFactory::RegisterWindowSystem(CreateWinSystem, "ps5");
}

std::unique_ptr<CWinSystemBase> CWinSystemPS5GLContext::CreateWinSystem()
{
  return std::make_unique<CWinSystemPS5GLContext>();
}

bool CWinSystemPS5GLContext::InitWindowSystem()
{
  // Software-decoded video rendered through the generic GL YUV renderer.
  // Hardware decoding (VideoDec2) plugs in here later as a HW accel + renderer.
  VIDEOPLAYER::CRendererFactory::ClearRenderer();
  CDVDFactoryCodec::ClearHWAccels();
  CLinuxRendererGL::Register();

  if (!CWinSystemPS5::InitWindowSystem())
    return false;

  if (!m_eglContext.CreateDisplay(EGL_DEFAULT_DISPLAY))
    return false;

  if (!m_eglContext.InitializeDisplay(EGL_OPENGL_API))
  {
    m_eglContext.Destroy();
    return false;
  }

  if (!m_eglContext.ChooseConfig(EGL_OPENGL_BIT))
  {
    m_eglContext.Destroy();
    return false;
  }

  if (!CreateContext())
  {
    m_eglContext.Destroy();
    return false;
  }

  return true;
}

bool CWinSystemPS5GLContext::DestroyWindowSystem()
{
  CDVDFactoryCodec::ClearHWAccels();
  VIDEOPLAYER::CRendererFactory::ClearRenderer();
  m_eglContext.Destroy();
  return CWinSystemPS5::DestroyWindowSystem();
}

bool CWinSystemPS5GLContext::CreateContext()
{
  // Kodi's GL renderer needs 3.2 Core; Mesa hands back the highest compatible
  // version (4.6 on ps5-opengl 0.3.0).
  const EGLint glMajor = 3;
  const EGLint glMinor = 2;

  CEGLAttributesVec contextAttribs;
  contextAttribs.Add({{EGL_CONTEXT_MAJOR_VERSION_KHR, glMajor},
                      {EGL_CONTEXT_MINOR_VERSION_KHR, glMinor},
                      {EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR}});

  if (!m_eglContext.CreateContext(contextAttribs))
  {
    CLog::Log(LOGERROR, "CWinSystemPS5GLContext: EGL OpenGL {}.{} core context creation failed",
              glMajor, glMinor);
    return false;
  }
  return true;
}

void CWinSystemPS5GLContext::QueryOutputGeometry()
{
  EGLint width = 0;
  EGLint height = 0;
  if (eglQuerySurface(m_eglContext.GetEGLDisplay(), m_eglContext.GetEGLSurface(), EGL_WIDTH, &width) &&
      eglQuerySurface(m_eglContext.GetEGLDisplay(), m_eglContext.GetEGLSurface(), EGL_HEIGHT, &height) &&
      width > 0 && height > 0)
  {
    m_outputWidth = width;
    m_outputHeight = height;
  }
  // The SDK profiles are 1080p60, 1440p120 and 4K120; EGL cannot tell us the
  // refresh, so infer it from the height until the SDK exposes it.
  m_outputRefresh = (m_outputHeight > 1080) ? 120.0f : 60.0f;
}

bool CWinSystemPS5GLContext::CreateNewWindow(const std::string& name,
                                             bool fullScreen,
                                             RESOLUTION_INFO& res)
{
  OnLostDevice();

  if (!DestroyWindow())
    return false;

  // A null native window selects the console's fullscreen output.
  if (!m_eglContext.CreateSurface(static_cast<EGLNativeWindowType>(0)))
  {
    CLog::Log(LOGERROR, "CWinSystemPS5GLContext: failed to create the EGL window surface");
    return false;
  }

  if (!m_eglContext.BindContext())
  {
    m_eglContext.DestroySurface();
    return false;
  }

  QueryOutputGeometry();

  m_nWidth = m_outputWidth;
  m_nHeight = m_outputHeight;
  m_fRefreshRate = m_outputRefresh;
  m_bFullScreen = true;

  // Report what the output really is; the caller's RESOLUTION_INFO may carry
  // stale numbers from guisettings.xml.
  res.iWidth = m_nWidth;
  res.iHeight = m_nHeight;
  res.iScreenWidth = m_nWidth;
  res.iScreenHeight = m_nHeight;
  res.fRefreshRate = m_fRefreshRate;

  m_bWindowCreated = true;
  OnResetDevice();
  return true;
}

bool CWinSystemPS5GLContext::DestroyWindow()
{
  m_eglContext.DestroySurface();
  m_bWindowCreated = false;
  return true;
}

bool CWinSystemPS5GLContext::SetFullScreen(bool fullScreen,
                                           RESOLUTION_INFO& res,
                                           bool blankOtherDisplays)
{
  // There is exactly one mode. Recreating the surface is only needed when the
  // window does not exist yet; every other "mode change" is answered with the
  // fixed output geometry.
  if (!m_bWindowCreated && !CreateNewWindow("Kodi", true, res))
    return false;

  res.iWidth = m_nWidth;
  res.iHeight = m_nHeight;
  res.iScreenWidth = m_nWidth;
  res.iScreenHeight = m_nHeight;
  res.fRefreshRate = m_fRefreshRate;

  CRenderSystemGL::ResetRenderSystem(m_nWidth, m_nHeight);
  return true;
}

void CWinSystemPS5GLContext::SetVSyncImpl(bool enable)
{
  m_eglContext.SetVSync(enable);
}

void CWinSystemPS5GLContext::PresentRender(bool rendered, bool videoLayer)
{
  if (!m_bRenderCreated)
    return;

  if (rendered || videoLayer)
  {
    // eglSwapBuffers is our only vertical-sync source.
    const auto before = std::chrono::steady_clock::now();
    if (!m_eglContext.TrySwapBuffers())
    {
      CEGLUtils::Log(LOGERROR, "eglSwapBuffers failed");
      throw std::runtime_error("eglSwapBuffers failed");
    }
    AccountSwap(before, std::chrono::steady_clock::now());
  }
  else
  {
    // Nothing changed this frame: yield instead of spinning a core.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

// Frame pacing diagnostics: how often frames really reach the display and
// how long a swap blocks (it should wait for vertical sync). Logged every
// 5 seconds while frames are being presented.
void CWinSystemPS5GLContext::AccountSwap(std::chrono::steady_clock::time_point before,
                                         std::chrono::steady_clock::time_point after)
{
  using namespace std::chrono;
  const double swapMs = duration<double, std::milli>(after - before).count();
  if (m_swapCount == 0)
    m_swapWindowStart = before;
  else
    m_swapGapMaxMs = std::max(m_swapGapMaxMs, duration<double, std::milli>(before - m_lastSwapEnd).count());
  m_lastSwapEnd = after;
  ++m_swapCount;
  m_swapTotalMs += swapMs;
  m_swapMaxMs = std::max(m_swapMaxMs, swapMs);

  const double windowS = duration<double>(after - m_swapWindowStart).count();
  if (windowS < 5.0)
    return;
  CLog::Log(LOGINFO,
            "PS5 present: {:.1f} frames/s over {:.1f} s, swap blocks avg {:.2f} ms / max {:.2f} ms, "
            "longest gap between swaps {:.1f} ms",
            m_swapCount / windowS, windowS, m_swapTotalMs / m_swapCount, m_swapMaxMs,
            m_swapGapMaxMs);
  m_swapCount = 0;
  m_swapTotalMs = m_swapMaxMs = m_swapGapMaxMs = 0;
}
