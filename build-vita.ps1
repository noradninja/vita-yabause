[CmdletBinding()]
param(
    [string]$VitaSdk,
    [string]$Ninja,
    [ValidateSet('VitaGL', 'Software')]
    [string]$Renderer = 'VitaGL',
    [ValidateSet('Enabled', 'Disabled')]
    [string]$Audio = 'Enabled',
    [ValidateSet('Enabled', 'Disabled')]
    [string]$TextureCache = 'Enabled',
    [ValidateSet('Enabled', 'Disabled')]
    [string]$BootGame = 'Disabled',
    [ValidateSet('CopyOnWrite', 'SynchronizedInPlace')]
    [string]$VitaGlTextureUpdates = 'SynchronizedInPlace',
    [ValidateSet('Wide', 'Square', 'BufferedSquare')]
    [string]$AtlasMode = 'Square',
    [ValidateSet('Dirty', 'Bands')]
    [string]$AtlasUpload = 'Dirty',
    [ValidateSet('Enabled', 'Disabled')]
    [string]$AtlasOptimizer = 'Enabled',
    [string]$VpkName = 'yabause.vpk',
    [switch]$OverwriteVpk,
    [switch]$Profile,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$RepositoryRoot = $PSScriptRoot
$BuildDirectory = Join-Path $RepositoryRoot 'build-vita'

function Resolve-VpkName {
    param([string]$RequestedName)

    if ([string]::IsNullOrWhiteSpace($RequestedName) -or
        $RequestedName -eq '.' -or $RequestedName -eq '..') {
        throw '-VpkName must not be empty.'
    }
    if ([System.IO.Path]::GetFileName($RequestedName) -ne $RequestedName -or
        $RequestedName.IndexOfAny([System.IO.Path]::GetInvalidFileNameChars()) -ge 0) {
        throw '-VpkName must be a filename without directory components or invalid characters.'
    }

    $Extension = [System.IO.Path]::GetExtension($RequestedName)
    if ([string]::IsNullOrEmpty($Extension)) {
        $RequestedName += '.vpk'
    }
    elseif ($Extension -ine '.vpk') {
        throw '-VpkName must have a .vpk extension.'
    }
    return $RequestedName
}

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
        'arm-vita-eabi\include\vitashark.h',
        'arm-vita-eabi\include\shacccg_ext.h',
        'arm-vita-eabi\lib\libvitashark.a',
        'arm-vita-eabi\lib\libSceShaccCgExt.a',
        'arm-vita-eabi\lib\libmathneon.a',
        'arm-vita-eabi\lib\libtaihen_stub.a'
    )
    $MissingVitaGl = @(
        $VitaGlRequirements | Where-Object {
            -not (Test-Path -LiteralPath (Join-Path $ResolvedVitaSdk $_))
        }
    )
    if ($MissingVitaGl.Count -ne 0) {
        $MissingList = $MissingVitaGl -join [Environment]::NewLine
        throw "The VitaGL renderer requires current VitaSDK packages vitaShaRK, SceShaccCgExt, libmathneon, and taihen. Missing:$([Environment]::NewLine)$MissingList"
    }
}

$RendererValue = $Renderer.ToLowerInvariant()
$ProfileValue = if ($Profile) { 'ON' } else { 'OFF' }
$AudioValue = if ($Audio -eq 'Enabled') { 'ON' } else { 'OFF' }
$TextureCacheValue = if ($TextureCache -eq 'Enabled') { 'ON' } else { 'OFF' }
$BootGameValue = if ($BootGame -eq 'Enabled') { 'ON' } else { 'OFF' }
$VitaGlTextureUpdatesValue = $VitaGlTextureUpdates.ToLowerInvariant()
$BootMode = if ($BootGame -eq 'Enabled') { 'Game' } else { 'BIOS' }
$AtlasModeValue = $AtlasMode.ToLowerInvariant()
$AtlasUploadValue = $AtlasUpload.ToLowerInvariant()
$AtlasOptimizerValue = if ($AtlasOptimizer -eq 'Enabled') { 'ON' } else { 'OFF' }
$ResolvedVpkName = Resolve-VpkName $VpkName
$Vpk = Join-Path $BuildDirectory $ResolvedVpkName

