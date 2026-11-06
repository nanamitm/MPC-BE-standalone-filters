/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <cstddef>
#include <unknwn.h>
#include <guiddef.h>
#include <mmreg.h>

// Version 1 transport contract, independent of FFmpeg headers.
// Interleaved 48 kHz, 24 channels, WAVEFORMATEXTENSIBLE, mask = 0.
// SubFormat is PCM (16 bit) or IEEE_FLOAT (32 bit), with valid bits = bits.
// Channels, in this exact order (FFmpeg native AV_CH_LAYOUT_22POINT2):
// FL FR FC LFE BL BR FLC FRC BC SL SR TC TFL TFC TFR TBL TBC TBR
// LFE2 TSL TSR BFC BFL BFR.
// Unlabelled 24-channel PCM is deliberately not interpreted as this format.
inline constexpr GUID MEDIASUBTYPE_MPC_22POINT2 =
{0x3740b29c, 0x02a5, 0x4f75, {0xa8, 0xc9, 0x90, 0x8d, 0x9b, 0xac, 0xb8, 0x57}};

enum MPC_SPATIAL_MODE : int { MPC_SPATIAL_UNAVAILABLE = -1, MPC_SPATIAL_AUTO = 0, MPC_SPATIAL_DISABLED = 1 };

// Separate IID: preserve the existing IMpcAudioRendererFilter vtable.
interface __declspec(uuid("540EA4F8-4198-4A1D-B96D-47F61EFD811A"))
IMpcAudioRendererSpatial : public IUnknown {
	STDMETHOD(SetSpatialMode(INT mode)) PURE;
	// A forwarding filter returns UNAVAILABLE until a compatible destination is connected.
	STDMETHOD_(INT, GetSpatialMode()) PURE;
	// S_OK = spatial active, S_FALSE = disabled/not a spatial input,
	// failing HRESULT = last spatial failure; WASAPI fallback may still play.
	STDMETHOD(GetSpatialStatus()) PURE;
};

namespace MpcSpatial {
	inline constexpr unsigned Channels = 24;
	inline constexpr unsigned Rate = 48000;
	inline constexpr unsigned Objects = 23;
	struct Position { float x, y, z; }; // Windows: right/up/back, in metres
	inline constexpr Position Positions[Channels] = {
		{-0.5f,0,-0.866f}, {0.5f,0,-0.866f}, {0,0,-1}, {0,0,0},
		{-0.707f,0,0.707f}, {0.707f,0,0.707f}, {-0.259f,0,-0.966f}, {0.259f,0,-0.966f},
		{0,0,1}, {-1,0,0}, {1,0,0}, {0,1,0},
		{-0.5f,0.707f,-0.5f}, {0,0.707f,-0.707f}, {0.5f,0.707f,-0.5f},
		{-0.5f,0.707f,0.5f}, {0,0.707f,0.707f}, {0.5f,0.707f,0.5f},
		{0,0,0}, {-0.707f,0.707f,0}, {0.707f,0.707f,0},
		{0,-0.5f,-0.866f}, {-0.433f,-0.5f,-0.75f}, {0.433f,-0.5f,-0.75f}
	};

	inline bool ValidFormat(const WAVEFORMATEX* w, size_t bytes) {
		if (!w || bytes < sizeof(WAVEFORMATEXTENSIBLE)
			|| w->wFormatTag != WAVE_FORMAT_EXTENSIBLE || w->cbSize != 22
			|| w->nChannels != Channels || w->nSamplesPerSec != Rate) return false;
		const auto* e = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(w);
		const GUID pcm = {1,0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
		const GUID flt = {3,0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
		return e->dwChannelMask == 0 && e->Samples.wValidBitsPerSample == w->wBitsPerSample
			&& ((e->SubFormat == pcm && w->wBitsPerSample == 16)
				|| (e->SubFormat == flt && w->wBitsPerSample == 32))
			&& w->nBlockAlign == Channels * w->wBitsPerSample / 8
			&& w->nAvgBytesPerSec == Rate * w->nBlockAlign;
	}
}
