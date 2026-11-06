// SPDX-License-Identifier: GPL-3.0-or-later
// Load the built filter without registering it. All submitted samples are silent.
#include <windows.h>
#include <dshow.h>
#include <atlbase.h>
#include <atlstr.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "filters/renderer/MpcAudioRenderer/SpatialAudioFormat.h"
#include "filters/renderer/MpcAudioRenderer/IMpcAudioRenderer.h"
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"strmiids.lib")
#pragma comment(lib,"uuid.lib")
#define CHECK(x) do { HRESULT result=(x); if(FAILED(result)){ printf("FAIL line %d: %s = %08lx\n",__LINE__,#x,result); return 1; } }while(0)
#define REQUIRE(x) do { if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);return 1;} }while(0)
class OutputPin : public IPin {
public:
 STDMETHODIMP QueryInterface(REFIID id,void** p) override { *p=nullptr; if(id==IID_IUnknown||id==IID_IPin){*p=this; return S_OK;}return E_NOINTERFACE; }
 STDMETHODIMP_(ULONG) AddRef() override{return 2;}
 STDMETHODIMP_(ULONG) Release() override{return 1;}
 STDMETHODIMP Connect(IPin*,const AM_MEDIA_TYPE*) override{return E_NOTIMPL;}
 STDMETHODIMP ReceiveConnection(IPin*,const AM_MEDIA_TYPE*) override{return E_NOTIMPL;}
 STDMETHODIMP Disconnect() override{return S_OK;}
 STDMETHODIMP ConnectedTo(IPin**) override{return VFW_E_NOT_CONNECTED;}
 STDMETHODIMP ConnectionMediaType(AM_MEDIA_TYPE*) override{return E_NOTIMPL;}
 STDMETHODIMP QueryPinInfo(PIN_INFO* p) override{*p={}; p->dir=PINDIR_OUTPUT;return S_OK;}
 STDMETHODIMP QueryDirection(PIN_DIRECTION* p) override{*p=PINDIR_OUTPUT;return S_OK;}
 STDMETHODIMP QueryId(LPWSTR*) override{return E_NOTIMPL;}
 STDMETHODIMP QueryAccept(const AM_MEDIA_TYPE*) override{return S_OK;}
 STDMETHODIMP EnumMediaTypes(IEnumMediaTypes**) override{return E_NOTIMPL;}
 STDMETHODIMP QueryInternalConnections(IPin**,ULONG*) override{return E_NOTIMPL;}
 STDMETHODIMP EndOfStream() override{return S_OK;}
 STDMETHODIMP BeginFlush() override{return S_OK;}
 STDMETHODIMP EndFlush() override{return S_OK;}
 STDMETHODIMP NewSegment(REFERENCE_TIME,REFERENCE_TIME,double) override{return S_OK;}
};
int wmain(int argc,wchar_t** argv){
 REQUIRE(argc==2);
 CHECK(CoInitializeEx(nullptr,COINIT_MULTITHREADED));
 HMODULE module=LoadLibraryW(argv[1]); REQUIRE(module);
 auto factoryFn=reinterpret_cast<HRESULT(STDAPICALLTYPE*)(REFCLSID,REFIID,void**)>(GetProcAddress(module,"DllGetClassObject")); REQUIRE(factoryFn);
 const CLSID clsid={0x54c37e17,0xbe59,0x4adb,{0xb3,0x38,0xd2,0xf0,0x4b,0xd2,0xa0,0xa5}};
 CComPtr<IClassFactory> factory; CHECK(factoryFn(clsid,IID_PPV_ARGS(&factory)));
 CComPtr<IBaseFilter> filter; CHECK(factory->CreateInstance(nullptr,IID_PPV_ARGS(&filter)));
 CComQIPtr<IMediaSeeking> seeking(filter); REQUIRE(seeking);
 CComQIPtr<IMpcAudioRendererSpatial> spatial(filter); REQUIRE(spatial);
 REQUIRE(spatial->SetSpatialMode(42)==E_INVALIDARG);
 CHECK(spatial->SetSpatialMode(MPC_SPATIAL_DISABLED)); REQUIRE(spatial->GetSpatialMode()==MPC_SPATIAL_DISABLED);
 CComQIPtr<IMpcAudioRendererFilter> settings(filter); REQUIRE(settings);
 CHECK(settings->SetReleaseDeviceIdle(TRUE));CHECK(settings->SetPauseKeepActive(FALSE));
 CHECK(settings->SetWasapiMethod(WASAPI_METHOD::PUSH));
 CComPtr<IEnumPins> pins; CHECK(filter->EnumPins(&pins));
 CComPtr<IPin> pin; REQUIRE(pins->Next(1,&pin,nullptr)==S_OK);
 WAVEFORMATEXTENSIBLE w={};
 w.Format={WAVE_FORMAT_EXTENSIBLE,24,48000,48000*24*2,24*2,16,22}; w.Samples.wValidBitsPerSample=16; w.SubFormat=MEDIASUBTYPE_PCM;
 AM_MEDIA_TYPE mt={};mt.majortype=MEDIATYPE_Audio;mt.subtype=MEDIASUBTYPE_MPC_22POINT2;mt.bFixedSizeSamples=TRUE;mt.lSampleSize=w.Format.nBlockAlign;mt.formattype=FORMAT_WaveFormatEx;mt.cbFormat=sizeof(w);mt.pbFormat=(BYTE*)&w;
 REQUIRE(pin->QueryAccept(&mt)==S_OK);
 mt.cbFormat=sizeof(WAVEFORMATEX);REQUIRE(pin->QueryAccept(&mt)!=S_OK);mt.cbFormat=sizeof(w);
 w.Format.nAvgBytesPerSec++;REQUIRE(pin->QueryAccept(&mt)!=S_OK);w.Format.nAvgBytesPerSec--;
 w.dwChannelMask=3;REQUIRE(pin->QueryAccept(&mt)!=S_OK);w.dwChannelMask=0;
 mt.subtype=MEDIASUBTYPE_PCM;REQUIRE(pin->QueryAccept(&mt)!=S_OK);mt.subtype=MEDIASUBTYPE_MPC_22POINT2;
 // Distinguish PCM16 from float32 and reject malformed metadata.
 w.SubFormat=MEDIASUBTYPE_IEEE_FLOAT; REQUIRE(pin->QueryAccept(&mt)!=S_OK);
 w.Format.wBitsPerSample=w.Samples.wValidBitsPerSample=32;w.Format.nBlockAlign=96;w.Format.nAvgBytesPerSec=48000*96;REQUIRE(pin->QueryAccept(&mt)==S_OK);
 w.SubFormat=MEDIASUBTYPE_PCM;w.Format.wBitsPerSample=w.Samples.wValidBitsPerSample=16;w.Format.nBlockAlign=48;w.Format.nAvgBytesPerSec=48000*48;
 printf("PASS format negotiation, malformed metadata and COM settings ABI\n");
 OutputPin source;
 // Exercise both manual downmix and automatic spatial/fallback paths.
 for(int scenario: {0,1,2}){
  const int mode = scenario == 0 ? MPC_SPATIAL_DISABLED : MPC_SPATIAL_AUTO;
  const int frameBytes = scenario == 2 ? 96 : 48;
  if(scenario == 2) {
   w.SubFormat=MEDIASUBTYPE_IEEE_FLOAT;
   w.Format.wBitsPerSample=w.Samples.wValidBitsPerSample=32;
   w.Format.nBlockAlign=frameBytes;w.Format.nAvgBytesPerSec=48000*frameBytes;
   mt.lSampleSize=frameBytes;
  }
  CHECK(spatial->SetSpatialMode(mode));
  CHECK(pin->ReceiveConnection(&source,&mt));
  WAVEFORMATEX *in=nullptr,*out=nullptr; CHECK(settings->GetStatus(&in,&out)); REQUIRE(in&&out); REQUIRE(in->nChannels==24);
  HRESULT status=spatial->GetSpatialStatus();
  printf("mode %d: spatial=%08lx, input=%u bits, output=%u channels/%lu Hz\n",mode,status,w.Format.wBitsPerSample,out->nChannels,out->nSamplesPerSec);
  if(status==S_OK) REQUIRE(out->nChannels==24); else REQUIRE(out->nChannels<=18);
  CComQIPtr<IMemInputPin> input(pin); REQUIRE(input);
  CComPtr<IMemAllocator> alloc; CHECK(input->GetAllocator(&alloc));
  ALLOCATOR_PROPERTIES want={2,480*frameBytes,1,0},actual={}; CHECK(alloc->SetProperties(&want,&actual)); CHECK(input->NotifyAllocator(alloc,FALSE)); CHECK(alloc->Commit());
  CHECK(filter->Pause()); CHECK(filter->Run(0));
  CHECK(pin->NewSegment(0,10000000,1.0));
  for(int i=0;i<60;i++){
   CComPtr<IMediaSample> sample; CHECK(alloc->GetBuffer(&sample,nullptr,nullptr,0));BYTE* data=nullptr;CHECK(sample->GetPointer(&data));memset(data,0,480*frameBytes);CHECK(sample->SetActualDataLength(480*frameBytes));
   REFERENCE_TIME a=i*100000LL,b=a+100000;CHECK(sample->SetTime(&a,&b));CHECK(sample->SetSyncPoint(TRUE));CHECK(input->Receive(sample));
   if(mode==MPC_SPATIAL_AUTO && i==10) CHECK(seeking->SetRate(1.25));
   if(mode==MPC_SPATIAL_AUTO && i==12) REQUIRE(spatial->GetSpatialStatus()!=S_OK);
   if(mode==MPC_SPATIAL_AUTO && i==15) CHECK(seeking->SetRate(1.0));
   if(i==20){CHECK(filter->Pause()); Sleep(3300); CHECK(filter->Run(0));}
   if(mode==MPC_SPATIAL_AUTO && i==45) CHECK(spatial->SetSpatialMode(MPC_SPATIAL_DISABLED));
   if(mode==MPC_SPATIAL_AUTO && i==50) CHECK(spatial->SetSpatialMode(MPC_SPATIAL_AUTO));
   if(i==40){CHECK(pin->BeginFlush());CHECK(pin->EndFlush());CHECK(pin->NewSegment(0,10000000,1.0));}
  }
  Sleep(100);
  HRESULT after=spatial->GetSpatialStatus();
  printf("after streaming: spatial=%08lx\n",after);
  if(status==S_OK) REQUIRE(after==S_OK);
  CHECK(filter->Stop()); CHECK(alloc->Decommit()); CHECK(pin->Disconnect());
  printf("PASS silent streaming, idle release/resume, live mode switch, flush and stop (%d)\n",mode);
 }
 // Ensure ordinary stereo still negotiates after a 24ch stream.
 WAVEFORMATEX stereo={WAVE_FORMAT_PCM,2,48000,192000,4,16,0};
 mt.subtype=MEDIASUBTYPE_PCM;mt.cbFormat=sizeof(stereo);mt.pbFormat=(BYTE*)&stereo;mt.lSampleSize=4;
 CHECK(pin->ReceiveConnection(&source,&mt)); REQUIRE(spatial->GetSpatialStatus()==S_FALSE);CHECK(filter->Stop());CHECK(pin->Disconnect());
 printf("PASS 24ch -> ordinary stereo format transition\n");
 return 0;
}
