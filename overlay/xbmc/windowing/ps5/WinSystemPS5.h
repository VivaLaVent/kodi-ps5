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

#if __has_include(<ps5_opengl_display.h>)
#include <ps5_opengl_display.h> // PS5_OPENGL_NATIVE_{WIDTH,HEIGHT,FPS}: GL SDK build profile
#endif

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

  // After the first frame: offer the 120 Hz output mode to Kodi if the
  // system accepts it (Kodi's "Adjust display refresh rate" then uses it).
  void DetectOutputModes();

  // Switch between the system rate and 120 Hz for a requested refresh rate.
  // Returns the refresh rate actually in effect.
  float SwitchOutputRate(float requestedHz);

  // Back to the system's own mode (on exit).
  void RestoreOutputMode();

  // The output's real refresh rate (Kodi's mode list may say otherwise, e.g.
  // when a requested mode could not be set up exactly).
  float OutputRefreshRate() const { return m_fRefreshRate; }

  // The vblank clock found vblanks arriving at a rate other than the output's
  // (e.g. the PS5's own VRR setting turned the 120 Hz mode into VRR): don't
  // use it again until the next output mode change.
  void SetVblankClockUnreliable() { m_vblankClockUnreliable = true; }
  bool IsVblankClockUnreliable() const { return m_vblankClockUnreliable; }

  bool CanDoWindowed() override { return false; }
  bool SupportsScreenMove() override { return false; }
  bool HasCursor() override { return false; }
  bool MessagePump() override { return false; }

  void Register(IDispResource* resource) override;
  void Unregister(IDispResource* resource) override;

protected:
  void OnLostDevice();
  void OnResetDevice();

  // Output geometry. Kodi registers its resolutions before the window (and
  // the EGL surface) exists, so start from the GL SDK's build profile - the
  // size its surface will have - and refine from the surface afterwards.
#if defined(PS5_OPENGL_NATIVE_WIDTH) && defined(PS5_OPENGL_NATIVE_HEIGHT)
  int m_outputWidth{PS5_OPENGL_NATIVE_WIDTH};
  int m_outputHeight{PS5_OPENGL_NATIVE_HEIGHT};
#else
  int m_outputWidth{1920};
  int m_outputHeight{1080};
#endif
#if defined(PS5_OPENGL_NATIVE_FPS)
  float m_outputRefresh{static_cast<float>(PS5_OPENGL_NATIVE_FPS)};
#else
  float m_outputRefresh{60.0f};
#endif

  float m_systemRefresh{0.0f};    // rate of the system's default mode (0: not known yet)
  bool m_highRefreshAvailable{false};
  // explicit-rate modes the system accepted in a kodi-probe-modes trial
  // (saved in special://home/ps5-output-modes.txt)
  bool m_rate23976Available{false};
  bool m_rate50Available{false};
  uint64_t m_rate23976Field{0}; // refresh-field value the trial verified
  uint64_t m_rate50Field{0};

  enum class ActiveOutput
  {
    System,   // the system's own mode (59.94 Hz): GUI and stopped state
    High120,  // the 120 Hz preset (119.88 Hz)
    Rate23976, // explicit 23.976 Hz
    Rate50,   // explicit 50 Hz
  };
  ActiveOutput m_activeOutput{ActiveOutput::System};

  void RunModeTrial();
  void LoadModeResults();
  bool m_vblankClockUnreliable{false};

  CCriticalSection m_resourceSection;
  std::vector<IDispResource*> m_resources;

  std::unique_ptr<KODI::PLATFORM::PS5::CPadInput> m_padInput;
};

} // namespace KODI::WINDOWING::PS5
