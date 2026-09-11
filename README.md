# Yabause - A Saturn Emulator

## PS Vita VitaGL branch

The `vita/vitagl` branch contains the current PS Vita port. Its adapted YGL
renderer uses vitaGL for VDP1 rasterization, VDP2 drawing, and final
composition. Projective VDP1 textures, point sampling, Gouraud color,
command-ordered half-transparency, and native 480x272 presentation are working
through the Saturn BIOS. Decoded VDP textures are stored in an incrementally
updated atlas, with optional persistent VDP2 caching.

BIOS rendering is the current compatibility target. Full game compatibility,
remaining Saturn edge cases, broader renderer verification, and SH2 execution
performance are still in development. The port currently uses the SH2
interpreter and Q68; no SH2 dynamic recompiler is enabled.

Place an uncompressed 512 KiB Saturn BIOS at:

```text
ux0:data/yabause/bios.bin
```

Game-test builds mount this fixed CUE path:

```text
ux0:data/yabause/bin/game.cue
```

Keep the BIN track files referenced by the CUE in the same directory. Game
mode still uses the real BIOS and lets it boot the mounted disc normally.

Initialize the pinned private vitaGL source and install its support
dependencies into VitaSDK:

```powershell
Set-Location E:\vita-yabause
git submodule update --init --recursive
.\install-vitagl-deps.ps1
```

The project builds and links its own pinned vitaGL archive; it does not replace
the SDK-wide `libvitaGL.a`. The dependency installer supplies vitaShaRK,
SceShaccCgExt, libmathneon, and taihen.


The Vita also requires the decrypted shader compiler at
`ur0:/data/libshacccg.suprx`.

From native Windows PowerShell, build the default VitaGL package with:

```powershell
Set-Location E:\vita-yabause
.\build-vita.ps1 -Clean -Renderer VitaGL
```

The script uses `VITASDK` when set and otherwise checks
`E:\dev\VitaSDK`. Use `-VitaSdk` or `-Ninja` to provide alternate tool paths,
or `-Clean` to recreate the build tree. Existing top-level VPK files in
`build-vita` are preserved by clean builds. The default output is
`build-vita\yabause.vpk` with title ID `YABA00001`.

Useful build switches include:

- `-Audio Enabled|Disabled` (default `Enabled`)
- `-Profile` to write aggregated timings to
  `ux0:data/yabause/profile.log`
- `-TextureCache Enabled|Disabled` (default `Enabled`)
- `-BootGame Enabled|Disabled` (default `Disabled`); enabled builds mount
  `ux0:data/yabause/bin/game.cue`
- `-AtlasMode Wide|Square|BufferedSquare` (default `Square`)
- `-AtlasUpload Dirty|Bands` (default `Dirty`)
- `-VitaGlTextureUpdates CopyOnWrite|SynchronizedInPlace` (default `SynchronizedInPlace`)
- `-Vdp2Worker Enabled|Disabled` (default `Disabled`) for BIOS RBG0 decode A/B tests
- `-VpkName <filename>` to retain separately named packages
- `-OverwriteVpk` to intentionally replace an existing named package

Create separately named BIOS and game-test packages without deleting the
preceding output:

```powershell
.\build-vita.ps1 -Clean -Renderer VitaGL -BootGame Disabled -VpkName yabause-bios.vpk
.\build-vita.ps1 -Clean -Renderer VitaGL -BootGame Enabled -VpkName yabause-game.vpk
```

For BIOS VDP2 worker comparisons:

```powershell
.\build-vita.ps1 -Clean -Renderer VitaGL -Profile -Audio Disabled -BootGame Disabled -TextureCache Enabled -AtlasMode Square -AtlasUpload Dirty -DirectAtlasUpload Enabled -DirectAtlasMode Bounding -Vdp2Worker Disabled -VpkName yabause-bios-vdp2-single.vpk
.\build-vita.ps1 -Clean -Renderer VitaGL -Profile -Audio Disabled -BootGame Disabled -TextureCache Enabled -AtlasMode Square -AtlasUpload Dirty -DirectAtlasUpload Enabled -DirectAtlasMode Bounding -Vdp2Worker Enabled -VpkName yabause-bios-vdp2-worker.vpk
```

The worker is local to the VitaGL rotation decoder. It does not enable
Yabause's general threading mode, and it never performs atlas allocation,
cache publication, or GPU calls.

For atlas comparisons:

```powershell
.\build-vita.ps1 -Clean -Renderer VitaGL -Profile -Audio Disabled -TextureCache Enabled -AtlasMode Wide -VpkName yabause-wide.vpk
.\build-vita.ps1 -Clean -Renderer VitaGL -Profile -Audio Disabled -TextureCache Enabled -AtlasMode Square -VpkName yabause-square.vpk
.\build-vita.ps1 -Clean -Renderer VitaGL -Profile -Audio Disabled -TextureCache Enabled -AtlasMode BufferedSquare -VpkName yabause-buffered-square.vpk
```

`Square` uses a 1024x1024 primary atlas and lazily allocates a second 1024x1024
page when required, reaching the former Wide layout's capacity without paying
for the overflow page during ordinary workloads. The verified default `Dirty`
uploader transfers exact dirty regions.
`Bands` remains available for transfer-coalescing comparisons.
`SynchronizedInPlace` uses the private vitaGL build's in-place texture updates
and waits once per upload epoch when an atlas is still in flight. This avoids
vitaGL's full-atlas copy-on-write allocation. `CopyOnWrite` remains available
for diagnostic comparison.

The software renderer remains available with `-Renderer Software` for
reference captures and renderer comparisons. More Vita-specific details are in
[`yabause/src/vita/README.md`](yabause/src/vita/README.md).

Controller mapping:

| PS Vita | Sega Saturn |
| --- | --- |
| D-pad | D-pad |
| Square | A |
| Cross | B |
| Circle | C |
| Triangle | Y |
| L / R | L / R |
| Start | Start |
| Select | Unused |

[Documentation](http://wiki.yabause.org/index.php5?title=Documentations) | [Homepage](https://yabause.org/) | [Development Builds](https://yabause.org/download/)

[![Travis CI Build Status](https://travis-ci.org/Yabause/yabause.svg?branch=master)](https://travis-ci.org/Yabause/yabause)
[![Appveyor Build status](https://ci.appveyor.com/api/projects/status/n35d1obw5deo1dsl/branch/master?svg=true)](https://ci.appveyor.com/project/Guillaumito/yabause)
[![CircleCI Build Status](https://circleci.com/gh/Yabause/yabause/tree/master.svg?style=shield&circle-token=c3153fb8a4e9d5a8801604ce5cac566c5ea16774)](https://circleci.com/gh/Yabause/yabause)
[![Coverity Scan Status](https://scan.coverity.com/projects/6271/badge.svg)](https://scan.coverity.com/projects/6271)
