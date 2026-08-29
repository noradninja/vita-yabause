[CmdletBinding()]
param(
    [string]$VitaSdk,
    [string]$Ninja,
    [ValidateSet('VitaGL', 'Software')]
    [string]$Renderer = 'VitaGL',
    [switch]$Profile,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$RepositoryRoot = $PSScriptRoot
$BuildDirectory = Join-Path $RepositoryRoot 'build-vita'

function Resolve-VitaSdk {
    param([string]$RequestedPath)

    $Candidates = @($RequestedPath, $env:VITASDK, 'E:\dev\VitaSDK') | Where-Object { $_ }
    foreach ($Candidate in $Candidates) {
        $Resolved = Resolve-Path -LiteralPath $Candidate -ErrorAction SilentlyContinue
        if ($Resolved) {
            $Required = @(
                'bin\arm-vita-eabi-gcc.exe',
                'share\vita.toolchain.cmake',
                'share\vita.cmake',
                'bin\vita-pack-vpk.exe',
                'bin\vita-mksfoex.exe'
            )
            $Complete = $true
            foreach ($Item in $Required) {
                if (-not (Test-Path -LiteralPath (Join-Path $Resolved.Path $Item))) {
                    $Complete = $false
                    break
                }
            }
            if ($Complete) {
                return $Resolved.Path
            }
        }
    }
    throw 'A complete native Windows VitaSDK was not found. Pass -VitaSdk or set VITASDK.'
}

function Resolve-Ninja {
    param([string]$RequestedPath)

    $Candidates = @($RequestedPath)
    $PathCommand = Get-Command ninja.exe -ErrorAction SilentlyContinue
    if ($PathCommand) {
        $Candidates += $PathCommand.Source
    }
    $Candidates += 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'

    foreach ($Candidate in ($Candidates | Where-Object { $_ })) {
        $Resolved = Resolve-Path -LiteralPath $Candidate -ErrorAction SilentlyContinue
        if ($Resolved) {
            return $Resolved.Path
        }
    }
    throw 'ninja.exe was not found. Pass its full path with -Ninja.'
}

$ResolvedVitaSdk = Resolve-VitaSdk $VitaSdk
$ResolvedNinja = Resolve-Ninja $Ninja
$CMake = (Get-Command cmake.exe -ErrorAction Stop).Source

if ($Renderer -eq 'VitaGL') {
    $VitaGlRequirements = @(
        'arm-vita-eabi\include\vitaGL.h',
        'arm-vita-eabi\include\vitashark.h',
        'arm-vita-eabi\include\shacccg_ext.h',
        'arm-vita-eabi\lib\libvitaGL.a',
        'arm-vita-eabi\lib\libvitashark.a',
        'arm-vita-eabi\lib\libSceShaccCgExt.a',
        'arm-vita-eabi\lib\libmathneon.a',
        'arm-vita-eabi\lib\libzip.a',
        'arm-vita-eabi\lib\libz.a'
    )
    $MissingVitaGl = @(
        $VitaGlRequirements | Where-Object {
            -not (Test-Path -LiteralPath (Join-Path $ResolvedVitaSdk $_))
        }
    )
    if ($MissingVitaGl.Count -ne 0) {
        $MissingList = $MissingVitaGl -join [Environment]::NewLine
        throw "The VitaGL renderer requires current VitaSDK packages vitaGL, vitaShaRK, SceShaccCgExt, libmathneon, and taihen. Missing:$([Environment]::NewLine)$MissingList"
    }
}

$RendererValue = $Renderer.ToLowerInvariant()
$ProfileValue = if ($Profile) { 'ON' } else { 'OFF' }

if ($Clean -and (Test-Path -LiteralPath $BuildDirectory)) {
    $ResolvedBuild = (Resolve-Path -LiteralPath $BuildDirectory).Path
    if (-not $ResolvedBuild.StartsWith($RepositoryRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove build directory outside the repository: $ResolvedBuild"
    }
    Remove-Item -LiteralPath $ResolvedBuild -Recurse -Force
}

$env:VITASDK = $ResolvedVitaSdk
$Toolchain = Join-Path $ResolvedVitaSdk 'share\vita.toolchain.cmake'

& $CMake `
    -S (Join-Path $RepositoryRoot 'yabause') `
    -B $BuildDirectory `
    -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$ResolvedNinja" `
    "-DCMAKE_TOOLCHAIN_FILE=$Toolchain" `
    '-DCMAKE_BUILD_TYPE=Release' `
    '-DYAB_PORTS=vita' `
    '-DYAB_WANT_OPENGL=OFF' `
    "-DVITA_VIDEO_BACKEND=$RendererValue" `
    "-DVITA_PROFILE=$ProfileValue" `
    '-DYAB_WANT_OPENAL=OFF' `
    '-DYAB_WANT_MUSASHI=OFF' `
    '-DYAB_WANT_C68K=OFF' `
    '-DYAB_WANT_Q68=ON' `
    '-DYAB_USE_PLAY_JIT=OFF' `
    '-DSH2_DYNAREC=OFF' `
    '-DYAB_NETWORK=OFF' `
    '-DYAB_TESTS=OFF' `
    '-DYAB_USE_SCSP2=OFF' `
    '-DYAB_USE_SCSPMIDI=OFF'

if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed with exit code $LASTEXITCODE."
}

& $CMake --build $BuildDirectory --target yabause.vpk-vpk
if ($LASTEXITCODE -ne 0) {
    throw "Vita build failed with exit code $LASTEXITCODE."
}

$GeneratedVpk = Join-Path $BuildDirectory 'src\vita\yabause.vpk'
$Vpk = Join-Path $BuildDirectory 'yabause.vpk'
if (Test-Path -LiteralPath $GeneratedVpk) {
    Copy-Item -LiteralPath $GeneratedVpk -Destination $Vpk -Force
}
if (-not (Test-Path -LiteralPath $Vpk)) {
    throw "The build completed without producing $Vpk."
}

Write-Host "Vita package created: $Vpk ($Renderer renderer, profiling: $ProfileValue)" -ForegroundColor Green
