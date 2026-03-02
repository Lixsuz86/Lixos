param(
    [string]$Output = "$PSScriptRoot\lixos.zip"
)

Write-Host "Creating package: $Output"
if (Test-Path $Output) { Remove-Item $Output -Force }

Compress-Archive -Path "$PSScriptRoot\*" -DestinationPath $Output -Force

if (Test-Path $Output) {
    Write-Host "Package created: $Output"
} else {
    Write-Host "Failed to create package"
    exit 1
}
