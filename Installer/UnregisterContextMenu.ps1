$ErrorActionPreference = 'Continue'
Get-AppxPackage -Name 'NanaZip.LocalDebug.ContextMenu' | Remove-AppxPackage
Get-AppxPackage -AllUsers -Name 'NanaZip.LocalDebug.ContextMenu' | Remove-AppxPackage -AllUsers
Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue
