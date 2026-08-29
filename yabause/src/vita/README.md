# PS Vita video backends

The native PowerShell build supports two presentation backends:

```powershell
.\build-vita.ps1 -Renderer VitaGL
.\build-vita.ps1 -Renderer Software
```

`VitaGL` is the default. It currently uploads the completed VIDSoft framebuffer
to a reusable GPU texture and uses vitaGL for point-filtered presentation. The
software build retains the direct CDRAM presenter for A/B testing.

Both paths select the largest uniform integer scale that fits 960x544 and center
the result over opaque black. Examples are 320x224 to 640x448, 352x240 to
704x480, and 704x480 at 1x. Neither axis is stretched independently.

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
