/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <chrono>
#include <cstdlib>

#include "WinSystemPS5.h"
#include "rendering/gl/RenderSystemGL.h"
#include "utils/EGLUtils.h"
#include "windowing/linux/WinSystemEGL.h"

#include <memory>
#include <string>

namespace KODI::WINDOWING::PS5
{

/*!
 * \brief OpenGL window system backed by the ps5-opengl SDK.
 *
 * The SDK exposes a Mesa/Gallium OpenGL 4.6 Core implementation behind a
 * plain EGL 1.4-style interface: eglGetDisplay(EGL_DEFAULT_DISPLAY) and a
 * window surface created with a null native window select the console's
 * fullscreen output. Its size is fixed by the SDK build profile, so we query
 * the surface for the real geometry instead of trusting Kodi's settings.
 */
/*
 * CWinSystemEGL (windowing/linux) is a small mixin that owns the
 * CEGLContextUtils and exposes the EGL handles; RetroPlayer's EGL hardware
 * rendering context casts the window system to it.
 */
class CWinSystemPS5GLContext : public CWinSystemPS5,
                               public CRenderSystemGL,
                               public LINUX::CWinSystemEGL
{
public:
  CWinSystemPS5GLContext();
  ~CWinSystemPS5GLContext() override = default;

  static void Register();
  static std::unique_ptr<CWinSystemBase> CreateWinSystem();

  // CWinSystemBase
  CRenderSystemBase* GetRenderSystem() override { return this; }
  bool InitWindowSystem() override;
  bool DestroyWindowSystem() override;
  bool CreateNewWindow(const std::string& name, bool fullScreen, RESOLUTION_INFO& res) override;
  bool DestroyWindow() override;
  bool SetFullScreen(bool fullScreen, RESOLUTION_INFO& res, bool blankOtherDisplays) override;
  // The PS5 GL driver reports buffer ages that do not match its swap chain
  // (stale GUI content showed through). Age 0 makes Kodi redraw the whole
  // screen every frame, which is cheap on this GPU.
  void SetDirtyRegions(const CDirtyRegionList& dirtyRegions) override {}
  int GetBufferAge() override { return 0; }

  // CRenderSystemGL
  void PresentRender(bool rendered, bool videoLayer) override;

protected:
  void SetVSyncImpl(bool enable) override;
  void PresentRenderImpl(bool rendered) override {}

private:
  bool m_videoOutLogged = false;
  std::chrono::steady_clock::time_point m_nextVrrPresent{}; // VRR presentation cadence
  // kodi-debug: presented-frame statistics
  const bool m_countPresents = std::getenv("KODI_PS5_DEBUG") != nullptr;
  unsigned m_presents = 0;
  unsigned m_presentsWithGui = 0;
  std::chrono::steady_clock::time_point m_presentWindow{};

  bool CreateContext();
  void QueryOutputGeometry();

};

} // namespace KODI::WINDOWING::PS5
