# Decompile every indexed GB.EXE function with Ghidra (headless), applying symbols.csv.
# Output: reverse_engineering/out/decomp/gb.c, gb_ds.c (DS globals renamed), gb_globals_xref.txt.
# Needs: _tools/ghidra_12.0.4_PUBLIC (or $env:GHIDRA_HOME) and JDK 21+ (or $env:JAVA_HOME).
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$re = Join-Path $root 'reverse_engineering'
$ghidra = if ($env:GHIDRA_HOME) { $env:GHIDRA_HOME } else { Join-Path $root '_tools\ghidra_12.0.4_PUBLIC' }
if (-not $env:JAVA_HOME) {
    $jdk = Get-ChildItem 'C:\Program Files\Java' -Directory -Filter 'jdk-*' -ErrorAction SilentlyContinue |
        Where-Object { [int]($_.Name -replace '^jdk-(\d+).*$', '$1') -ge 21 } | Select-Object -Last 1
    if (-not $jdk) { throw 'Set JAVA_HOME to a JDK 21 or later.' }
    $env:JAVA_HOME = $jdk.FullName
}
$dgroup = '1B73'
New-Item -ItemType Directory -Force (Join-Path $re 'out\ghidra'), (Join-Path $re 'out\decomp') | Out-Null
& python (Join-Path $re 'tools\symbols.py')
if ($LASTEXITCODE -ne 0) { throw 'symbols.csv has errors.' }
& (Join-Path $ghidra 'support\analyzeHeadless.bat') (Join-Path $re 'out\ghidra') GB `
    -import (Join-Path $re 'out\GB_unp.exe') -overwrite -scriptPath (Join-Path $re 'tools\ghidra') `
    -preScript SetDS.java $dgroup `
    -postScript FixNearFlows.java $dgroup `
    -postScript ApplySymbols.java (Join-Path $re 'out\symbols_ghidra.txt') $dgroup `
    -postScript DecompileAll.java (Join-Path $re 'map\gb_starts.txt') (Join-Path $re 'out\decomp\gb.c') $dgroup 120 only
if ($LASTEXITCODE -ne 0) { throw 'Ghidra headless failed.' }
& python (Join-Path $re 'tools\ghidra\postprocess.py') (Join-Path $re 'out\decomp\gb.c') $dgroup
