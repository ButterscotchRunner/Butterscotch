param(
    [string]$ProjectRoot = ((Resolve-Path (Join-Path $PSScriptRoot '..')).ProviderPath),
    [string]$ExePath,
    [string]$Sdl3Path,
    [string]$AppName = 'Butterscotch',
    [string]$Publisher = 'CN=ButterscotchDev',
    [string]$Version = '1.0.0.0',
    [string]$OutputRoot = ((Resolve-Path (Join-Path $PSScriptRoot '..')).ProviderPath + '\Packaging\ManualAppx'),
    [switch]$SkipSign,
    [string]$PfxPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-RepoPath {
    param(
        [string]$Path,
        [string]$ProjectRootOverride
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return $null
    }

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return (Resolve-Path $Path -ErrorAction Stop).ProviderPath
    }

    $rootCandidates = @(
        $ProjectRootOverride,
        (Get-Location).Path
    )

    foreach ($root in $rootCandidates | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }) {
        $candidate = Join-Path $root $Path
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate -ErrorAction Stop).ProviderPath
        }
    }

    return (Resolve-Path $Path -ErrorAction Stop).ProviderPath
}

Write-Host "Project root: $ProjectRoot"
Write-Host "Output root: $OutputRoot"

if ([string]::IsNullOrWhiteSpace($ExePath)) {
    $ExePath = Join-Path $ProjectRoot 'build-uwp\Debug\butterscotch.exe'
} else {
    $ExePath = Resolve-RepoPath -Path $ExePath -ProjectRootOverride $ProjectRoot
}

if ([string]::IsNullOrWhiteSpace($Sdl3Path)) {
    $Sdl3Path = Join-Path $ProjectRoot 'build-uwp\Debug\SDL3.dll'
} else {
    $Sdl3Path = Resolve-RepoPath -Path $Sdl3Path -ProjectRootOverride $ProjectRoot
}

$dataWinPath = Join-Path $ProjectRoot 'data.win'
$gameDbPath = Join-Path $ProjectRoot 'vendor\gamecontrollerdb.txt'

foreach ($p in @($ExePath, $Sdl3Path, $dataWinPath, $gameDbPath)) {
    if (-not (Test-Path $p)) {
        throw "Required file not found: $p"
    }
}

$stagingDir = Join-Path $OutputRoot 'Staging'
$workingDir = Join-Path $stagingDir 'Butterscotch.appx'
$assetsDir = Join-Path $workingDir 'Assets'
$manifestPath = Join-Path $workingDir 'AppxManifest.xml'
$packagePath = Join-Path $OutputRoot 'Butterscotch.appx'

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
New-Item -ItemType Directory -Force -Path $stagingDir | Out-Null
New-Item -ItemType Directory -Force -Path $workingDir | Out-Null
New-Item -ItemType Directory -Force -Path $assetsDir | Out-Null

