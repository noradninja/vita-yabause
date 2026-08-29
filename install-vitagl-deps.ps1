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

$TarCommand = Get-Command tar.exe -ErrorAction Stop
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
        Invoke-WebRequest -Uri $Asset.browser_download_url -OutFile $Archive -Headers $Headers

        $Entries = @(& $TarCommand.Source -tf $Archive)
        if ($LASTEXITCODE -ne 0) {
            throw "Could not inspect $($Asset.name)."
        }
        $SdkEntry = $Entries | Where-Object { $_ -match '(^|/)arm-vita-eabi/' } | Select-Object -First 1
        if (-not $SdkEntry) {
            throw "$($Asset.name) contains no arm-vita-eabi SDK files."
        }

        $NormalizedEntry = $SdkEntry -replace '\\', '/'
        $MarkerIndex = $NormalizedEntry.IndexOf('arm-vita-eabi/')
        $Prefix = $NormalizedEntry.Substring(0, $MarkerIndex).Trim('/')
        $StripComponents = if ($Prefix) { ($Prefix -split '/').Count } else { 0 }

        Write-Host "Installing $Package into $SdkRoot..." -ForegroundColor Cyan
        & $TarCommand.Source -xf $Archive -C $SdkRoot "--strip-components=$StripComponents"
        if ($LASTEXITCODE -ne 0) {
            throw "Could not install $($Asset.name)."
        }
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
