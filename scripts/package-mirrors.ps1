param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^bmclapi-[A-Za-z0-9][A-Za-z0-9._-]*$')]
    [string]$ReleaseTag
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$workspace = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location $workspace
$dist = Join-Path $workspace 'dist'
New-Item -ItemType Directory -Path $dist -Force | Out-Null

$installRoot = Join-Path $workspace 'install'
cmake --install build --config Release --prefix $installRoot
if ($LASTEXITCODE -ne 0) { throw 'CMake install failed.' }

$packageName = "PrismLauncher-BMCLAPI-Windows-x64-$ReleaseTag"
$packageRoot = Join-Path $dist $packageName
Copy-Item -LiteralPath $installRoot -Destination $packageRoot -Recurse
cmake --install build --config Release --prefix $packageRoot --component portable
if ($LASTEXITCODE -ne 0) { throw 'CMake portable install failed.' }

# CMake's Qt deployment intentionally omits the MSVC compiler runtime.
$crtDir = Join-Path $env:VCToolsRedistDir 'x64\Microsoft.VC143.CRT'
if (-not (Test-Path -LiteralPath $crtDir -PathType Container)) {
    throw "MSVC runtime directory is missing: $crtDir"
}
Copy-Item -Path (Join-Path $crtDir '*.dll') -Destination $packageRoot
foreach ($required in @('vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $packageRoot $required) -PathType Leaf)) {
        throw "MSVC runtime file is missing: $required"
    }
}

Copy-Item -LiteralPath (Join-Path $workspace 'LICENSE') -Destination (Join-Path $packageRoot 'LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $workspace 'docs/BMCLAPIRelease.md') -Destination (Join-Path $packageRoot 'README-BMCLAPI.md')
$licenses = Join-Path $packageRoot 'licenses'
New-Item -ItemType Directory -Path $licenses -Force | Out-Null
$vcpkgShare = Join-Path $workspace 'build/vcpkg_installed/x64-windows/share'
if (Test-Path -LiteralPath $vcpkgShare -PathType Container) {
    Get-ChildItem -LiteralPath $vcpkgShare -Directory | ForEach-Object {
        $copyright = Join-Path $_.FullName 'copyright'
        if (Test-Path -LiteralPath $copyright -PathType Leaf) {
            Copy-Item -LiteralPath $copyright -Destination (Join-Path $licenses "$($_.Name).txt")
        }
    }
}
if ($env:QT_ROOT_DIR) {
    $qtLicenses = Join-Path $env:QT_ROOT_DIR 'LICENSES'
    if (Test-Path -LiteralPath $qtLicenses -PathType Container) {
        Copy-Item -LiteralPath $qtLicenses -Destination (Join-Path $licenses 'Qt') -Recurse
    }
}

$exe = Join-Path $packageRoot 'prismlauncher.exe'
foreach ($requiredPath in @($exe, (Join-Path $packageRoot 'portable.txt'),
                            (Join-Path $packageRoot 'platforms/qwindows.dll'),
                            (Join-Path $packageRoot 'jars'))) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Portable package is incomplete: $requiredPath"
    }
}

# --version exits during command-line parsing; the data directory is isolated.
$smokeDir = Join-Path $env:RUNNER_TEMP "prism-bmclapi-smoke-$ReleaseTag"
New-Item -ItemType Directory -Path $smokeDir -Force | Out-Null
$startInfo = [System.Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = $exe
$startInfo.WorkingDirectory = $packageRoot
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.Environment['PATH'] = "$packageRoot;$env:SystemRoot\System32;$env:SystemRoot"
foreach ($key in @('QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QML2_IMPORT_PATH', 'QML_IMPORT_PATH')) {
    [void]$startInfo.Environment.Remove($key)
}
$startInfo.ArgumentList.Add('--dir')
$startInfo.ArgumentList.Add($smokeDir)
$startInfo.ArgumentList.Add('--version')
$process = [System.Diagnostics.Process]::Start($startInfo)
try {
    if (-not $process.WaitForExit(15000)) {
        $process.Kill($true)
        throw 'Packaged launcher --version timed out.'
    }
    if ($process.ExitCode -ne 0) {
        throw "Packaged launcher --version failed with exit code $($process.ExitCode)."
    }
} finally {
    $process.Dispose()
}

$archive = Join-Path $dist "$packageName.zip"
Compress-Archive -LiteralPath $packageRoot -DestinationPath $archive -CompressionLevel Optimal
if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) { throw 'Portable ZIP was not created.' }

$patch = Join-Path $dist "PrismLauncher-BMCLAPI-$ReleaseTag.patch"
git diff --binary --output="$patch" 11.1.1 HEAD
if ($LASTEXITCODE -ne 0) { throw 'Could not generate the source patch.' }
if ((Get-Item -LiteralPath $patch).Length -eq 0) { throw 'Source patch is empty.' }
$baseCommit = (git rev-parse '11.1.1^{commit}').Trim()
$builtCommit = (git rev-parse HEAD).Trim()
@"
This is a community fork of Prism Launcher 11.1.1 with configurable Minecraft download sources.
Built commit: $builtCommit
Upstream base tag: 11.1.1
Upstream base commit: $baseCommit
Repository: https://github.com/$env:GITHUB_REPOSITORY

To reconstruct the source, check out PrismLauncher/PrismLauncher at tag 11.1.1,
apply PrismLauncher-BMCLAPI-$ReleaseTag.patch with git apply, then run
git submodule update --init --recursive. The patch includes this fork's build workflow.
"@ | Set-Content -LiteralPath (Join-Path $dist 'SOURCE.txt') -Encoding utf8

$checksums = @($archive, $patch) | ForEach-Object {
    $hash = Get-FileHash -LiteralPath $_ -Algorithm SHA256
    "$($hash.Hash.ToLowerInvariant())  $(Split-Path -Leaf $_)"
}
$checksums | Set-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt') -Encoding ascii
@"
Windows x64 portable build of a community Prism Launcher 11.1.1 fork. This is not an official Prism Launcher release. The executable is unsigned.

解压 ZIP，运行 prismlauncher.exe。在「设置 → 服务 → Minecraft Downloads」选择 Official、BMCLAPI，或填写兼容 BMCLAPI 的自定义 HTTPS 地址。镜像下载失败时会尝试官方地址。

The ZIP includes the launcher, bundled runtime files, portable mode marker, project license, and dependency license material. SHA256SUMS.txt verifies the ZIP and source patch. SOURCE.txt identifies the exact upstream base and built commit.
"@ | Set-Content -LiteralPath (Join-Path $dist 'RELEASE_NOTES.md') -Encoding utf8
