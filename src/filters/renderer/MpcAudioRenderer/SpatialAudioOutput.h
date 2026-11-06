/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <SpatialAudioClient.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <atlbase.h>
#include <array>
#include <vector>
#include "SpatialAudioFormat.h"

// Used under the renderer lock. No second queue, thread, or synthetic clock.
class CSpatialAudioOutput final {
	CComPtr<ISpatialAudioClient> m_client;
	CComPtr<ISpatialAudioObjectRenderStream> m_stream;
	std::array<CComPtr<ISpatialAudioObject>, MpcSpatial::Objects> m_objects;
	std::vector<float> m_interleaved;
	UINT32 m_frames = 0;
	bool m_updating = false;
public:
	~CSpatialAudioOutput();
	HRESULT Open(IMMDevice* device, HANDLE event, IAudioClock** clock);
	HRESULT Start() { return m_stream->Start(); }
	HRESULT Stop() { return m_stream->Stop(); }
	HRESULT Begin(UINT32& frames, BYTE*& data);
	HRESULT End(DWORD flags);
};
