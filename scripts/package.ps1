<#
  Compila o Birdstation (Release) e monta o pacote portatil:
      dist\Birdstation-<versao>-win64.zip
  O zip contem a pasta Birdstation\ pronta para rodar apos descompactar.

  Requisitos: Visual Studio 2022 (carga "Desenvolvimento para desktop com C++") e CMake 3.21+.
  Se faltarem, o script oferece instalar com o winget.
  Uso:  powershell -ExecutionPolicy Bypass -File scripts\package.ps1 [-Version 1.0.0] [-SkipBuild]
#>
param(
  [string]$Version = '1.0.0',
  [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$root  = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$build = Join-Path $root 'build'

function Update-SessionPath {
  $env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [Environment]::GetEnvironmentVariable('Path','User')
}

function Find-CMake {
  $c = Get-Command cmake -ErrorAction SilentlyContinue
  if ($c) { return $c.Source }
  $cands = @(
    "$env:ProgramFiles\CMake\bin\cmake.exe",
    "${env:ProgramFiles(x86)}\CMake\bin\cmake.exe"
  )
  $cands += Get-ChildItem "$env:ProgramFiles\Microsoft Visual Studio\*\*\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -ErrorAction SilentlyContinue | ForEach-Object FullName
  $cands += Get-ChildItem "${env:ProgramFiles(x86)}\Microsoft Visual Studio\*\*\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -ErrorAction SilentlyContinue | ForEach-Object FullName
  foreach ($p in $cands) { if ($p -and (Test-Path $p)) { return $p } }
  return $null
}

function Test-VCTools {
  $vw = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  if (-not (Test-Path $vw)) { return $false }
  $p = & $vw -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  return [bool]$p
}

function Install-WithWinget($id, $extra) {
  if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    throw "winget nao encontrado. Instale manualmente: $id (veja o README)."
  }
  winget install --id $id -e --accept-package-agreements --accept-source-agreements @extra
}

if (-not $SkipBuild) {
  $cmake = Find-CMake
  if (-not $cmake) {
    Write-Host 'CMake nao encontrado.'
    if ((Read-Host 'Instalar o CMake agora com o winget? (S/N)') -match '^[sSyY]') {
      Install-WithWinget 'Kitware.CMake' @()
      Update-SessionPath
      $cmake = Find-CMake
    }
    if (-not $cmake) { throw 'CMake e necessario. Instale em https://cmake.org/download/ (marque "Add to PATH") e rode de novo.' }
  }
  if (-not (Test-VCTools)) {
    Write-Host 'Ferramentas C++ do Visual Studio nao encontradas.'
    if ((Read-Host 'Instalar as Build Tools do Visual Studio 2022 (C++), ~3 GB, com o winget? (S/N)') -match '^[sSyY]') {
      Install-WithWinget 'Microsoft.VisualStudio.2022.BuildTools' @('--override','--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended')
    }
    if (-not (Test-VCTools)) { throw 'Instale o Visual Studio 2022 (ou Build Tools) com a carga "Desenvolvimento para desktop com C++" e rode de novo.' }
  }
  Write-Host "Usando CMake: $cmake"
  & $cmake -S $root -B $build -A x64
  if ($LASTEXITCODE) { throw 'Falha ao configurar o CMake.' }
  & $cmake --build $build --config Release --parallel
  if ($LASTEXITCODE) { throw 'Falha ao compilar.' }
}

$exe = Join-Path $build 'Release\Birdstation.exe'
if (-not (Test-Path $exe)) { throw "Birdstation.exe nao encontrado em $exe" }

$dist  = Join-Path $root 'dist'
$stage = Join-Path $dist 'Birdstation'
if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
foreach ($d in 'cores','roms','covers','assets') { New-Item -ItemType Directory -Force -Path (Join-Path $stage $d) | Out-Null }

Copy-Item $exe $stage
Copy-Item (Join-Path $root 'assets\birdstation.png') (Join-Path $stage 'assets')
Copy-Item (Join-Path $root 'LICENSE') $stage
Copy-Item (Join-Path $root 'THIRD_PARTY_NOTICES.md') $stage
Copy-Item (Join-Path $root 'README.md') $stage

@"
Coloque suas ROMs de SNES (.sfc, .smc, .swc, .fig) nesta pasta (subpastas funcionam).
Depois abra o Birdstation.exe e clique em "Baixar capas" para buscar as imagens de capa.
Para usar uma capa propria, salve-a em covers\ com o MESMO nome da ROM (ex.: covers\Meu Jogo.png)
ou arraste a imagem para a janela com o jogo selecionado.
"@ | Set-Content -Encoding UTF8 (Join-Path $stage 'roms\LEIAME.txt')

& (Join-Path $PSScriptRoot 'fetch-core.ps1') -Dest (Join-Path $stage 'cores')

$zip = Join-Path $dist "Birdstation-$Version-win64.zip"
Compress-Archive -Path $stage -DestinationPath $zip -Force
Write-Host ""
Write-Host "Pacote pronto: $zip"
