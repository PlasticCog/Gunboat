$ErrorActionPreference = 'Stop'
$toolchain = 'C:\msys64\ucrt64\bin'
if (Test-Path -LiteralPath $toolchain) { $env:PATH = "$toolchain;$env:PATH" }
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw 'CMake is required. This workspace was built with MSYS2 UCRT64 GCC, CMake and Ninja.'
}
$source = $PSScriptRoot
$build = Join-Path $source 'build'
& cmake -S $source -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& cmake --build $build
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
& ctest --test-dir $build --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Native checks failed.' }
Write-Host 'Ready. Run Port.cmd starts the game (build\gunboat.exe); Run Gunboat.cmd starts the Codex prototype.'
