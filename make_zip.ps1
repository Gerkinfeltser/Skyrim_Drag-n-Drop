$scriptDir = $PSScriptRoot
$tempDir = Join-Path $env:TEMP 'Drag-n-Drop_zip'
$destZip = Join-Path $scriptDir 'Drag-n-Drop-v0-x.zip'

Remove-Item $tempDir -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $destZip -Force -ErrorAction SilentlyContinue

New-Item -ItemType Directory -Path $tempDir | Out-Null

Copy-Item (Join-Path $scriptDir 'DragAndDrop.esp') $tempDir
Copy-Item -Recurse (Join-Path $scriptDir 'scripts') $tempDir
Copy-Item -Recurse (Join-Path $scriptDir 'SKSE\Plugins') $tempDir
Copy-Item -Recurse (Join-Path $scriptDir 'Source') $tempDir
Copy-Item (Join-Path $scriptDir 'README.md') (Join-Path $tempDir 'Drag-n-Drop-README.md')

Compress-Archive -Path "$tempDir\*" -DestinationPath $destZip

Remove-Item $tempDir -Recurse -Force

Write-Host "Created: $destZip"
Get-Item $destZip