Write-Host "Vita build configuration: renderer=$Renderer atlas=$AtlasMode atlas-upload=$AtlasUpload atlas-optimizer=$AtlasOptimizer texture-cache=$TextureCache vitaGL-texture-updates=$VitaGlTextureUpdates boot=$BootMode profile=$ProfileValue audio=$Audio output=$ResolvedVpkName" -ForegroundColor Cyan

if ((Test-Path -LiteralPath $Vpk) -and -not $OverwriteVpk) {
    throw "The requested package already exists: $Vpk. Choose another -VpkName or pass -OverwriteVpk."
}

if ($Clean -and (Test-Path -LiteralPath $BuildDirectory)) {
    $ResolvedBuild = (Resolve-Path -LiteralPath $BuildDirectory).Path
    if (-not $ResolvedBuild.StartsWith($RepositoryRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove build directory outside the repository: $ResolvedBuild"
    }
    $PreservedVpks = @(
        Get-ChildItem -LiteralPath $ResolvedBuild -File -Filter '*.vpk' -ErrorAction SilentlyContinue
    )
    $PreserveDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ("vita-yabause-vpks-" + [guid]::NewGuid().ToString('N'))
    try {
        if ($PreservedVpks.Count -ne 0) {
            New-Item -ItemType Directory -Path $PreserveDirectory | Out-Null
            foreach ($Package in $PreservedVpks) {
                Copy-Item -LiteralPath $Package.FullName -Destination $PreserveDirectory
            }
        }
        Remove-Item -LiteralPath $ResolvedBuild -Recurse -Force
        New-Item -ItemType Directory -Path $BuildDirectory | Out-Null
    }
    finally {
        if (Test-Path -LiteralPath $PreserveDirectory) {
            if (-not (Test-Path -LiteralPath $BuildDirectory)) {
                New-Item -ItemType Directory -Path $BuildDirectory | Out-Null
            }
            foreach ($Package in $PreservedVpks) {
                Copy-Item -LiteralPath (Join-Path $PreserveDirectory $Package.Name) -Destination $BuildDirectory -Force
            }
            Remove-Item -LiteralPath $PreserveDirectory -Recurse -Force
        }
    }
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
    "-DVITA_AUDIO_ENABLED=$AudioValue" `
    "-DVITA_TEXTURE_CACHE=$TextureCacheValue" `
    "-DVITA_BOOT_GAME=$BootGameValue" `
    "-DVITA_VGL_TEXTURE_UPDATES=$VitaGlTextureUpdatesValue" `
    "-DVITA_ATLAS_MODE=$AtlasModeValue" `
    "-DVITA_ATLAS_UPLOAD=$AtlasUploadValue" `
    "-DVITA_ATLAS_OPTIMIZER=$AtlasOptimizerValue" `
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

$GeneratedVpk = Join-Path $BuildDirectory 'src\vita\yabause.vpk'
if (Test-Path -LiteralPath $GeneratedVpk) {
    Remove-Item -LiteralPath $GeneratedVpk -Force
}

& $CMake --build $BuildDirectory --target yabause.vpk-vpk
if ($LASTEXITCODE -ne 0) {
    throw "Vita build failed with exit code $LASTEXITCODE."
}

if (Test-Path -LiteralPath $GeneratedVpk) {
    Copy-Item -LiteralPath $GeneratedVpk -Destination $Vpk -Force:$OverwriteVpk
}
if (-not (Test-Path -LiteralPath $Vpk)) {
    throw "The build completed without producing $Vpk."
}

Write-Host "Vita package created: $Vpk ($Renderer renderer, boot: $BootMode, profiling: $ProfileValue, audio: $Audio, texture cache: $TextureCache, atlas: $AtlasMode, atlas upload: $AtlasUpload, atlas optimizer: $AtlasOptimizer, vitaGL texture updates: $VitaGlTextureUpdates)" -ForegroundColor Green
