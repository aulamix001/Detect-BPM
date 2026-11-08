[CmdletBinding()]
param(
    [string]$ValidatorPath = "",
    [switch]$BuildOnly
)
$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
if (-not [Environment]::Is64BitOperatingSystem -or $env:OS -ne "Windows_NT") {
    throw "Execute no Windows x64 com Visual Studio 2022 (Desktop development with C++) e CMake >= 3.24."
}
$root = Split-Path -Parent $PSScriptRoot
foreach ($tool in @("cmake", "ctest", "git")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "Ferramenta ausente: $tool. Use Developer PowerShell for VS 2022 com CMake e Git instalados."
    }
}
Push-Location $root
try {
    & cmake -S . -B build/windows -G "Visual Studio 17 2022" -A x64 -DSMART_TIME_BUILD_PLUGIN=ON -DBUILD_TESTING=ON
    if ($LASTEXITCODE -ne 0) { throw "Falha na configuracao CMake" }
    & cmake --build build/windows --config Release --parallel 4
    if ($LASTEXITCODE -ne 0) { throw "Falha na compilacao" }
    & ctest --test-dir build/windows -C Release --output-on-failure --no-tests=error
    if ($LASTEXITCODE -ne 0) { throw "Testes falharam; nao distribuir" }
    $bundle = Join-Path $root "build/windows/SmartTime_artefacts/Release/VST3/SMART TIME.vst3"
    $module = Join-Path $bundle "Contents/x86_64-win/SMART TIME.vst3"
    if (-not (Test-Path $module)) { throw "Modulo Windows x64 nao encontrado: $module" }
    if ($BuildOnly) {
        Write-Host "VST3 candidato gerado: $bundle"
        Write-Warning "Nao validado pelo validator.exe nem aprovado no Studio One. Nenhuma instalacao global foi realizada."
        return
    }
    if (-not $ValidatorPath -or -not (Test-Path $ValidatorPath)) {
        throw "Build candidato em '$bundle'. Falta executar validator.exe do VST3 SDK. Execute novamente com -ValidatorPath; nenhum pacote aprovado foi criado."
    }
    & $ValidatorPath $bundle
    if ($LASTEXITCODE -ne 0) { throw "Validacao VST3 falhou; nao distribuir" }
    New-Item -ItemType Directory -Force dist | Out-Null
    $package = Join-Path $root "dist/CHi-SMART-TIME-0.1.0-Windows-x64-candidate.zip"
    if (Test-Path $package) { Remove-Item $package }
    Compress-Archive -LiteralPath $bundle -DestinationPath $package
    Write-Host "Candidato: $package"
    Write-Host "Ainda exige escuta e testes no Studio One. Nenhuma instalacao global foi realizada."
} finally { Pop-Location }
