param(
    [Parameter(Mandatory = $true)][string]$ResultPath,
    [Parameter(Mandatory = $true)][string]$ExpectedVersion
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

function Get-NanaZipShiftRight([long]$Value, [int]$Count) {
    if ($Value -band 0x80000000) {
        return (($Value -shr $Count) -bxor 0xFFFF0000)
    }
    return ($Value -shr $Count)
}

function Get-NanaZipLong([byte[]]$Bytes, [int]$Index) {
    return [BitConverter]::ToInt32($Bytes, $Index)
}

function Convert-NanaZipInt32([long]$Value) {
    return [BitConverter]::ToInt32([BitConverter]::GetBytes($Value), 0)
}

function Get-NanaZipUserChoiceHash([string]$BaseInfo) {
    $bytesBaseInfo = [System.Text.Encoding]::Unicode.GetBytes($BaseInfo)
    $bytesBaseInfo += 0x00, 0x00
    $md5 = [System.Security.Cryptography.MD5]::Create()
    $bytesMD5 = $md5.ComputeHash($bytesBaseInfo)
    $lengthBase = ($BaseInfo.Length * 2) + 2
    $length = (($lengthBase -band 4) -le 1) + (Get-NanaZipShiftRight $lengthBase 2) - 1
    if ($length -le 1) {
        throw 'association hash length is invalid'
    }

    $map = @{
        PDATA = 0; CACHE = 0; COUNTER = 0; INDEX = 0; MD51 = 0; MD52 = 0
        OUTHASH1 = 0; OUTHASH2 = 0; R0 = 0; R1 = @(0, 0); R2 = @(0, 0)
        R3 = 0; R4 = @(0, 0); R5 = @(0, 0); R6 = @(0, 0); R7 = @(0, 0)
    }
    $map.MD51 = ((Get-NanaZipLong $bytesMD5 0) -bor 1) + 0x69FB0000L
    $map.MD52 = ((Get-NanaZipLong $bytesMD5 4) -bor 1) + 0x13DB0000L
    $map.INDEX = Get-NanaZipShiftRight ($length - 2) 1
    $map.COUNTER = $map.INDEX + 1
    while ($map.COUNTER) {
        $map.R0 = Convert-NanaZipInt32 ((Get-NanaZipLong $bytesBaseInfo $map.PDATA) + [long]$map.OUTHASH1)
        $map.R1[0] = Convert-NanaZipInt32 (Get-NanaZipLong $bytesBaseInfo ($map.PDATA + 4))
        $map.PDATA = $map.PDATA + 8
        $map.R2[0] = Convert-NanaZipInt32 (($map.R0 * ([long]$map.MD51)) - (0x10FA9605L * ((Get-NanaZipShiftRight $map.R0 16))))
        $map.R2[1] = Convert-NanaZipInt32 ((0x79F8A395L * ([long]$map.R2[0])) + (0x689B6B9FL * (Get-NanaZipShiftRight $map.R2[0] 16)))
        $map.R3 = Convert-NanaZipInt32 ((0xEA970001L * $map.R2[1]) - (0x3C101569L * (Get-NanaZipShiftRight $map.R2[1] 16)))
        $map.R4[0] = Convert-NanaZipInt32 ($map.R3 + $map.R1[0])
        $map.R5[0] = Convert-NanaZipInt32 ($map.CACHE + $map.R3)
        $map.R6[0] = Convert-NanaZipInt32 (($map.R4[0] * [long]$map.MD52) - (0x3CE8EC25L * (Get-NanaZipShiftRight $map.R4[0] 16)))
        $map.R6[1] = Convert-NanaZipInt32 ((0x59C3AF2DL * $map.R6[0]) - (0x2232E0F1L * (Get-NanaZipShiftRight $map.R6[0] 16)))
        $map.OUTHASH1 = Convert-NanaZipInt32 ((0x1EC90001L * $map.R6[1]) + (0x35BD1EC9L * (Get-NanaZipShiftRight $map.R6[1] 16)))
        $map.OUTHASH2 = Convert-NanaZipInt32 ([long]$map.R5[0] + [long]$map.OUTHASH1)
        $map.CACHE = ([long]$map.OUTHASH2)
        $map.COUNTER = $map.COUNTER - 1
    }

    $outHash = New-Object byte[] 16
    [BitConverter]::GetBytes([int]$map.OUTHASH1).CopyTo($outHash, 0)
    [BitConverter]::GetBytes([int]$map.OUTHASH2).CopyTo($outHash, 4)

    $map.CACHE = 0
    $map.OUTHASH1 = 0
    $map.OUTHASH2 = 0
    $map.PDATA = 0
    $map.MD51 = (Get-NanaZipLong $bytesMD5 0) -bor 1
    $map.MD52 = (Get-NanaZipLong $bytesMD5 4) -bor 1
    $map.INDEX = Get-NanaZipShiftRight ($length - 2) 1
    $map.COUNTER = $map.INDEX + 1
    while ($map.COUNTER) {
        $map.R0 = Convert-NanaZipInt32 ((Get-NanaZipLong $bytesBaseInfo $map.PDATA) + ([long]$map.OUTHASH1))
        $map.PDATA = $map.PDATA + 8
        $map.R1[0] = Convert-NanaZipInt32 ($map.R0 * [long]$map.MD51)
        $map.R1[1] = Convert-NanaZipInt32 ((0xB1110000L * $map.R1[0]) - (0x30674EEFL * (Get-NanaZipShiftRight $map.R1[0] 16)))
        $map.R2[0] = Convert-NanaZipInt32 ((0x5B9F0000L * $map.R1[1]) - (0x78F7A461L * (Get-NanaZipShiftRight $map.R1[1] 16)))
        $map.R2[1] = Convert-NanaZipInt32 ((0x12CEB96DL * (Get-NanaZipShiftRight $map.R2[0] 16)) - (0x46930000L * $map.R2[0]))
        $map.R3 = Convert-NanaZipInt32 ((0x1D830000L * $map.R2[1]) + (0x257E1D83L * (Get-NanaZipShiftRight $map.R2[1] 16)))
        $map.R4[0] = Convert-NanaZipInt32 ([long]$map.MD52 * ([long]$map.R3 + (Get-NanaZipLong $bytesBaseInfo ($map.PDATA - 4))))
        $map.R4[1] = Convert-NanaZipInt32 ((0x16F50000L * $map.R4[0]) - (0x5D8BE90BL * (Get-NanaZipShiftRight $map.R4[0] 16)))
        $map.R5[0] = Convert-NanaZipInt32 ((0x96FF0000L * $map.R4[1]) - (0x2C7C6901L * (Get-NanaZipShiftRight $map.R4[1] 16)))
        $map.R5[1] = Convert-NanaZipInt32 ((0x2B890000L * $map.R5[0]) + (0x7C932B89L * (Get-NanaZipShiftRight $map.R5[0] 16)))
        $map.OUTHASH1 = Convert-NanaZipInt32 ((0x9F690000L * $map.R5[1]) - (0x405B6097L * (Get-NanaZipShiftRight ($map.R5[1]) 16)))
        $map.OUTHASH2 = Convert-NanaZipInt32 ([long]$map.OUTHASH1 + $map.CACHE + $map.R3)
        $map.CACHE = ([long]$map.OUTHASH2)
        $map.COUNTER = $map.COUNTER - 1
    }

    [BitConverter]::GetBytes([int]$map.OUTHASH1).CopyTo($outHash, 8)
    [BitConverter]::GetBytes([int]$map.OUTHASH2).CopyTo($outHash, 12)
    $hashValue1 = (Get-NanaZipLong $outHash 8) -bxor (Get-NanaZipLong $outHash 0)
    $hashValue2 = (Get-NanaZipLong $outHash 12) -bxor (Get-NanaZipLong $outHash 4)
    $outHashBase = New-Object byte[] 8
    [BitConverter]::GetBytes([int]$hashValue1).CopyTo($outHashBase, 0)
    [BitConverter]::GetBytes([int]$hashValue2).CopyTo($outHashBase, 4)
    return [Convert]::ToBase64String($outHashBase)
}

function Get-NanaZipUserExperience {
    $fallback = 'User Choice set via Windows User Experience {D18B6DD5-6124-4341-9318-804003BAFA0B}'
    $needle = 'User Choice set via Windows User Experience'
    $shell32 = Join-Path $env:SystemRoot 'SysWOW64\shell32.dll'
    if (-not (Test-Path $shell32)) {
        $shell32 = Join-Path $env:SystemRoot 'System32\shell32.dll'
    }
    $stream = [System.IO.File]::Open($shell32, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try {
        $reader = New-Object System.IO.BinaryReader($stream)
        $bytes = $reader.ReadBytes(5MB)
        $text = [Text.Encoding]::Unicode.GetString($bytes)
        $start = $text.IndexOf($needle)
        if ($start -lt 0) { return $fallback }
        $end = $text.IndexOf('}', $start)
        if ($end -lt $start) { return $fallback }
        return $text.Substring($start, $end - $start + 1)
    }
    finally {
        $stream.Close()
    }
}

function Remove-NanaZipUserChoice([string]$Extension) {
    $code = @'
using System;
using System.Runtime.InteropServices;
namespace NanaZipAssoc {
    public class Reg {
        [DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern int RegDeleteKey(System.UIntPtr hKey, string subKey);
        public static void Delete(string key) {
            RegDeleteKey((System.UIntPtr)0x80000001u, key);
        }
    }
}
'@
    if (-not ('NanaZipAssoc.Reg' -as [type])) {
        Add-Type -TypeDefinition $code
    }
    $key = "Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\$Extension\UserChoice"
    [NanaZipAssoc.Reg]::Delete($key) | Out-Null
}

function Set-NanaZipDefaultAssociations {
    $pkg = Get-AppxPackage -Name 'NanaZip.LocalDebug.ContextMenu'
    $root = "HKCU:\Software\Classes\Local Settings\Software\Microsoft\Windows\CurrentVersion\AppModel\Repository\Packages\$($pkg.PackageFullName)"
    $keys = @(Get-ChildItem $root -Recurse -ErrorAction SilentlyContinue | Where-Object { $_.PSChildName -eq 'FileAssociations' })
    if ($keys.Count -eq 0) {
        throw 'package registered no file associations'
    }
    $map = @{}
    foreach ($key in $keys) {
        $props = Get-ItemProperty $key.PSPath
        foreach ($prop in $props.PSObject.Properties) {
            if ($prop.Name -like '.*') {
                $map[$prop.Name.ToLower()] = [string]$prop.Value
            }
        }
    }
    $extensions = @('.zip', '.zipx', '.rar', '.7z', '.tar', '.gz', '.tgz', '.bz2', '.xz', '.iso', '.cab')
    $sid = [System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value.ToLower()
    $experience = Get-NanaZipUserExperience
    $now = [DateTime]::Now
    $stamp = [DateTime]::New($now.Year, $now.Month, $now.Day, $now.Hour, $now.Minute, 0).ToFileTime()
    $dateHex = (($stamp -shr 32).ToString('X8') + ($stamp -band 0xFFFFFFFFL).ToString('X8')).ToLower()
    foreach ($extension in $extensions) {
        if (-not $map.ContainsKey($extension)) {
            throw "missing association for $extension"
        }
        $progId = $map[$extension]
        $baseInfo = "$extension$sid$progId$dateHex$experience".ToLower()
        $hash = Get-NanaZipUserChoiceHash $baseInfo
        [Microsoft.Win32.Registry]::SetValue(
            'HKEY_CURRENT_USER\SOFTWARE\Microsoft\Windows\CurrentVersion\ApplicationAssociationToasts',
            ($progId + '_' + $extension), 0, [Microsoft.Win32.RegistryValueKind]::DWord)
        Remove-NanaZipUserChoice $extension
        $choice = "HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\$extension\UserChoice"
        [Microsoft.Win32.Registry]::SetValue($choice, 'Hash', $hash)
        [Microsoft.Win32.Registry]::SetValue($choice, 'ProgId', $progId)
    }
}

try {
    if (Test-Path $ResultPath) {
        Remove-Item $ResultPath -Force
    }
    $cer = Join-Path $PSScriptRoot 'NanaZip.LocalDebug.cer'
    $cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($cer)
    Add-PublicCert -StoreName 'TrustedPeople' -StoreLocation 'CurrentUser' -Certificate $cert

    $msix = Join-Path $PSScriptRoot 'NanaZip.LocalDebug.Full.msix'
    try {
        Add-AppxPackage -Path $msix -ForceUpdateFromAnyVersion -ForceApplicationShutdown
    }
    catch {
        $blocked = $_.Exception.Message -match '0x80073CFB'
        if (-not $blocked) { throw }
        Get-AppxPackage -Name 'NanaZip.LocalDebug.ContextMenu' | Remove-AppxPackage
        Add-AppxPackage -Path $msix -ForceUpdateFromAnyVersion -ForceApplicationShutdown
    }
    $pkg = Get-AppxPackage -Name 'NanaZip.LocalDebug.ContextMenu'
    if (-not $pkg) {
        throw 'Get-AppxPackage did not find NanaZip.LocalDebug.ContextMenu'
    }
    if ([string]$pkg.Version -ne $ExpectedVersion) {
        throw "installed version is $($pkg.Version), expected $ExpectedVersion"
    }
    Set-NanaZipDefaultAssociations
    Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue
    Write-Result "OK $($pkg.Version) $($pkg.PackageFamilyName)"
    exit 0
}
catch {
    $code = 1
    try { $code = $_.Exception.HResult } catch { $code = 1 }
    $hex = '0x{0:X8}' -f ($code -band 0xFFFFFFFF)
    $message = ($_.Exception.Message -replace '[\r\n]+', ' ')
    Write-Result "FAIL $hex $message"
    exit 1
}
