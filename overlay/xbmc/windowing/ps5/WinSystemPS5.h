/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "threads/CriticalSection.h"
#include "windowing/WinSystem.h"

#include <memory>
#include <string>
#include <vector>

class IDispResource;

namespace KODI::PLATFORM::PS5
{
class CPadInput;
}

namespace KODI::WINDOWING::PS5
{

/*!
 * \brief Window system for PlayStation 5 homebrew.
 *
 * The console has one output whose geometry is fixed by the ps5-opengl SDK
 * build profile (1080p60, 1440p120 or 4K120): no windowed mode, no cursor,
 * no screensaver, no runtime mode switching. Input is pushed straight into
 * the application port by the pad thread, so MessagePump() has nothing to do.
 */
class CWinSystemPS5 : public CWinSystemBase
{
public:
  CWinSystemPS5();
  ~CWinSystemPS5() override;

  const std::string GetName() override { return "ps5"; }

  bool InitWindowSystem() override;
  bool DestroyWindowSystem() override;

  bool ResizeWindow(int newWidth, int newHeight, int newLeft, int newTop) override;
  void UpdateResolutions() override;

  // Vertical sync for Kodi's reference clock (VideoSyncPS5).
  std::unique_ptr<CVideoSync> GetVideoSync(CVideoReferenceClock* clock) override;

  // The system reported the real output refresh rate (after the first
  // presented frame): adopt it for the desktop resolution.
  void ApplySystemRefreshRate(float hz);

  bool CanDoWindowed() override { return false; }
  bool SupportsScreenMove() override { return false; }
  bool HasCursor() override { return false; }
  bool MessagePump() override { return false; }

  void Register(IDispResource* resource) override;
  void Unregister(IDispResource* resource) override;

protected:
  void OnLostDevice();
  void OnResetDevice();

  // Output geometry; refined from the EGL surface once it exists.
  int m_outputWidth{1920};
  int m_outputHeight{1080};
  float m_outputRefresh{60.0f};

  CCriticalSection m_resourceSection;
  std::vector<IDispResource*> m_resources;

  std::unique_ptr<KODI::PLATFORM::PS5::CPadInput> m_padInput;
};

} // namespace KODI::WINDOWING::PS5
