[CmdletBinding()]
param(
    [string]$VitaSdk
)

$ErrorActionPreference = 'Stop'
$DefaultSdk = 'E:\dev\VitaSDK'
$RequestedSdk = if ($VitaSdk) { $VitaSdk } elseif ($env:VITASDK) { $env:VITASDK } else { $DefaultSdk }
$ResolvedSdk = Resolve-Path -LiteralPath $RequestedSdk -ErrorAction Stop
$SdkRoot = $ResolvedSdk.Path

$Compiler = Join-Path $SdkRoot 'bin\arm-vita-eabi-gcc.exe'
if (-not (Test-Path -LiteralPath $Compiler)) {
    throw "The selected directory is not a native Windows VitaSDK: $SdkRoot"
}

$CurlCommand = Get-Command curl.exe -ErrorAction Stop
$CMakeCommand = Get-Command cmake.exe -ErrorAction Stop
$Packages = @(
    'taihen'
    'SceShaccCgExt'
    'vitaShaRK'
    'libmathneon'
    'vitaGL'
)

Write-Host 'Reading the official VitaSDK package snapshot...' -ForegroundColor Cyan
$Headers = @{ 'User-Agent' = 'vita-yabause-vitagl-installer' }
$Releases = Invoke-RestMethod -Uri 'https://api.github.com/repos/vitasdk/vitasdk-autobuild/releases?per_page=10' -Headers $Headers
$Snapshot = $Releases | Where-Object { -not $_.draft -and $_.tag_name -like 'packages-snapshot-*' } | Select-Object -First 1
if (-not $Snapshot) {
    throw 'No published VitaSDK package snapshot was found.'
}

$TemporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('vita-yabause-vitagl-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $TemporaryRoot | Out-Null

try {
    foreach ($Package in $Packages) {
        $Pattern = '^' + [regex]::Escape($Package) + '-.+-vita\.pkg\.tar\.xz$'
        $Asset = $Snapshot.assets | Where-Object { $_.name -match $Pattern } | Select-Object -First 1
        if (-not $Asset) {
            throw "The package $Package is absent from snapshot $($Snapshot.tag_name)."
        }

        $Archive = Join-Path $TemporaryRoot $Asset.name
        Write-Host "Downloading $($Asset.name)..." -ForegroundColor Cyan
        & $CurlCommand.Source --fail --location --retry 3 --retry-all-errors --connect-timeout 20 --user-agent 'vita-yabause-vitagl-installer' --output $Archive $Asset.browser_download_url
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $Archive)) {
            throw "Could not download $($Asset.name)."
        }

        $DownloadedSize = (Get-Item -LiteralPath $Archive).Length
        if ($DownloadedSize -ne [long]$Asset.size) {
            throw "$($Asset.name) has size $DownloadedSize; GitHub published $($Asset.size). Delete the temporary download and retry."
        }
        if ($Asset.digest -and $Asset.digest -like 'sha256:*') {
            $ExpectedHash = $Asset.digest.Substring(7)
            $DownloadedHash = (Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($DownloadedHash -ne $ExpectedHash.ToLowerInvariant()) {
                throw "$($Asset.name) failed its published SHA-256 check."
            }
        }

        $ExtractRoot = Join-Path $TemporaryRoot ($Package + '-extract')
        New-Item -ItemType Directory -Path $ExtractRoot | Out-Null
        Push-Location $ExtractRoot
        try {
            & $CMakeCommand.Source -E tar xvf $Archive
            if ($LASTEXITCODE -ne 0) {
                throw "CMake could not extract the verified archive $($Asset.name)."
            }
        }
        finally {
            Pop-Location
        }

        $ArmRoot = Get-ChildItem -LiteralPath $ExtractRoot -Directory -Filter 'arm-vita-eabi' -Recurse | Select-Object -First 1
        if (-not $ArmRoot) {
            throw "$($Asset.name) contains no arm-vita-eabi SDK directory."
        }

        Write-Host "Installing $Package into $SdkRoot..." -ForegroundColor Cyan
        Copy-Item -Path (Join-Path $ArmRoot.FullName '*') -Destination (Join-Path $SdkRoot 'arm-vita-eabi') -Recurse -Force
    }
}
finally {
    if (Test-Path -LiteralPath $TemporaryRoot) {
        Remove-Item -LiteralPath $TemporaryRoot -Recurse -Force
    }
}

$Expected = @(
    'arm-vita-eabi\include\vitaGL.h'
    'arm-vita-eabi\include\vitashark.h'
    'arm-vita-eabi\include\shacccg_ext.h'
    'arm-vita-eabi\lib\libvitaGL.a'
    'arm-vita-eabi\lib\libvitashark.a'
    'arm-vita-eabi\lib\libSceShaccCgExt.a'
    'arm-vita-eabi\lib\libmathneon.a'
    'arm-vita-eabi\lib\libtaihen_stub.a'
)
$Missing = @($Expected | Where-Object {
    -not (Test-Path -LiteralPath (Join-Path $SdkRoot $_))
})
if ($Missing.Count -ne 0) {
    throw "Package extraction completed but required files are still missing:$([Environment]::NewLine)$($Missing -join [Environment]::NewLine)"
}

Write-Host "VitaGL dependencies installed from $($Snapshot.tag_name)." -ForegroundColor Green
Write-Host "VitaSDK: $SdkRoot" -ForegroundColor Green
