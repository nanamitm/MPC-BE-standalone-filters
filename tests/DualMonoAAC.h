// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <vector>
#include <cstdint>

// AAC-LC PNS produces nonzero, independent main/sub signals without recordings.
// Two SCEs plus a PCE describe dual mono; a normal CPE is the stereo control.
inline std::vector<uint8_t> MakeDualMonoAAC(bool stereo = false, bool latm = false, bool noPCE = false) {
    struct Bits {
        std::vector<uint8_t> data;
        unsigned pos=0;
        void put(unsigned value,unsigned count) {
            for(unsigned i=count;i;--i) {
                if(!(pos%8))data.push_back(0);
                data.back()|=((value>>(i-1))&1)<<(7-pos%8);++pos;
            }
        }
        void align() { while(pos%8)put(0,1); }
    } raw,adts;
    if(!stereo && !noPCE) {
        raw.put(5,3);raw.put(0,4); // PCE, tag
        raw.put(1,2);raw.put(3,4); // LC, 48kHz
        raw.put(2,4);raw.put(0,4);raw.put(0,4); // front/side/back
        raw.put(0,2);raw.put(0,3);raw.put(0,4);
        raw.put(0,3); // no mixdown fields
        raw.put(0,1);raw.put(0,4);raw.put(0,1);raw.put(1,4); // SCE0, SCE1
        raw.align();raw.put(0,8); // no PCE comment
    }
    const auto channel=[&](unsigned gain) {
        raw.put(gain,8);raw.put(0,1);raw.put(0,2);raw.put(0,1);
        raw.put(1,6);raw.put(0,1); // max_sfb=1, no prediction
        raw.put(13,4);raw.put(1,5); // one noise band
        raw.put(256,9);raw.put(0,3); // noise gain; no pulse/TNS/gain
    };
    if(stereo) {
        raw.put(1,3);raw.put(0,4);raw.put(0,1); // CPE, no common window
        channel(150);channel(170);
    } else {
        raw.put(0,3);raw.put(0,4);channel(150);
        raw.put(0,3);raw.put(1,4);channel(170);
    }
    raw.put(7,3);raw.align();
    if(latm) {
        // Start with stereo ASC; an in-band PCE defines the SCE+SCE mapping.
        Bits mux;
        mux.put(0,1);mux.put(0,1);mux.put(1,1);mux.put(0,6);
        mux.put(0,4);mux.put(0,3);mux.put(2,5);mux.put(3,4);mux.put(2,4);mux.put(0,3);
        mux.put(0,3);mux.put(255,8);mux.put(0,1);mux.put(0,1);
        mux.put(unsigned(raw.data.size()),8);
        for(auto byte:raw.data)mux.put(byte,8);
        std::vector<uint8_t> result={0x56,uint8_t(0xe0|(mux.data.size()>>8)),uint8_t(mux.data.size())};
        result.insert(result.end(),mux.data.begin(),mux.data.end());return result;
    }
    const unsigned size=unsigned(raw.data.size())+7;
    adts.put(0xfff,12);adts.put(0,1);adts.put(0,2);adts.put(1,1);
    adts.put(1,2);adts.put(3,4);adts.put(0,1);adts.put(stereo?2:0,3);
    adts.put(0,4);adts.put(size,13);adts.put(0x7ff,11);adts.put(0,2);
    adts.data.insert(adts.data.end(),raw.data.begin(),raw.data.end());
    return adts.data;
}
