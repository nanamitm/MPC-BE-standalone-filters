// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <vector>
#include <cstdint>

// Minimal AAC-LC silence. Channel configuration 13 is the standard 22.2 layout.
// No external recording or encoder is needed for the decoder/graph regression.
inline std::vector<uint8_t> MakeSilentLatm(unsigned config = 2, unsigned frequencyIndex = 3)
{
	struct Bits {
		std::vector<uint8_t> data;
		unsigned position = 0;
		void put(unsigned value, unsigned count) {
			for (unsigned i = count; i; --i) {
				if (!(position % 8)) data.push_back(0);
				data.back() |= ((value >> (i-1)) & 1) << (7-position%8);
				++position;
			}
		}
	} raw, mux;
	const auto channel = [&] {
		raw.put(100,8); // global_gain
		raw.put(0,1); raw.put(0,2); raw.put(0,1); // long window
		raw.put(0,6); raw.put(0,1); // max_sfb, prediction
		raw.put(0,3); // pulse, TNS, gain control
	};
	const auto element = [&](unsigned type, unsigned tag) {
		raw.put(type,3); raw.put(tag,4);
		if (type == 1) raw.put(0,1); // CPE without common window
		channel();
		if (type == 1) channel();
	};
	if (config == 13) {
		const unsigned elements[][2] = {
			{0,0},{1,0},{1,1},{1,2},{1,3},{0,1},{3,0},{3,1},
			{0,2},{1,4},{1,5},{0,3},{1,6},{0,4},{0,5},{1,7}
		};
		for (const auto& e : elements) element(e[0],e[1]);
	} else if (config == 1) element(0,0);
	else element(1,0);
	raw.put(7,3); // ID_END (remaining bits are zero padding)
	mux.put(0,1); // useSameStreamMux
	mux.put(0,1); mux.put(1,1); mux.put(0,6); // audioMuxVersion, framing, subframes
	mux.put(0,4); mux.put(0,3); // program/layer
	mux.put(2,5); mux.put(frequencyIndex,4); mux.put(config,4); // AAC LC ASC
	mux.put(0,3); // GASpecificConfig
	mux.put(0,3); mux.put(255,8); mux.put(0,1); mux.put(0,1);
	mux.put(static_cast<unsigned>(raw.data.size()),8);
	for (auto byte : raw.data) mux.put(byte,8);
	std::vector<uint8_t> result = {0x56, static_cast<uint8_t>(0xe0 | (mux.data.size() >> 8)), static_cast<uint8_t>(mux.data.size())};
	result.insert(result.end(),mux.data.begin(),mux.data.end());
	return result;
}
