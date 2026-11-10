param(
    [Parameter(Mandatory = $true)][string]$ResultPath
)

$ErrorActionPreference = 'Stop'

function Write-Result([string]$Text) {
    $dir = Split-Path -Parent $ResultPath
    if ($dir -and -not (Test-Path $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
    Set-Content -Path $ResultPath -Value $Text -Encoding ASCII
}

function Add-PublicCert([string]$StoreName, [string]$StoreLocation, $Certificate) {
    $store = New-Object System.Security.Cryptography.X509Certificates.X509Store($StoreName, $StoreLocation)
    $store.Open([System.Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
    try {
        $found = $store.Certificates.Find(
            [System.Security.Cryptography.X509Certificates.X509FindType]::FindByThumbprint,
            $Certificate.Thumbprint,
            $false)
        if ($found.Count -eq 0) {
            $store.Add($Certificate)
        }
    }
    finally {
        $store.Close()
    }
}

try {
    if (Test-Path $ResultPath) {
        Remove-Item $ResultPath -Force
    }
    $cer = Join-Path $PSScriptRoot 'NanaZip.LocalDebug.cer'
    $cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($cer)
    Add-PublicCert -StoreName 'TrustedPeople' -StoreLocation 'LocalMachine' -Certificate $cert

    $unlock = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock'
    if (-not (Test-Path $unlock)) {
        New-Item -Path $unlock -Force | Out-Null
    }
    New-ItemProperty -Path $unlock -Name 'AllowAllTrustedApps' -PropertyType DWord -Value 1 -Force | Out-Null
    New-ItemProperty -Path $unlock -Name 'AllowDevelopmentWithoutDevLicense' -PropertyType DWord -Value 1 -Force | Out-Null

    Write-Result 'OK'
    exit 0
}
catch {
    $code = $_.Exception.HResult
    if (-not $code) { $code = 1 }
    $hex = '0x{0:X8}' -f ([uint32]$code)
    $message = ($_.Exception.Message -replace '[\r\n]+', ' ')
    Write-Result "FAIL $hex $message"
    exit 1
}
