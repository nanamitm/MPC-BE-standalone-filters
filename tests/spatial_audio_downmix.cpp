// SPDX-License-Identifier: GPL-3.0-or-later
#include "filters/renderer/MpcAudioRenderer/stdafx.h"
#include <memory>
#include "AudioTools/Mixer.h"
#include <dxgi1_2.h>
#include <initguid.h>
#include <d3d12.h>
#include <d3d12video.h>

#include "AudioTools/SpatialPCM.h"
extern "C" {
#include "ExtLib/ffmpeg/libavutil/channel_layout.h"
}
#include <cstdio>
#include <cmath>
#define REGISTER_FILTER
#include "filters/ffmpeg_link_fix.h"
static int CheckCanonicalConverter() {
 MpcSpatial::PCMConverter converter;
 AVFrame* frame=av_frame_alloc();
 if(!frame)return 1;
 const AVChannelLayout layout=AV_CHANNEL_LAYOUT_22POINT2;
 if(av_channel_layout_custom_init(&frame->ch_layout,24)<0)return 1;
 for(int ch=0;ch<24;ch++)frame->ch_layout.u.map[ch].id=av_channel_layout_channel_from_index(&layout,23-ch);
 frame->format=AV_SAMPLE_FMT_FLTP;frame->sample_rate=48000;frame->nb_samples=256;
 if(av_frame_get_buffer(frame,0)<0)return 1;
 for(int ch=0;ch<24;ch++)std::fill_n((float*)frame->extended_data[ch],256,float(24-ch)/32);
 std::vector<uint8_t> output;size_t bytes=0;
 if(!converter.Convert(frame,output,bytes)||bytes!=256*24*sizeof(float))return 1;
 const auto* pcm=reinterpret_cast<const float*>(output.data());
 for(int n=0;n<256;n++)for(int ch=0;ch<24;ch++)if(fabs(pcm[n*24+ch]-float(ch+1)/32)>0.000001)return 1;
 frame->ch_layout.u.map[0].id=AV_CHAN_UNKNOWN;
 if(converter.Convert(frame,output,bytes)||bytes)return 1;
 frame->ch_layout.u.map[0].id=av_channel_layout_channel_from_index(&layout,23);
 frame->sample_rate=44100;
 if(!converter.Convert(frame,output,bytes)||bytes<=256*24*sizeof(float))return 1;
 size_t tail=0;
 if(!converter.Drain(output,tail)||!tail)return 1;
 const size_t total=(bytes+tail)/(24*sizeof(float));
 if(total!=279)return 1; // ceil(256 * 48000 / 44100)
 if(!converter.Drain(output,tail)||tail)return 1;
 converter.Reset();av_frame_free(&frame);
 printf("PASS canonical 24ch ordering, unknown-speaker rejection and 44.1kHz resampling\n");
 return 0;
}
int main(){
 if(CheckCanonicalConverter()){printf("FAIL canonical converter\n");return 1;}
 for(auto layout : {AV_CH_LAYOUT_STEREO, AV_CH_LAYOUT_5POINT1, AV_CH_LAYOUT_7POINT1}){
  CMixer mixer;mixer.SetOptions(1,1,true,false);
  mixer.UpdateInput(SAMPLE_FMT_FLT,AV_CH_LAYOUT_22POINT2,48000);
  mixer.UpdateOutput(SAMPLE_FMT_FLT,(uint32_t)layout,48000);
  const int channels=layout==AV_CH_LAYOUT_STEREO?2:layout==AV_CH_LAYOUT_5POINT1?6:8;
  for(int ch=0;ch<24;ch++){
   float input[24]={},output[8]={}; input[ch]=1;
   if(mixer.Mixing((BYTE*)output,1,(const BYTE*)input,1)!=1){printf("FAIL mixer %d/%d\n",channels,ch);return 1;}
   double energy=0;for(int o=0;o<channels;o++){
    if(!std::isfinite(output[o])||fabs(output[o])>1.0001){printf("FAIL clipping %d/%d\n",channels,ch);return 1;}
    energy+=output[o]*output[o];
   }
   if(energy==0){printf("FAIL lost channel %d -> %d\n",ch,channels);return 1;}
  }
  float input[24],output[8]={};for(auto& v:input)v=1;
  if(mixer.Mixing((BYTE*)output,1,(const BYTE*)input,1)!=1)return 1;
  for(int o=0;o<channels;o++)if(!std::isfinite(output[o])||fabs(output[o])>1.0001){printf("FAIL summed clipping %d\n",channels);return 1;}
  printf("PASS all 24 individual channel impulses and normalized sum -> %d channels\n",channels);
 }
 return 0;
}
