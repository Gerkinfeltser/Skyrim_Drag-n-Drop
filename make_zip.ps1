$scriptDir = $PSScriptRoot
$tempDir = Join-Path $env:TEMP 'DragAndDrop_zip'
$destZip = Join-Path $scriptDir 'DragAndDrop_v0-x.zip'

Remove-Item $tempDir -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $destZip -Force -ErrorAction SilentlyContinue

New-Item -ItemType Directory -Path $tempDir | Out-Null

Copy-Item (Join-Path $scriptDir 'DragAndDrop.esp') $tempDir
Copy-Item -Recurse (Join-Path $scriptDir 'SEQ') $tempDir
New-Item -ItemType Directory -Path (Join-Path $tempDir 'SKSE\Plugins') | Out-Null
Copy-Item (Join-Path $scriptDir 'SKSE\Plugins\DragAndDrop.dll') (Join-Path $tempDir 'SKSE\Plugins\DragAndDrop.dll')
Copy-Item (Join-Path $scriptDir 'SKSE\Plugins\DragAndDrop.ini') (Join-Path $tempDir 'SKSE\Plugins\DragAndDrop.ini')
Copy-Item -Recurse (Join-Path $scriptDir 'scripts') $tempDir
Copy-Item -Recurse (Join-Path $scriptDir 'Source') $tempDir
Copy-Item (Join-Path $scriptDir 'README.md') (Join-Path $tempDir 'Drag-n-Drop-README.md')

Compress-Archive -Path "$tempDir\*" -DestinationPath $destZip

Remove-Item $tempDir -Recurse -Force

Write-Host "Created: $destZip"
Get-Item $destZip
