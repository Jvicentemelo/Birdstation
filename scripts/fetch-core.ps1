<#
  Baixa o nucleo Snes9x (libretro) para a pasta cores\.
  Uso:  powershell -ExecutionPolicy Bypass -File scripts\fetch-core.ps1
#>
param(
  [string]$Dest = (Join-Path $PSScriptRoot '..\cores')
)
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$url = 'https://buildbot.libretro.com/nightly/windows/x86_64/latest/snes9x_libretro.dll.zip'
New-Item -ItemType Directory -Force -Path $Dest | Out-Null
$zip = Join-Path ([IO.Path]::GetTempPath()) 'snes9x_libretro.dll.zip'

Write-Host "Baixando Snes9x (libretro) de $url ..."
Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
Expand-Archive -Path $zip -DestinationPath $Dest -Force
Remove-Item $zip -Force

$dll = Join-Path $Dest 'snes9x_libretro.dll'
if (-not (Test-Path $dll)) { throw "snes9x_libretro.dll nao foi encontrado depois de extrair." }
Write-Host "OK: $((Resolve-Path $dll).Path)"
