param(
    [string]$Version = '1.0.1.21',
    [string]$Configuration = 'Debug',
    [string]$BasePackage,
    [string]$SdkBin,
    [string]$InnoCompiler,
    [switch]$SkipInstaller
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Output = Join-Path $Root 'Output'
if (-not $BasePackage) {
    $BasePackage = Join-Path $Output 'NanaZip.LocalDebug.Full.msix'
}
if (-not $SdkBin) {
    $SdkBin = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin\10.0.26100.0\x64'
}
if (-not $InnoCompiler) {
    $InnoCompiler = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'
}
if ($Version -notmatch '^\d+\.\d+\.\d+\.\d+$') {
    throw 'Version must have four numeric components.'
}
$Package = Join-Path $Output "NanaZip.LocalDebug.Full.$Version.msix"
$Installer = Join-Path $Output "NanaZip-$Version-Setup.exe"
if ((Test-Path -LiteralPath $Package) -or (Test-Path -LiteralPath $Installer)) {
    throw 'This version already exists. Choose a higher version to preserve prior artifacts.'
}
$MakeAppx = Join-Path $SdkBin 'makeappx.exe'
$SignTool = Join-Path $SdkBin 'signtool.exe'
$Binaries = Join-Path $Output "Binaries\$Configuration\x64"
$PublicCertificate = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new(
    (Join-Path $PSScriptRoot 'NanaZip.LocalDebug.cer'))
$Certificate = Get-ChildItem Cert:\CurrentUser\My | Where-Object {
    $_.Thumbprint -eq $PublicCertificate.Thumbprint -and $_.HasPrivateKey
} | Select-Object -First 1
if (-not $Certificate) {
    throw 'The existing installer signing certificate private key is unavailable.'
}
$Replacements = @('NanaZip.ShellExtension.dll', 'NanaZip.Modern.FileManager.exe')
$RequiredInputs = @($BasePackage, $MakeAppx, $SignTool)
if (-not $SkipInstaller) { $RequiredInputs += $InnoCompiler }
foreach ($Path in $RequiredInputs) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required build input is missing: $Path"
    }
}
foreach ($Name in $Replacements) {
    if (-not (Test-Path -LiteralPath (Join-Path $Binaries $Name))) {
        throw "Build the updated binary first: $Name"
    }
}

$Stage = Join-Path $Output ('InstallerBuild-' + $Version + '-' + [guid]::NewGuid().ToString('N'))
$Layout = Join-Path $Stage 'Layout'
New-Item -ItemType Directory -Path $Stage | Out-Null
& $MakeAppx unpack /p $BasePackage /d $Layout /o
if ($LASTEXITCODE -ne 0) { throw 'Unable to unpack the base MSIX.' }

$ManifestPath = Join-Path $Layout 'AppxManifest.xml'
$Manifest = [System.Xml.XmlDocument]::new()
$Manifest.PreserveWhitespace = $true
$Manifest.Load($ManifestPath)
$Identity = $Manifest.DocumentElement.SelectSingleNode('*[local-name()="Identity"]')
if ($Identity.GetAttribute('Name') -ne 'NanaZip.LocalDebug.ContextMenu' -or
    $Identity.GetAttribute('Publisher') -ne $Certificate.Subject -or
    $Identity.GetAttribute('ProcessorArchitecture') -ne 'x64') {
    throw 'The base package identity does not match the existing installer.'
}
if ([version]$Version -le [version]$Identity.GetAttribute('Version')) {
    throw 'The installer version must be newer than the base package.'
}
$Identity.SetAttribute('Version', $Version)
$Manifest.Save($ManifestPath)
foreach ($Name in $Replacements) {
    Copy-Item -LiteralPath (Join-Path $Binaries $Name) -Destination (Join-Path $Layout $Name)
}
# Package metadata must be regenerated after changing the payload.
foreach ($Name in @('AppxBlockMap.xml', 'AppxSignature.p7x', 'AppxMetadata\CodeIntegrity.cat')) {
    $Metadata = Join-Path $Layout $Name
    if (Test-Path -LiteralPath $Metadata) {
        Remove-Item -LiteralPath $Metadata
    }
}
& $MakeAppx pack /d $Layout /p $Package /o /h SHA256
if ($LASTEXITCODE -ne 0) { throw 'Unable to create the updated MSIX.' }
& $SignTool sign /fd SHA256 /sha1 $Certificate.Thumbprint /s My $Package
if ($LASTEXITCODE -ne 0) { throw 'Unable to sign the updated MSIX.' }

$Signature = Get-AuthenticodeSignature -LiteralPath $Package
if (-not $Signature.SignerCertificate -or
    $Signature.SignerCertificate.Thumbprint -ne $PublicCertificate.Thumbprint) {
    throw 'The updated package was not signed with the original installer certificate.'
}
$Artifacts = @($Package)
if (-not $SkipInstaller) {
    & $InnoCompiler "/DAppVersion=$Version" "/DPayloadDir=$Layout" "/DPackagePath=$Package" `
        (Join-Path $PSScriptRoot 'NanaZip.iss')
    if ($LASTEXITCODE -ne 0) { throw 'Unable to compile the installer.' }
    $Artifacts += $Installer
}

$Hashes = Get-FileHash -Algorithm SHA256 -LiteralPath $Artifacts
$Hashes | Select-Object Path, Hash | ConvertTo-Json |
    Set-Content -LiteralPath (Join-Path $Stage 'ArtifactHashes.json') -Encoding utf8
[pscustomobject]@{
    Version = $Version
    Installer = $(if ($SkipInstaller) { $null } else { $Installer })
    Package = $Package
    Layout = $Layout
    CertificateThumbprint = $PublicCertificate.Thumbprint
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Stage 'BuildResult.json') -Encoding utf8
if (-not $SkipInstaller) { Write-Output "Built installer: $Installer" }
Write-Output "Built package: $Package"
