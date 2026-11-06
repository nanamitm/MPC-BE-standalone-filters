# MPC-BE-standalone-filters

(Formerly `MPCVideoDec-fork`; old links redirect here.)

Standalone builds of selected DirectShow filters from
[MPC-BE](https://github.com/Aleksoid1978/MPC-BE), with fixes that didn't make
it upstream and dark mode support for their property pages. This repository
does not build the MPC-BE player itself, only these filters:

- **MPCVideoDec-fork.ax** - the D3D11/DXVA2 hardware video decoder.
- **MpcAudioRenderer-fork.ax** - the WASAPI audio renderer.

## Why this exists

This fork started out carrying a couple of D3D11 decode fixes on top of upstream
[MPC-BE](https://github.com/Aleksoid1978/MPC-BE), worked out while running
this decoder under [BonDriver_dantto4k](https://github.com/0p1pkt2/dantto4k)-fed
4K/8K broadcast streams in [TVTest](https://github.com/DBCTRADO/TVTest):

- A dangling pointer crash on a 4K→8K format switch in the DXVA2/D3D11
  hwaccel packet path (`FillAVPacket`).
- Output format changes no longer trigger `EC_DISPLAY_CHANGED`, which used
  to make the filter graph manager stop and restart the whole graph -
  occasionally leaving D3D11 device creation in a bad state (no video on
  startup or after a renderer switch).

Since then it has grown into a small set of standalone filters, released
independently of upstream.

## Getting the decoder

Prebuilt `MPCVideoDec-fork.ax` (x64) is published on the
[Releases](https://github.com/nanamitm/MPC-BE-standalone-filters/releases) page,
tagged `v*` (`MPCVideoDec-fork-x64.zip`). Register it like any other DirectShow filter
(`regsvr32 MPCVideoDec-fork.ax`), or point a standalone-filters-aware player at
it directly.

This build registers under its own CLSIDs and filter names
(`MPC Video Decoder (nanamitm)` / `MPC Video Converter (nanamitm)`), so it
won't collide in the registry with an official MPC-BE install on the same
machine.

## Audio renderer

`MpcAudioRenderer-fork.ax` is upstream's MPC Audio Renderer, built on the same
dark-mode-aware property page base as the decoder. It registers as
`MPC Audio Renderer (nanamitm)` with its own CLSIDs, and keeps its settings
under `HKCU\Software\nanamitm Filters\MPC Audio Renderer`, so it can sit next
to an official MPC-BE install. It is released alongside the decoder under the
same `v*` tags (`MpcAudioRenderer-fork-x64.zip`). Register it with
`regsvr32 MpcAudioRenderer-fork.ax`.

### 22.2-channel spatial output

The renderer accepts an explicitly labelled 22.2-channel PCM transport and can
render it through Windows Spatial Audio on the selected audio device. The new
**22.2ch output** setting offers **Spatial / Auto** (the default) and **Downmix**.
Auto requires spatial sound to be enabled on that device, support for 48 kHz
mono float objects, and capacity for 23 dynamic objects. LFE1 and LFE2 are
averaged into one non-directional-position object; the other 22 channels use
fixed positions. This is object-based spatial playback, not discrete output to
24 physical speakers or an Atmos bitstream encoder. The LFE object has no
dedicated hardware LFE routing guarantee.

When unavailable, or after a spatial stream error, the renderer downmixes to the
selected endpoint's normal WASAPI shared format. The full 64-bit input layout
is preserved for mixing, and the matrix is normalized to avoid summation
clipping. A failed spatial stream is retried after an endpoint/settings change
or a new input connection, not on every sample. The status page reports the
active path and the last spatial initialization/stream error. Playback speed
changes use the normal downmix path. Normal PCM and bitstream inputs continue
to use the existing WASAPI settings.

The spatial backend shares the renderer's packet queue, volume/mute/balance,
device selection/notifications, flush handling, and hardware audio clock. It
uses event-driven object updates even when the WASAPI setting is Push.

#### Decoder connection contract

[`SpatialAudioFormat.h`](src/filters/renderer/MpcAudioRenderer/SpatialAudioFormat.h)
defines the version 1 media subtype `MEDIASUBTYPE_MPC_22POINT2` and the separate
`IMpcAudioRendererSpatial` settings interface (the old settings IID/vtable is
unchanged). An upstream decoder must send:

- `MEDIATYPE_Audio`, `MEDIASUBTYPE_MPC_22POINT2`, `FORMAT_WaveFormatEx`.
- `WAVEFORMATEXTENSIBLE`, `cbSize = 22`, 24 channels, 48000 Hz,
  `dwChannelMask = 0`, valid bits equal to container bits.
- Interleaved signed PCM16 (`SubFormat = MEDIASUBTYPE_PCM`) or float32
  (`SubFormat = MEDIASUBTYPE_IEEE_FLOAT`), with matching block alignment and
  byte rate.
- This exact channel order (FFmpeg native `AV_CH_LAYOUT_22POINT2`):
  `FL FR FC LFE BL BR FLC FRC BC SL SR TC TFL TFC TFR TBL TBC TBR LFE2 TSL TSR BFC BFL BFR`.

Unlabelled 24-channel PCM is rejected: channel count alone does not describe
speaker positions. LibISDB/TVTest needs a corresponding connection change to
use this path; installing this filter alone does not enable 22.2ch playback
from the existing LibISDB output.

#### Validation

From an x64 Visual Studio Native Tools prompt, after building the renderer and
its dependencies into the same directory, run:

```bat
tests\run_spatial_audio_tests.cmd _bin\validation-upstream-x64
```

The smoke test loads the AX directly without registration or saving settings.
It sends silence to the current endpoint and checks malformed format rejection,
PCM16/float32 negotiation, downmix and automatic spatial/fallback streaming,
idle release/resume, live mode changes, flush, and a 24ch-to-stereo transition.
The mixer test checks individual impulses on all 24 channels and their normalized
sum into stereo, 5.1, and 7.1. These tests do not validate perceived spatial
direction, real broadcast A/V sync, hot-unplug recovery, or long playback.

## Building

This repository is trimmed to just the source these filters need (FFmpeg,
vvdec, uavs3d, soxr, speex, mfx_dispatch, libbs2b, AudioTools, DSUtil,
BaseClasses, and the filter base classes) - it's not a full MPC-BE checkout.

```
build.bat Build x64 Filters Release NoWait
```

produces `_bin\Filters_x64\MPCVideoDec-fork.ax` and
`_bin\Filters_x64\MpcAudioRenderer-fork.ax`. See
[`.github/workflows/release.yml`](.github/workflows/release.yml)
for the exact CI recipe (MinGW/GCC toolchain setup for the bundled FFmpeg,
then an MSVC build of the filters themselves).

For local registration helpers, use
[`scripts/Install_MPCVideoDec-fork_64.cmd`](scripts/Install_MPCVideoDec-fork_64.cmd) /
[`scripts/Uninstall_MPCVideoDec-fork_64.cmd`](scripts/Uninstall_MPCVideoDec-fork_64.cmd)
and
[`scripts/Install_MpcAudioRenderer-fork_64.cmd`](scripts/Install_MpcAudioRenderer-fork_64.cmd) /
[`scripts/Uninstall_MpcAudioRenderer-fork_64.cmd`](scripts/Uninstall_MpcAudioRenderer-fork_64.cmd).

## License

GPLv3, same as upstream MPC-BE. See [LICENSE.txt](LICENSE.txt) and
[Aleksoid1978/MPC-BE](https://github.com/Aleksoid1978/MPC-BE) for the
original project and full credits.
