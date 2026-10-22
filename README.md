# MPCVideoDec-fork

Standalone builds of selected DirectShow filters from
[MPC-BE](https://github.com/Aleksoid1978/MPC-BE), with fixes that didn't make
it upstream and dark mode support for their property pages. This repository
does not build the MPC-BE player itself, only these filters:

- **MPCVideoDec-fork.ax** - the D3D11/DXVA2 hardware video decoder.
- **MpcAudioRenderer-fork.ax** - the WASAPI audio renderer.

## Why this exists

This fork carries a couple of D3D11 decode fixes on top of upstream
[MPC-BE](https://github.com/Aleksoid1978/MPC-BE), worked out while running
this decoder under [BonDriver_dantto4k](https://github.com/0p1pkt2/dantto4k)-fed
4K/8K broadcast streams in [TVTest](https://github.com/DBCTRADO/TVTest):

- A dangling pointer crash on a 4K→8K format switch in the DXVA2/D3D11
  hwaccel packet path (`FillAVPacket`).
- Output format changes no longer trigger `EC_DISPLAY_CHANGED`, which used
  to make the filter graph manager stop and restart the whole graph -
  occasionally leaving D3D11 device creation in a bad state (no video on
  startup or after a renderer switch).

This fork builds and distributes its own MPCVideoDec-fork.ax independently.

## Getting the decoder

Prebuilt `MPCVideoDec-fork.ax` (x64) is published on the
[Releases](https://github.com/nanamitm/MPCVideoDec-fork/releases) page,
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
