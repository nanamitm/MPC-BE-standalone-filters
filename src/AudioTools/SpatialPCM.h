// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <limits>
#include <vector>
extern "C" {
#include "ExtLib/ffmpeg/libavutil/frame.h"
#include "ExtLib/ffmpeg/libswresample/swresample.h"
}

namespace MpcSpatial {
// Preserve speaker identities, including custom input order, before exposing
// the version 1 transport. Ordinary DWORD speaker masks cannot describe 22.2.
class PCMConverter {
	SwrContext* m_context = nullptr;
	AVChannelLayout m_layout{};
	int m_rate = 0, m_format = -1, m_outputRate = 0;
public:
	PCMConverter() = default;
	PCMConverter(const PCMConverter&) = delete;
	PCMConverter& operator=(const PCMConverter&) = delete;
	~PCMConverter() { Reset(); }
	void Reset() {
		swr_free(&m_context);
		av_channel_layout_uninit(&m_layout);
	}
	// Drain resampling delay at end of stream without inventing more input.
	bool Drain(std::vector<uint8_t>& output, size_t& bytes) {
		bytes = 0;
		if (!m_context) return true;
		const int capacity = swr_get_out_samples(m_context, 0);
		if (capacity <= 0) return capacity == 0;
		output.resize(size_t(capacity) * 24 * sizeof(float));
		uint8_t* data = output.data();
		const int samples = swr_convert(m_context, &data, capacity, nullptr, 0);
		if (samples < 0) return false;
		bytes = size_t(samples) * 24 * sizeof(float);
		return true;
	}
	bool Convert(const AVFrame* frame, std::vector<uint8_t>& output, size_t& bytes, int outputRate = 48000) {
		bytes = 0;
		if (!frame || frame->ch_layout.nb_channels != 24 || frame->sample_rate <= 0 || frame->nb_samples <= 0 || outputRate <= 0) return false;
		const AVChannelLayout canonical = AV_CHANNEL_LAYOUT_22POINT2;
		for (int ch = 0; ch < canonical.nb_channels; ++ch) {
			const auto id = av_channel_layout_channel_from_index(&canonical,ch);
			if (av_channel_layout_index_from_channel(&frame->ch_layout,id) < 0) return false;
		}
		if (!m_context || m_rate != frame->sample_rate || m_outputRate != outputRate
			|| m_format != frame->format || av_channel_layout_compare(&m_layout,&frame->ch_layout)) {
			Reset();
			if (swr_alloc_set_opts2(&m_context,&canonical,AV_SAMPLE_FMT_FLT,outputRate,
				&frame->ch_layout,static_cast<AVSampleFormat>(frame->format),frame->sample_rate,0,nullptr) < 0
				|| !m_context || swr_init(m_context) < 0
				|| av_channel_layout_copy(&m_layout,&frame->ch_layout) < 0) { Reset(); return false; }
			m_rate=frame->sample_rate; m_format=frame->format; m_outputRate=outputRate;
		}
		const int capacity=swr_get_out_samples(m_context,frame->nb_samples);
		if (capacity <= 0 || size_t(capacity) > (std::numeric_limits<size_t>::max)()/(24*sizeof(float))) return false;
		const size_t required=size_t(capacity)*24*sizeof(float);
		if (output.size()<required) output.resize(required);
		uint8_t* data=output.data();
		const int samples=swr_convert(m_context,&data,capacity,const_cast<const uint8_t**>(frame->extended_data),frame->nb_samples);
		if (samples < 0) return false;
		bytes=size_t(samples)*24*sizeof(float);
		return true;
	}
};
}
