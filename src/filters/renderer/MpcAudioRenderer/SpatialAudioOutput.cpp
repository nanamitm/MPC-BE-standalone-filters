/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "stdafx.h"
#include "SpatialAudioOutput.h"
#include <cmath>

CSpatialAudioOutput::~CSpatialAudioOutput()
{
	if (m_stream) {
		if (m_updating) m_stream->EndUpdatingAudioObjects();
		m_stream->Stop();
	}
}

HRESULT CSpatialAudioOutput::Open(IMMDevice* device, HANDLE event, IAudioClock** clock)
{
	*clock = nullptr;
	HRESULT hr = device->Activate(__uuidof(ISpatialAudioClient), CLSCTX_ALL, nullptr, (void**)&m_client);
	if (FAILED(hr)) return hr;
	UINT32 objects = 0;
	hr = m_client->GetMaxDynamicObjectCount(&objects);
	if (FAILED(hr)) return hr;
	if (objects < MpcSpatial::Objects) return SPTLAUDCLNT_E_NO_MORE_OBJECTS;
	WAVEFORMATEX format = {WAVE_FORMAT_IEEE_FLOAT, 1, MpcSpatial::Rate, MpcSpatial::Rate * 4, 4, 32, 0};
	hr = m_client->IsAudioObjectFormatSupported(&format);
	if (hr != S_OK) return FAILED(hr) ? hr : AUDCLNT_E_UNSUPPORTED_FORMAT;
	UINT32 maxFrames = 0;
	hr = m_client->GetMaxFrameCount(&format, &maxFrames);
	if (FAILED(hr)) return hr;
	if (!maxFrames || maxFrames > MpcSpatial::Rate) return E_UNEXPECTED;
	m_interleaved.resize(size_t(maxFrames) * MpcSpatial::Channels);
	SpatialAudioObjectRenderStreamActivationParams params = {};
	params.ObjectFormat = &format;
	params.MinDynamicObjectCount = MpcSpatial::Objects;
	params.MaxDynamicObjectCount = MpcSpatial::Objects;
	params.Category = AudioCategory_Media;
	params.EventHandle = event;
	PROPVARIANT activation = {};
	activation.vt = VT_BLOB;
	activation.blob.cbSize = sizeof(params);
	activation.blob.pBlobData = reinterpret_cast<BYTE*>(&params);
	hr = m_client->ActivateSpatialAudioStream(&activation, __uuidof(ISpatialAudioObjectRenderStream), (void**)&m_stream);
	if (FAILED(hr)) return hr;
	// Objects must be activated within a Begin/End update pass (see Begin).
	return m_stream->GetService(__uuidof(IAudioClock), (void**)clock);
}

HRESULT CSpatialAudioOutput::Begin(UINT32& frames, BYTE*& data)
{
	data = nullptr;
	UINT32 available = 0;
	HRESULT hr = m_stream->BeginUpdatingAudioObjects(&available, &m_frames);
	if (FAILED(hr)) return hr;
	m_updating = true;
	frames = m_frames;
	if (size_t(frames) * MpcSpatial::Channels > m_interleaved.size()) {
		m_stream->EndUpdatingAudioObjects(); m_updating = false;
		return E_UNEXPECTED;
	}
	for (unsigned slot = 0, ch = 0; ch < MpcSpatial::Channels; ch++) {
		if (ch == 18) continue; // LFE2 shares LFE1's object
		if (!m_objects[slot]) {
			hr = m_stream->ActivateSpatialAudioObject(AudioObjectType_Dynamic, &m_objects[slot]);
			if (FAILED(hr)) break;
			const auto& p = MpcSpatial::Positions[ch];
			hr = m_objects[slot]->SetPosition(p.x, p.y, p.z);
			if (FAILED(hr)) break;
		}
		slot++;
	}
	if (FAILED(hr)) {
		m_stream->EndUpdatingAudioObjects(); m_updating = false;
		return hr;
	}
	data = reinterpret_cast<BYTE*>(m_interleaved.data());
	return S_OK;
}

HRESULT CSpatialAudioOutput::End(DWORD flags)
{
	if (!m_updating) return E_UNEXPECTED;
	HRESULT hr = S_OK;
	for (unsigned slot = 0, ch = 0; ch < MpcSpatial::Channels; ch++) {
		if (ch == 18) continue;
		BYTE* buffer = nullptr;
		UINT32 bytes = 0;
		hr = m_objects[slot++]->GetBuffer(&buffer, &bytes);
		if (FAILED(hr) || bytes < m_frames * sizeof(float)) {
			if (SUCCEEDED(hr)) hr = E_UNEXPECTED;
			break;
		}
		auto* dst = reinterpret_cast<float*>(buffer);
		for (UINT32 frame = 0; frame < m_frames; frame++) {
			const auto* src = &m_interleaved[size_t(frame) * MpcSpatial::Channels];
			float value = ch == 3 ? 0.5f * (src[3] + src[18]) : src[ch];
			dst[frame] = flags & AUDCLNT_BUFFERFLAGS_SILENT ? 0.f
				: (std::isfinite(value) ? std::clamp(value, -1.f, 1.f) : 0.f);
		}
	}
	HRESULT end = m_stream->EndUpdatingAudioObjects();
	m_updating = false;
	return FAILED(hr) ? hr : end;
}
