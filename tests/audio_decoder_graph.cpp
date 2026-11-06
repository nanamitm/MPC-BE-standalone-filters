// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>
#include <dshow.h>
#include <atlbase.h>
#include <atlstr.h>
#include <memory>
#include <vector>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <streams.h>
#include <initguid.h>
#include <moreuuids.h>
#include "filters/renderer/MpcAudioRenderer/SpatialAudioFormat.h"
#include "SilentLatm.h"
#include "filters/switcher/AudioSwitcher/IAudioSwitcherFilter.h"
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"strmiids.lib")
#pragma comment(lib,"winmm.lib")
#pragma comment(lib,"uuid.lib")
HINSTANCE g_hInst = GetModuleHandleW(nullptr);
#define CHECK(x) do { HRESULT result=(x); if(FAILED(result)){printf("FAIL %d: %s = %08lx\n",__LINE__,#x,result);return 1;} }while(0)
#define REQUIRE(x) do { if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;} }while(0)

class SourcePin final : public CBaseOutputPin {
	CMediaType m_type;
public:
	SourcePin(CBaseFilter* filter, CCritSec* lock, HRESULT* hr, const CMediaType& type)
		: CBaseOutputPin(NAME("Test source pin"),filter,lock,hr,L"Out"),m_type(type) {}
	HRESULT CheckMediaType(const CMediaType*) override { return S_OK; }
	HRESULT GetMediaType(int index, CMediaType* type) override {
		if(index) return VFW_S_NO_MORE_ITEMS; *type=m_type; return S_OK;
	}
	HRESULT DecideBufferSize(IMemAllocator* allocator, ALLOCATOR_PROPERTIES* props) override {
		props->cBuffers=4; props->cbBuffer=65536; props->cbAlign=1; props->cbPrefix=0;
		ALLOCATOR_PROPERTIES actual{}; return allocator->SetProperties(props,&actual);
	}
	HRESULT Send(const std::vector<uint8_t>& bytes, unsigned index) {
		CComPtr<IMediaSample> sample;
		HRESULT hr=GetDeliveryBuffer(&sample,nullptr,nullptr,0); if(FAILED(hr))return hr;
		BYTE* data=nullptr; if(FAILED(hr=sample->GetPointer(&data)))return hr;
		if(bytes.size()>static_cast<size_t>(sample->GetSize()))return E_INVALIDARG;
		memcpy(data,bytes.data(),bytes.size()); sample->SetActualDataLength(static_cast<long>(bytes.size()));
		REFERENCE_TIME start=index*1024LL*10000000/48000, end=(index+1)*1024LL*10000000/48000;
		sample->SetTime(&start,&end); sample->SetSyncPoint(TRUE); sample->SetDiscontinuity(index==0);
		return Deliver(sample);
	}
};
class Source : public CBaseFilter {
	CCritSec m_lock;
	std::unique_ptr<SourcePin> m_pin;
public:
	Source(HRESULT* hr,const CMediaType& type):CBaseFilter(NAME("Test source"),nullptr,&m_lock,CLSID_NULL) {
		m_pin=std::make_unique<SourcePin>(this,&m_lock,hr,type);
	}
	int GetPinCount() override{return 1;}
	CBasePin* GetPin(int index) override{return index==0?m_pin.get():nullptr;}
	HRESULT Send(const std::vector<uint8_t>& data,unsigned index){return m_pin->Send(data,index);}
};
class Capture : public CBaseRenderer, public IMpcAudioRendererSpatial {
public:
	bool spatial;
	unsigned samples=0;
	REFERENCE_TIME start=0,stop=0;
	std::vector<unsigned> channelHistory;
	CMediaType type;
	std::vector<uint8_t> last;
	Capture(HRESULT* hr,bool enable):CBaseRenderer(CLSID_AudioRender,NAME("Test capture"),nullptr,hr),spatial(enable){}
	DECLARE_IUNKNOWN;
	STDMETHODIMP NonDelegatingQueryInterface(REFIID id,void** value) override {
		if(spatial && id==__uuidof(IMpcAudioRendererSpatial))return GetInterface(static_cast<IMpcAudioRendererSpatial*>(this),value);
		return CBaseRenderer::NonDelegatingQueryInterface(id,value);
	}
	STDMETHODIMP SetSpatialMode(INT) override{return E_NOTIMPL;}
	STDMETHODIMP_(INT) GetSpatialMode() override{return MPC_SPATIAL_AUTO;}
	STDMETHODIMP GetSpatialStatus() override{return S_FALSE;}
	HRESULT CheckMediaType(const CMediaType* mt) override {
		if(mt->majortype!=MEDIATYPE_Audio || mt->formattype!=FORMAT_WaveFormatEx || mt->cbFormat<sizeof(WAVEFORMATEX))return VFW_E_TYPE_NOT_ACCEPTED;
		if(mt->subtype==MEDIASUBTYPE_MPC_22POINT2)return spatial && MpcSpatial::ValidFormat((WAVEFORMATEX*)mt->pbFormat,mt->cbFormat)?S_OK:VFW_E_TYPE_NOT_ACCEPTED;
		const auto* w=(WAVEFORMATEX*)mt->pbFormat;
		return (mt->subtype==MEDIASUBTYPE_PCM || mt->subtype==MEDIASUBTYPE_IEEE_FLOAT)
			&& (w->wFormatTag==WAVE_FORMAT_PCM || w->wFormatTag==WAVE_FORMAT_IEEE_FLOAT || w->wFormatTag==WAVE_FORMAT_EXTENSIBLE)
			?S_OK:VFW_E_TYPE_NOT_ACCEPTED;
	}
	HRESULT SetMediaType(const CMediaType* mt) override { type=*mt; return CBaseRenderer::SetMediaType(mt); }
	HRESULT DoRenderSample(IMediaSample* sample) override {
		BYTE* data=nullptr;
		HRESULT hr=sample->GetPointer(&data);if(FAILED(hr))return hr;
		last.assign(data,data+sample->GetActualDataLength());
		channelHistory.push_back(reinterpret_cast<const WAVEFORMATEX*>(type.pbFormat)->nChannels);
		sample->GetTime(&start,&stop);
		++samples;return S_OK;
	}
};

