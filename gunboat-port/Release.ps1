# Builds the release package: release\Gunboat-<version>-win64.zip with gunboat.exe and the controller
# mapping tool gunboat_controller.exe (stripped), SDL3.dll, an empty Game folder with its README, the
# player README and the licenses. No game files: the player adds their own to Game. The sound effects
# editor is a development tool and is not in the package. Run Build.ps1 first (it builds and tests the
# port).
$ErrorActionPreference = 'Stop'
$toolchain = 'C:\msys64\ucrt64\bin'
if (Test-Path -LiteralPath $toolchain) { $env:PATH = "$toolchain;$env:PATH" }
$source = $PSScriptRoot
$build = Join-Path $source 'build'
& cmake --build $build --target gunboat gunboat_controller
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }

$exe = Join-Path $build 'gunboat.exe'
$version = (& $exe --version).Split(' ')[-1].Trim()
$name = "Gunboat-$version-win64"
$out = Join-Path $source 'release'
$stage = Join-Path $out $name
$top = Join-Path $stage 'Gunboat'
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Force (Join-Path $top 'Game') | Out-Null
New-Item -ItemType Directory -Force (Join-Path $top 'licenses') | Out-Null

& strip -o (Join-Path $top 'gunboat.exe') $exe
if ($LASTEXITCODE -ne 0) { throw 'strip failed.' }
& strip -o (Join-Path $top 'gunboat_controller.exe') (Join-Path $build 'gunboat_controller.exe')
if ($LASTEXITCODE -ne 0) { throw 'strip failed.' }
Copy-Item -LiteralPath (Join-Path $build 'SDL3.dll') -Destination $top
# The text files with Windows line ends.
function Copy-Text($from, $to) {
    $text = (Get-Content -LiteralPath $from -Raw).Replace('@VERSION@', $version) -replace "`r?`n", "`r`n"
    Set-Content -LiteralPath $to -Value $text -Encoding ascii -NoNewline
}
Copy-Text (Join-Path $source 'dist\README.txt') (Join-Path $top 'README.txt')
Copy-Text (Join-Path $source 'dist\Game\README.txt') (Join-Path $top 'Game\README.txt')
Copy-Item -LiteralPath (Join-Path $source 'THIRD_PARTY.md') -Destination $top
Copy-Item -LiteralPath (Join-Path $source 'licenses\test-drive-3-sdl3-MIT.txt') -Destination (Join-Path $top 'licenses')
Copy-Item -LiteralPath (Join-Path $source 'vendor\nuked-opl3\LICENSE') -Destination (Join-Path $top 'licenses\Nuked-OPL3-LGPL-2.1.txt')
Copy-Item -LiteralPath (Join-Path $source 'deps\SDL3-3.4.16\LICENSE.txt') -Destination (Join-Path $top 'licenses\SDL3-zlib.txt')
Copy-Item -LiteralPath (Join-Path $source 'vendor\imgui\LICENSE.txt') -Destination (Join-Path $top 'licenses\Dear-ImGui-MIT.txt')

# No game file may be in the package.
$game = Get-ChildItem -LiteralPath (Join-Path $top 'Game') -File | Where-Object { $_.Name -ne 'README.txt' }
if ($game) { throw "Game folder not empty: $($game.Name -join ', ')" }

$zip = Join-Path $out "$name.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
# (Compress-Archive of PowerShell 5.1 writes '\' into the entry names; the zip format wants '/'.)
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::Open($zip, 'Create')
try {
    Get-ChildItem -LiteralPath $top -Recurse -File | ForEach-Object {
        $entry = 'Gunboat/' + $_.FullName.Substring($top.Length + 1).Replace('\', '/')
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $_.FullName, $entry, 'Optimal') | Out-Null
    }
} finally {
    $archive.Dispose()
}
Write-Host "Release package: $zip"
Get-ChildItem -LiteralPath $top -Recurse -File | ForEach-Object { '{0,10}  {1}' -f $_.Length, $_.FullName.Substring($stage.Length + 1) }
