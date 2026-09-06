# PS Vita port

The `vita/vitagl` branch is the active PS Vita renderer branch. VitaGL is not
just a presentation layer: the adapted YGL path performs VDP1 rasterization,
VDP2 drawing, and composition on the GPU. The software backend remains useful
as a visual reference.

The native PowerShell build supports two presentation backends:

```powershell
.\build-vita.ps1 -Renderer VitaGL
.\build-vita.ps1 -Renderer Software
```

`VitaGL` is the default. BIOS-used projective sprites, Gouraud shading, point
sampling, clipping, priorities, and ordered half-transparency are implemented.
Decoded texture data is uploaded through dirty atlas regions, and VDP2 bitmap
surfaces can persist across frames. Full game compatibility is not yet the
acceptance target.

Saturn resolutions that fit within 480x272 are centered without doubling and
use point filtering. Larger output modes use the full 960x544 display target.

## vitaGL prerequisites

Install the official hard-float VitaSDK packages with native PowerShell:

```powershell
.\\install-vitagl-deps.ps1
```

The installer downloads only taihen, SceShaccCgExt, vitaShaRK, libmathneon, and
vitaGL from the current official VitaSDK package snapshot and overlays their
SDK files into the selected VitaSDK. Pass `-VitaSdk` to target another SDK.
The build script validates the resulting headers and libraries.

Modern vitaGL also requires the decrypted runtime shader compiler at:

```
ur0:/data/libshacccg.suprx
```

The application checks this before initializing vitaGL and displays a readable
error through the bootstrap framebuffer if it is absent.

## Profiling

Add `-Profile` to either build. Aggregated frame, presentation, and blocking
audio-update timings are written every 300 frames to:

```
ux0:data/yabause/profile.log
```

Profiling is disabled by default.

## Build options and retained packages

Run builds from native Windows PowerShell at the repository root. `-Clean`
recreates the build tree but preserves top-level `build-vita\*.vpk` files.
`-VpkName` accepts a filename with or without `.vpk`; an existing name is
rejected unless `-OverwriteVpk` is supplied.

```powershell
Set-Location E:\vita-yabause
.\build-vita.ps1 -Clean -Renderer VitaGL -VpkName yabause.vpk
```

The default BIOS build requires `ux0:data/yabause/bios.bin`. To mount a disc,
place `game.cue` and its referenced BIN tracks under
`ux0:data/yabause/bin`, then build a separate package:

```powershell
.\build-vita.ps1 -Clean -Renderer VitaGL -BootGame Enabled -VpkName yabause-game.vpk
```

Game mode keeps the real BIOS enabled and boots the mounted
`ux0:data/yabause/bin/game.cue` normally. Runtime disc selection and disc
swapping are not implemented yet.

Renderer test switches are:

```text
-Audio Enabled|Disabled
-TextureCache Enabled|Disabled
-BootGame Enabled|Disabled
-AtlasMode Wide|Square|BufferedSquare
-AtlasUpload Dirty|Bands
-Profile
```

`Wide` uses one 2048x1024 atlas. `Square` starts with one 1024x1024 atlas and
lazily allocates a second CPU/GPU page when required, reaching the former Wide
layout's total capacity without allocating the overflow page for ordinary
workloads.
`BufferedSquare` alternates two 1024x1024 GPU atlases while sharing one CPU
backing store. `Square` is the verified default; `Wide` and `BufferedSquare`
remain available for diagnostic comparisons. The startup log records the
compiled mode, dimensions,
texture count, persistent-row boundary, and active buffer so test packages can
be identified unambiguously.

`-AtlasUpload Dirty` is the verified default and transfers exact dirty regions.
`Bands` combines dirty rows into transfer bands while limiting extra uploaded
area to 25% and remains available for hardware A/B comparisons.