static HRESULT LoadFilter(HMODULE module,REFCLSID id,IBaseFilter** result)
{
	using Proc=HRESULT(STDAPICALLTYPE*)(REFCLSID,REFIID,void**);
	auto proc=reinterpret_cast<Proc>(GetProcAddress(module,"DllGetClassObject"));if(!proc)return E_NOINTERFACE;
	CComPtr<IClassFactory> factory;HRESULT hr=proc(id,IID_PPV_ARGS(&factory));
	return FAILED(hr)?hr:factory->CreateInstance(nullptr,IID_PPV_ARGS(result));
}
int wmain(int argc,wchar_t** argv)
{
	REQUIRE(argc>=2);CHECK(CoInitializeEx(nullptr,COINIT_MULTITHREADED));
	HMODULE module=LoadLibraryW(argv[1]);REQUIRE(module);
	const CLSID id={0xe0455945,0x14d5,0x4cf2,{0x90,0x87,0xac,0xb6,0x29,0x83,0xf8,0x71}};
	const unsigned config=argc>2?wcstoul(argv[2],nullptr,10):2;
	const bool transitions=argc>4 && wcstoul(argv[4],nullptr,10);
	const bool spatial=argc>3 && wcstoul(argv[3],nullptr,10);
	CMediaType input;input.SetType(&MEDIATYPE_Audio);input.SetSubtype(&MEDIASUBTYPE_LATM_AAC);input.SetFormatType(&FORMAT_WaveFormatEx);
	WAVEFORMATEX w={0,2,48000,0,1,0,0};input.SetFormat(reinterpret_cast<BYTE*>(&w),sizeof(w));
	if(config==24) {
		WAVEFORMATEXTENSIBLE pcm{};pcm.Format={WAVE_FORMAT_EXTENSIBLE,24,48000,48000*96,96,32,22};
		pcm.Samples.wValidBitsPerSample=32;pcm.SubFormat=MEDIASUBTYPE_IEEE_FLOAT;
		input.SetSubtype(&MEDIASUBTYPE_MPC_22POINT2);input.SetFormat(reinterpret_cast<BYTE*>(&pcm),sizeof(pcm));
	}
	HRESULT hr=S_OK;
	CComPtr<Source> source=new Source(&hr,input);CHECK(hr);
	CComPtr<Capture> sink=new Capture(&hr,spatial);CHECK(hr);
	CComPtr<IBaseFilter> decoder;CHECK(LoadFilter(module,id,&decoder));
	CComPtr<IGraphBuilder> graph;CHECK(graph.CoCreateInstance(CLSID_FilterGraph));
	CHECK(graph->AddFilter(source,L"Source"));CHECK(graph->AddFilter(decoder,L"Decoder"));CHECK(graph->AddFilter(sink,L"Capture"));
	CComPtr<IEnumPins> pins;CHECK(decoder->EnumPins(&pins));
	CComPtr<IPin> in,out;CHECK(pins->Next(1,&in,nullptr));CHECK(pins->Next(1,&out,nullptr));
	CComPtr<IPin> producer=out;
	if(config==24) producer=source->GetPin(0);
	else CHECK(graph->ConnectDirect(source->GetPin(0),in,&input));
	CComPtr<IBaseFilter> actualRenderer;
	IPin* destination=sink->GetPin(0);CComPtr<IPin> rendererInput;
	if(argc>6) {
		HMODULE rendererModule=LoadLibraryW(argv[6]);REQUIRE(rendererModule);
		const CLSID rendererId={0x54c37e17,0xbe59,0x4adb,{0xb3,0x38,0xd2,0xf0,0x4b,0xd2,0xa0,0xa5}};
		CHECK(LoadFilter(rendererModule,rendererId,&actualRenderer));
		CHECK(graph->RemoveFilter(sink));CHECK(graph->AddFilter(actualRenderer,L"MPC Renderer"));
		CComPtr<IEnumPins> list;CHECK(actualRenderer->EnumPins(&list));CHECK(list->Next(1,&rendererInput,nullptr));
		destination=rendererInput;
	}
	CComPtr<IBaseFilter> switcher; CComPtr<IPin> switchIn,switchOut;
	if(argc>5 && wcscmp(argv[5],L"-")) {
		HMODULE switchModule=LoadLibraryW(argv[5]);REQUIRE(switchModule);
		const CLSID switchId={0xa7d8300f,0x4619,0x40ca,{0xbf,0xf6,0x70,0x32,0xd5,0x2f,0x6e,0xe2}};
		CHECK(LoadFilter(switchModule,switchId,&switcher));CHECK(graph->AddFilter(switcher,L"Switcher"));
		CComQIPtr<IMpcAudioRendererSpatial> forwarding(switcher);REQUIRE(forwarding);
		REQUIRE(forwarding->GetSpatialMode()==MPC_SPATIAL_UNAVAILABLE);
		CComPtr<IEnumPins> list;CHECK(switcher->EnumPins(&list));CHECK(list->Next(1,&switchOut,nullptr));CHECK(list->Next(1,&switchIn,nullptr));
		CHECK(graph->ConnectDirect(producer,switchIn,nullptr));CHECK(graph->ConnectDirect(switchOut,destination,nullptr));
		REQUIRE(forwarding->GetSpatialMode()==(spatial?MPC_SPATIAL_AUTO:MPC_SPATIAL_UNAVAILABLE));
		if(config==24) {
			CComQIPtr<IAudioSwitcherFilter> settings(switcher);REQUIRE(settings);
			CHECK(settings->SetChannelMixer(true,SPK_STEREO));CHECK(settings->SetAudioGain(6));
			CHECK(settings->SetAutoVolumeControl(true,true,75,8));CHECK(settings->SetOutputFormats(SFMT_INT16));
			CHECK(settings->SetAudioTimeShift(10000));
		}
	} else CHECK(graph->ConnectDirect(producer,destination,nullptr));
	CComQIPtr<IMediaFilter> media(graph);CHECK(media->SetSyncSource(nullptr));
	CComQIPtr<IMediaControl> control(graph);CHECK(control->Run());
	auto frame=MakeSilentLatm(config);
	if(config==24) {
		std::vector<float> pcm(256*24);
		for(size_t i=0;i<pcm.size();++i)pcm[i]=float(int(i%127)-63)/128;
		frame.assign(reinterpret_cast<uint8_t*>(pcm.data()),reinterpret_cast<uint8_t*>(pcm.data()+pcm.size()));
	}
	for(unsigned i=0;i<12;i++) {
		if (transitions) sink->spatial = i<4 || i>=8;
		CHECK(source->Send(frame,i));
	}
	if(actualRenderer) {
		CComQIPtr<IMpcAudioRendererSpatial> status(actualRenderer);REQUIRE(status);
		CMediaType delivered;CHECK(rendererInput->ConnectionMediaType(&delivered));
		REQUIRE(delivered.subtype==MEDIASUBTYPE_MPC_22POINT2);
		printf("PASS decoder -> Switcher -> actual MPC renderer, 24ch format, spatial status=%08lx\n",status->GetSpatialStatus());
		CHECK(control->Stop());return 0;
	}
	if (transitions) {
		REQUIRE(std::find(sink->channelHistory.begin(),sink->channelHistory.end(),8)!=sink->channelHistory.end());
		REQUIRE(std::find(sink->channelHistory.begin(),sink->channelHistory.end(),24)!=sink->channelHistory.end());
	}
	REQUIRE(sink->samples>0);
	if(config==24) {
		REQUIRE(sink->last==frame);
		REQUIRE(sink->start==11*1024LL*10000000/48000+10000);
		REQUIRE(sink->stop==12*1024LL*10000000/48000+10000);
		printf("PASS Switcher preserves nonzero 24ch bytes/timestamps with stereo mixer, gain, normalizer and PCM16 settings enabled\n");
	}
	const auto* output=reinterpret_cast<const WAVEFORMATEX*>(sink->type.pbFormat);
	printf("PASS LATM config=%u spatial=%d samples=%u output=%u channels/%lu Hz/%u bits, subtype=%08lx\n",config,spatial,sink->samples,output->nChannels,output->nSamplesPerSec,output->wBitsPerSample,sink->type.subtype.Data1);
	if(config==1)REQUIRE(output->nChannels==1);
	if(config==2)REQUIRE(output->nChannels==2);
	if(config==13 && !spatial)REQUIRE(output->nChannels==8 && sink->type.subtype==MEDIASUBTYPE_PCM);
	if(config==13 && spatial)REQUIRE(sink->type.subtype==MEDIASUBTYPE_MPC_22POINT2 && MpcSpatial::ValidFormat(output,sink->type.cbFormat));
	CHECK(control->Stop());CHECK(graph->Disconnect(source->GetPin(0)));CHECK(graph->Disconnect(in));CHECK(graph->Disconnect(out));CHECK(graph->Disconnect(sink->GetPin(0)));
	if(switcher){CHECK(graph->Disconnect(switchIn));CHECK(graph->Disconnect(switchOut));}
	return 0;
}
