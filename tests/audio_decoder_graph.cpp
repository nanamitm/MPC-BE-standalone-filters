// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>
#include <dshow.h>
#include <atlbase.h>
#include <atlstr.h>
#include <memory>
#include <vector>
#include <cstdio>
#include <cmath>
#include <thread>
#include <ocidl.h>
#include <algorithm>
#include <streams.h>
#include <initguid.h>
#include <moreuuids.h>
#include "filters/renderer/MpcAudioRenderer/SpatialAudioFormat.h"
#include "SilentLatm.h"
#include "DualMonoAAC.h"
#include <IAudioDualMono.h>
#pragma comment(lib,"advapi32.lib")
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
	HANDLE blockEntered=nullptr,blockRelease=nullptr;
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
		if(blockEntered){SetEvent(blockEntered);WaitForSingleObject(blockRelease,5000);}
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
// Keep this console test independent of the MFC property-page header.
interface __declspec(uuid("03481710-D73E-4674-839F-03EDE2D60ED8"))
TestPropertyPages2 : public ISpecifyPropertyPages {
    STDMETHOD(CreatePage)(const GUID& guid, IPropertyPage** page) PURE;
};
class TestPageSite final : public IPropertyPageSite {
    LONG refs=1;
public:
    STDMETHODIMP QueryInterface(REFIID id,void** result) override {
        if(!result)return E_POINTER;*result=nullptr;
        if(id!=IID_IUnknown && id!=IID_IPropertyPageSite)return E_NOINTERFACE;
        *result=static_cast<IPropertyPageSite*>(this);AddRef();return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override {return InterlockedIncrement(&refs);}
    STDMETHODIMP_(ULONG) Release() override {const ULONG n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
    STDMETHODIMP OnStatusChange(DWORD) override {return S_OK;}
    STDMETHODIMP GetLocaleID(LCID* id) override {if(!id)return E_POINTER;*id=GetUserDefaultLCID();return S_OK;}
    STDMETHODIMP GetPageContainer(IUnknown** p) override {if(p)*p=nullptr;return E_NOTIMPL;}
    STDMETHODIMP TranslateAccelerator(MSG*) override {return E_NOTIMPL;}
};
static int CheckPropertyPage(IBaseFilter* decoder,ILAVAudioDualMono* dual) {
    CComQIPtr<TestPropertyPages2> pages(decoder);REQUIRE(pages);
    CAUUID ids{};CHECK(pages->GetPages(&ids));REQUIRE(ids.cElems==1);
    CComPtr<IPropertyPage> page;CHECK(pages->CreatePage(ids.pElems[0],&page));CoTaskMemFree(ids.pElems);
    CComPtr<IPropertyPageSite> site;site.Attach(new TestPageSite);CHECK(page->SetPageSite(site));
    IUnknown* object=decoder;CHECK(page->SetObjects(1,&object));
    PROPPAGEINFO info{};CHECK(page->GetPageInfo(&info));CoTaskMemFree(info.pszTitle);
    HWND parent=CreateWindowExW(0,L"STATIC",L"Audio settings test",WS_POPUP,0,0,info.size.cx,info.size.cy,nullptr,nullptr,g_hInst,nullptr);REQUIRE(parent);
    RECT bounds={0,0,info.size.cx,info.size.cy};CHECK(page->Activate(parent,&bounds,FALSE));
    HWND pageWindow=GetWindow(parent,GW_CHILD);REQUIRE(pageWindow);
    HWND combo=GetDlgItem(pageWindow,9999);REQUIRE(combo);
    REQUIRE(SendMessageW(combo,CB_GETCOUNT,0,0)==3);
    REQUIRE(SendMessageW(combo,CB_GETCURSEL,0,0)==dual->GetDualMonoMode());
    RECT client{};GetClientRect(pageWindow,&client);
    for(HWND child=GetWindow(pageWindow,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) {
        RECT r{};GetWindowRect(child,&r);MapWindowPoints(nullptr,pageWindow,reinterpret_cast<POINT*>(&r),2);
        // Combo boxes include their drop-down height in the window rectangle.
        if(child==combo) {COMBOBOXINFO cbi{sizeof(cbi)};REQUIRE(GetComboBoxInfo(combo,&cbi));r.bottom=r.top+(cbi.rcItem.bottom-cbi.rcItem.top)+8;}
        if(r.bottom>client.bottom || r.right>client.right) {printf("FAIL property control %d bounds %ld/%ld vs %ld/%ld\n",GetDlgCtrlID(child),r.right,r.bottom,client.right,client.bottom);return 1;}
    }
    SendMessageW(combo,CB_SETCURSEL,DualMono_Main,0);
    SendMessageW(pageWindow,WM_COMMAND,MAKEWPARAM(9999,CBN_SELCHANGE),reinterpret_cast<LPARAM>(combo));
    CHECK(page->Apply());REQUIRE(dual->GetDualMonoMode()==DualMono_Main);
    CHECK(page->Deactivate());CHECK(page->SetObjects(0,nullptr));CHECK(page->SetPageSite(nullptr));DestroyWindow(parent);
    printf("PASS property page three choices, control bounds and Apply\n");return 0;
}
class RegistrySandbox {
    HKEY key=nullptr;
    wchar_t path[160]{};
public:
    bool Open() {
        swprintf_s(path,L"Software\\nanamitm Filters\\Tests\\DualMono-%lu",GetCurrentProcessId());
        return RegCreateKeyExW(HKEY_CURRENT_USER,path,0,nullptr,0,KEY_ALL_ACCESS,nullptr,&key,nullptr)==ERROR_SUCCESS
            && RegOverridePredefKey(HKEY_CURRENT_USER,key)==ERROR_SUCCESS;
    }
    ~RegistrySandbox() {
        if(key) { RegOverridePredefKey(HKEY_CURRENT_USER,nullptr);RegCloseKey(key);RegDeleteTreeW(HKEY_CURRENT_USER,path); }
    }
};
int wmain(int argc,wchar_t** argv)
{
	REQUIRE(argc>=2);CHECK(CoInitializeEx(nullptr,COINIT_MULTITHREADED));
	RegistrySandbox sandbox;
	REQUIRE(sandbox.Open());
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
	if(config==99 || config==97) input.SetSubtype(&MEDIASUBTYPE_AAC_ADTS);
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
	CComQIPtr<ILAVAudioDualMono> dual(decoder);
	CComQIPtr<IAMStreamSelect> selection(decoder);
	DWORD savedMode=dual?dual->GetDualMonoMode():0;
	if(config==13) { REQUIRE(dual);CHECK(dual->SetDualMonoMode(DualMono_Sub)); }
	if(config>=97) {
		REQUIRE(dual && selection && savedMode==DualMono_Main);
		REQUIRE(dual->SetDualMonoMode(3)==E_INVALIDARG);
		DWORD count=0;CHECK(selection->Count(&count));REQUIRE(count==3);
		REQUIRE(selection->Count(nullptr)==E_POINTER);
		REQUIRE(selection->Enable(-1,AMSTREAMSELECTENABLE_ENABLE)==S_FALSE);
		REQUIRE(selection->Enable(0,0)==S_FALSE);
		CHECK(selection->Enable(0,AMSTREAMSELECTENABLE_ENABLE));
		DWORD flags=0,group=99;LPWSTR name=nullptr;
		CHECK(selection->Info(0,nullptr,&flags,nullptr,&group,&name,nullptr,nullptr));
		REQUIRE(flags==(AMSTREAMSELECTINFO_ENABLED|AMSTREAMSELECTINFO_EXCLUSIVE) && group==0 && name);CoTaskMemFree(name);
	}
	CComQIPtr<IMediaFilter> media(graph);CHECK(media->SetSyncSource(nullptr));
	CComQIPtr<IMediaControl> control(graph);CHECK(control->Run());
	auto frame=MakeSilentLatm(config);
	if(config==24) {
		std::vector<float> pcm(256*24);
		for(size_t i=0;i<pcm.size();++i)pcm[i]=float(int(i%127)-63)/128;
		frame.assign(reinterpret_cast<uint8_t*>(pcm.data()),reinterpret_cast<uint8_t*>(pcm.data()+pcm.size()));
	}
	double mainEnergy=0,subEnergy=0;
	for(unsigned i=0;i<(config>=97?24u:12u);i++) {
		if (transitions) sink->spatial = i<4 || i>=8;
		if(config>=97) {
			const unsigned mode=(i/4)%3;
			CHECK(selection->Enable(mode,AMSTREAMSELECTENABLE_ENABLE));
			frame=MakeDualMonoAAC(i>=12,config==98,config==97);
		}
		CHECK(source->Send(frame,i));
		if(config>=97 && i%4==3) {
			REQUIRE(sink->last.size()==1024*2*sizeof(int16_t));
			const auto* pcm=reinterpret_cast<const int16_t*>(sink->last.data());
			bool difference=false,nonzero=false;
			for(size_t n=0;n<1024;n++){difference|=pcm[n*2]!=pcm[n*2+1];nonzero|=pcm[n*2]!=0 || pcm[n*2+1]!=0;}
			printf("dual/stereo=%d mode=%u different=%d nonzero=%d\n",i>=12,(i/4)%3,difference,nonzero);
			double energy=0;
			for(size_t n=0;n<1024;n++)energy+=double(pcm[n*2])*pcm[n*2];
			if(i==7) mainEnergy=energy;
			if(i==11) subEnergy=energy;
			REQUIRE(nonzero && sink->samples==i+1);
			REQUIRE(difference==(i>=12 || (i/4)%3==DualMono_Both));
		}
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
	if(config>=97) {
		REQUIRE(mainEnergy>0 && subEnergy>mainEnergy*10); // sub fixture has greater gain
		sink->blockEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);
		sink->blockRelease=CreateEventW(nullptr,TRUE,FALSE,nullptr);
		REQUIRE(sink->blockEntered && sink->blockRelease);
		HRESULT sendResult=E_FAIL;
		std::thread blocked([&]{sendResult=source->Send(MakeDualMonoAAC(false,config==98,config==97),24);});
		const DWORD entered=WaitForSingleObject(sink->blockEntered,2000);
		const ULONGLONG begin=GetTickCount64();
		DWORD count=0;HRESULT a=selection->Count(&count);
		HRESULT b=selection->Info(0,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr);
		HRESULT c=selection->Enable(DualMono_Sub,AMSTREAMSELECTENABLE_ENABLE);
		const ULONGLONG duration=GetTickCount64()-begin;
		SetEvent(sink->blockRelease);blocked.join();
		CloseHandle(sink->blockEntered);CloseHandle(sink->blockRelease);
		sink->blockEntered=sink->blockRelease=nullptr;
		REQUIRE(entered==WAIT_OBJECT_0 && duration<1000 && count==3);
		CHECK(a);CHECK(b);CHECK(c);CHECK(sendResult);
		printf("PASS menu Count/Info/Enable while Receive/Deliver is blocked (%llu ms)\n",duration);
		CComPtr<IBaseFilter> reopened;CHECK(LoadFilter(module,id,&reopened));
		CComQIPtr<ILAVAudioDualMono> persisted(reopened);REQUIRE(persisted && persisted->GetDualMonoMode()==DualMono_Sub);
		CHECK(dual->SetDualMonoMode(savedMode));
		printf("PASS AAC dual mono Both/Main/Sub, live stereo transition, IAMStreamSelect, persistence and invalid arguments\n");
	}
	if(config==24) {
		REQUIRE(sink->last==frame);
		REQUIRE(sink->start==11*1024LL*10000000/48000+10000);
		REQUIRE(sink->stop==12*1024LL*10000000/48000+10000);
		printf("PASS Switcher preserves nonzero 24ch bytes/timestamps with stereo mixer, gain, normalizer and PCM16 settings enabled\n");
	}
	const auto* output=reinterpret_cast<const WAVEFORMATEX*>(sink->type.pbFormat);
	printf("PASS %s config=%u spatial=%d samples=%u output=%u channels/%lu Hz/%u bits, subtype=%08lx\n",config==99 || config==97?"ADTS":config==24?"PCM":"LATM",config,spatial,sink->samples,output->nChannels,output->nSamplesPerSec,output->wBitsPerSample,sink->type.subtype.Data1);
	if(config==1)REQUIRE(output->nChannels==1);
	if(config==2)REQUIRE(output->nChannels==2);
	if(config==13 && !spatial)REQUIRE(output->nChannels==8 && sink->type.subtype==MEDIASUBTYPE_PCM);
	if(config==13 && spatial)REQUIRE(sink->type.subtype==MEDIASUBTYPE_MPC_22POINT2 && MpcSpatial::ValidFormat(output,sink->type.cbFormat));
	CHECK(control->Stop());
	if(config>=97)REQUIRE(CheckPropertyPage(decoder,dual)==0);
	CHECK(graph->Disconnect(source->GetPin(0)));CHECK(graph->Disconnect(in));CHECK(graph->Disconnect(out));CHECK(graph->Disconnect(sink->GetPin(0)));
	if(switcher){CHECK(graph->Disconnect(switchIn));CHECK(graph->Disconnect(switchOut));}
	return 0;
}
