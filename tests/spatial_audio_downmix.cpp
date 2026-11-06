// SPDX-License-Identifier: GPL-3.0-or-later
#include "filters/renderer/MpcAudioRenderer/stdafx.h"
#include <memory>
#include "AudioTools/Mixer.h"
extern "C" {
#include "ExtLib/ffmpeg/libavutil/channel_layout.h"
}
#include <cstdio>
#include <cmath>
#define REGISTER_FILTER
#include "filters/ffmpeg_link_fix.h"
int main(){
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
