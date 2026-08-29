# Yabause - A Saturn Emulator

## PS Vita bootstrap

The `vita/bootstrap` branch contains an initial BIOS-only PS Vita port. It uses
the SH2 interpreter, Q68, the software video renderer, and native Vita display,
audio, and controller APIs. It does not include a BIOS, game loader, vitaGL, or
a dynamic recompiler.

Place an uncompressed 512 KiB Saturn BIOS at:

```text
ux0:data/yabause/bios.bin
```

From native Windows PowerShell, build the VPK with:

```powershell
Set-Location E:\vita-yabause
.\build-vita.ps1
```

The script uses `VITASDK` when set and otherwise checks
`E:\dev\VitaSDK`. Use `-VitaSdk` or `-Ninja` to provide alternate tool paths,
or `-Clean` to recreate only the `build-vita` output directory. The resulting
package is `build-vita\yabause.vpk` with title ID `YABA00001`.

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
