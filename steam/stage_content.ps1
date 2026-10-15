# stage_content.ps1 - assemble steam/content/{win64,linux64} for upload.
#
# Usage (repo root):
#   powershell -File steam\stage_content.ps1 [-LinuxTar <path-to-linux64.tar.gz>]
#
# win64 comes from dist\exit_interview (fresh local Release build).
# linux64 comes from the CI artifact exit-interview-linux64.tar.gz -
# grab it from the latest GitHub Actions "build" run or a tag Release.

param([string]$LinuxTar = ".\exit-interview-linux64.tar.gz")

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root

$win = "steam\content\win64"
$lin = "steam\content\linux64"
Remove-Item -Recurse -Force $win, $lin -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $win, $lin | Out-Null

Write-Host "win64 <- dist\exit_interview"
Copy-Item -Recurse dist\exit_interview\* $win\

Write-Host "linux64 <- $LinuxTar"
if (!(Test-Path $LinuxTar)) { throw "missing $LinuxTar - download the CI artifact first" }
tar -xzf $LinuxTar -C $lin
# exec bits do not survive Windows; they are restored at upload time -
# run the linux depot upload under Linux steamcmd (see README.md)

Write-Host "`nStaged:"
Get-ChildItem -Recurse -File steam\content | Group-Object { $_.Directory.Parent.Name } |
    ForEach-Object { "  $($_.Name): $($_.Count) files" }
Pop-Location
