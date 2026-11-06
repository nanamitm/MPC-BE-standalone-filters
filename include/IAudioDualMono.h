// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Unknwn.h>

// Compatible with arib-splitter's ILAVAudioDualMono contract.
// 0: main left/sub right; 1: main on both outputs; 2: sub on both outputs.
// Only genuine AAC SCE+SCE dual mono is affected, never ordinary stereo.
enum AudioDualMonoMode : DWORD { DualMono_Both, DualMono_Main, DualMono_Sub };
interface __declspec(uuid("571CC80C-1DCF-41F2-94EF-546D071EC039"))
ILAVAudioDualMono : public IUnknown {
    STDMETHOD_(DWORD, GetDualMonoMode)() PURE;
    STDMETHOD(SetDualMonoMode)(DWORD mode) PURE;
};
