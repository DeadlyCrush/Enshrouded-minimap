param(
  [string]$GameDir = "C:\Program Files (x86)\Steam\steamapps\common\Enshrouded",
  [string]$Configuration = "Release",
  [string]$Platform = "x64",
  [switch]$SkipBuild
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
if (Get-Process enshrouded -ErrorAction SilentlyContinue) { throw 'Close Enshrouded before installing.' }
if (-not (Test-Path -LiteralPath (Join-Path $GameDir 'enshrouded.exe'))) { throw 'Game executable missing.' }
if (-not (Test-Path -LiteralPath (Join-Path $GameDir 'shroudtopia.dll'))) { throw 'Shroudtopia is required.' }
if (-not $SkipBuild) {
  & (Join-Path $root 'build-release.ps1') -Configuration $Configuration -Platform $Platform
  if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
}
$sourceDll = Join-Path $root "build\$Platform\$Configuration\mods\minimap_mod.dll"
if (-not (Test-Path -LiteralPath $sourceDll)) { throw 'Built minimap_mod.dll missing.' }
$targetDir = Join-Path $GameDir 'mods\minimap_mod'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
# Loader discovery must never scan a backup DLL.
$backupDir = Join-Path $GameDir "minimap-backups\$stamp"
if (Test-Path -LiteralPath $backupDir) { throw 'Backup destination already exists.' }
New-Item -ItemType Directory -Path $backupDir | Out-Null
if (Test-Path -LiteralPath $targetDir) {
  Copy-Item -LiteralPath $targetDir -Destination (Join-Path $backupDir 'minimap_mod') -Recurse
  foreach ($file in Get-ChildItem -LiteralPath $targetDir -Recurse -File) {
    $relative = $file.FullName.Substring($targetDir.Length + 1)
    if ((Get-FileHash -LiteralPath $file.FullName).Hash -ne
        (Get-FileHash -LiteralPath (Join-Path $backupDir "minimap_mod\$relative")).Hash) { throw "Backup hash mismatch: $relative" }
  }
} else { 'minimap_mod was absent' | Set-Content (Join-Path $backupDir 'previous-install-absent.txt') }
if (Test-Path -LiteralPath (Join-Path $GameDir 'shroudtopia.json')) {
  Copy-Item -LiteralPath (Join-Path $GameDir 'shroudtopia.json') -Destination $backupDir
}
New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
Copy-Item -LiteralPath $sourceDll -Destination (Join-Path $targetDir 'minimap_mod.dll') -Force
foreach ($asset in Get-ChildItem -LiteralPath (Join-Path $root 'assets')) {
  Copy-Item -LiteralPath $asset.FullName -Destination $targetDir -Recurse -Force
}
if ((Get-FileHash -LiteralPath $sourceDll).Hash -ne
    (Get-FileHash -LiteralPath (Join-Path $targetDir 'minimap_mod.dll')).Hash) { throw 'Installed DLL hash mismatch.' }
Write-Output "Installed: $targetDir"
Write-Output "Backup: $backupDir"
Write-Output 'Existing shroudtopia.json was preserved. DLL installation is not gameplay validation.'