Write-Host "Creating placeholder app assets..."
Add-Type -AssemblyName System.Drawing
foreach ($spec in @(
    @{ Name = 'Square150x150Logo.png'; Width = 150; Height = 150 },
    @{ Name = 'Square44x44Logo.png'; Width = 44; Height = 44 },
    @{ Name = 'StoreLogo.png'; Width = 50; Height = 50 }
)) {
    $bmp = New-Object System.Drawing.Bitmap($spec.Width, $spec.Height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::FromArgb(255, 33, 33, 33))
    $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 255, 255, 255)), 2
    $g.DrawRectangle($pen, 1, 1, $spec.Width - 3, $spec.Height - 3)
    $g.DrawLine($pen, 0, 0, $spec.Width, $spec.Height)
    $g.DrawLine($pen, $spec.Width, 0, 0, $spec.Height)
    $g.Dispose()
    $bmp.Save((Join-Path $assetsDir $spec.Name), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

Write-Host "Copying runtime files..."
Copy-Item $ExePath (Join-Path $workingDir 'butterscotch.exe') -Force
Copy-Item $Sdl3Path (Join-Path $workingDir 'SDL3.dll') -Force
Copy-Item $dataWinPath (Join-Path $workingDir 'data.win') -Force
Copy-Item $gameDbPath (Join-Path $workingDir 'gamecontrollerdb.txt') -Force

@'
<?xml version="1.0" encoding="utf-8"?>
<Package
  xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10"
  xmlns:uap="http://schemas.microsoft.com/appx/manifest/uap/windows10"
  xmlns:rescap="http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities"
  IgnorableNamespaces="uap rescap">

  <Identity
    Name="Butterscotch"
    Publisher="CN=ButterscotchDev"
    Version="1.0.0.0" />

  <Properties>
    <DisplayName>Butterscotch</DisplayName>
    <PublisherDisplayName>Butterscotch</PublisherDisplayName>
    <Logo>Assets\StoreLogo.png</Logo>
  </Properties>

  <Dependencies>
    <TargetDeviceFamily Name="Windows.Universal" MinVersion="10.0.19041.0" MaxVersionTested="10.0.26100.0" />
  </Dependencies>

  <Resources>
    <Resource Language="en-US" />
  </Resources>

  <Capabilities>
    <rescap:Capability Name="runFullTrust" />
  </Capabilities>

  <Applications>
    <Application Id="App"
      Executable="butterscotch.exe"
      EntryPoint="Windows.FullTrustApplication">
      <uap:VisualElements
        DisplayName="Butterscotch"
        Description="Butterscotch"
        Square150x150Logo="Assets\Square150x150Logo.png"
        Square44x44Logo="Assets\Square44x44Logo.png"
        BackgroundColor="transparent" />
    </Application>
  </Applications>
</Package>
'@ | Set-Content -Path $manifestPath -Encoding UTF8

$makeAppx = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' -Recurse -Filter makeappx.exe -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending |
    Select-Object -First 1

if (-not $makeAppx) {
    throw "makeappx.exe not found. Install the Windows 10/11 SDK and ensure the kits bin directory is present."
}

$makeAppxPath = $makeAppx.FullName
$packageOutputPath = Join-Path $OutputRoot 'Butterscotch.appx'

if (Test-Path $packageOutputPath) {
    Remove-Item $packageOutputPath -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Packaging with makeappx: $makeAppxPath"
& $makeAppxPath pack /d $workingDir /p $packageOutputPath /nc /o
if ($LASTEXITCODE -ne 0) {
    throw "makeappx failed."
}

if (-not $SkipSign) {
    $signtool = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending |
        Select-Object -First 1

    if (-not $signtool) {
        throw "signtool.exe not found. Install the Windows SDK." 
    }

    if (-not $PfxPath) {
        $certPath = Join-Path $OutputRoot 'Butterscotch.pfx'
        $certCerPath = Join-Path $OutputRoot 'Butterscotch.cer'
        $makeCert = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' -Recurse -Filter makecert.exe -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending |
            Select-Object -First 1

        if (-not $makeCert) {
            throw "No certificate provided and no makecert.exe found. Pass -PfxPath or use -SkipSign to create an unsigned appx."
        }

        $certPassword = 'Butterscotch123!'
        $pvkPath = Join-Path $OutputRoot 'Butterscotch.pvk'
        $cerTempPath = Join-Path $OutputRoot 'Butterscotch-temp.cer'

        & $makeCert.FullName -r -pe -n "CN=ButterscotchDev" -ss My -sr CurrentUser -a SHA256 -sky exchange -sp "Microsoft RSA SChannel Cryptographic Provider" -sy 12 -sv $pvkPath $cerTempPath
        if ($LASTEXITCODE -ne 0) {
            throw "makecert failed."
        }

        & 'C:\Program Files (x86)\Windows Kits\10\bin\x64\pvk2pfx.exe' -pvk $pvkPath -pi $certPassword -spc $cerTempPath -pfx $certPath -po $certPassword
        if ($LASTEXITCODE -ne 0) {
            throw "pvk2pfx failed."
        }

        Copy-Item $cerTempPath $certCerPath -Force
        $PfxPath = $certPath
    }

    $signtoolPath = $signtool.FullName
    & $signtoolPath sign /fd SHA256 /f $PfxPath /p 'Butterscotch123!' $packageOutputPath
    if ($LASTEXITCODE -ne 0) {
        throw "signtool failed."
    }
}

Write-Host "Package created: $packageOutputPath"
