param(
    [switch]$SetupOnly,
    [switch]$Stop,
    [switch]$RebuildIso,
    [switch]$Smoke,
    [string]$DebugUdpHost
)

$ErrorActionPreference = 'Stop'

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$XemuRoot = 'C:\Games\Emulators\Xemu'
$RuntimeSource = 'C:\Games\Emulators\CXBX\UT99x'
$BuildRoot = Join-Path $RepoRoot 'UT99-Xbox\build_cli'
$IsoPath = Join-Path $BuildRoot 'ut99_xemu_current.iso'
$ScreenshotRoot = Join-Path $BuildRoot 'xemu_syslink_screenshots'

$XemuExeSource = Join-Path $XemuRoot 'xemu.exe'
if( !(Test-Path $XemuExeSource) )
{
    $XemuExeSource = Join-Path $XemuRoot 'UT99Codex\xemu.exe'
}

$BootRom = Join-Path $XemuRoot 'MCPX\mcpx_1.0.bin'
$FlashRom = Join-Path $XemuRoot 'BIOS\xbox-4627_debug.bin'
$HostDir = Join-Path $XemuRoot 'UT99SyslinkHost'
$ClientDir = Join-Path $XemuRoot 'UT99SyslinkClient'
$HostHdd = Join-Path $XemuRoot 'UT99Test\HDD\ut99_hdd.qcow2'
$ClientHdd = Join-Path $XemuRoot 'UT99Fresh\HDD\ut99_hdd.qcow2'
$HostEepromSource = Join-Path $XemuRoot 'EEPROM\eeprom.bin'
$ClientEepromSource = $HostEepromSource
$XisoToolCandidates = @(
    (Join-Path $BuildRoot 'tools\extract-xiso\artifacts\extract-xiso.exe'),
    'C:\Programming\GitHub\Guitar Hero II\tools\artifacts\extract-xiso.exe',
    (Join-Path $RepoRoot '..\Guitar Hero II\tools\artifacts\extract-xiso.exe')
)

function Require-Path([string]$Path, [string]$Label)
{
    if( !(Test-Path $Path) )
    {
        throw "$Label not found: $Path"
    }
}

function Get-XemuProcessForConfig([string]$ConfigPath)
{
    $escaped = [System.IO.Path]::GetFullPath($ConfigPath)
    Get-CimInstance Win32_Process -Filter "Name = 'xemu.exe'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -and $_.CommandLine.IndexOf($escaped, [System.StringComparison]::OrdinalIgnoreCase) -ge 0 }
}

function Stop-XemuForConfig([string]$ConfigPath)
{
    $procs = @(Get-XemuProcessForConfig $ConfigPath)
    foreach( $proc in $procs )
    {
        Stop-Process -Id $proc.ProcessId -Force -ErrorAction SilentlyContinue
    }
}

function Read-MonitorAvailable([System.Net.Sockets.NetworkStream]$Stream)
{
    $bytes = New-Object byte[] 4096
    $text = ''
    while( $Stream.DataAvailable )
    {
        $read = $Stream.Read($bytes, 0, $bytes.Length)
        if( $read -le 0 )
        {
            break
        }
        $text += [System.Text.Encoding]::ASCII.GetString($bytes, 0, $read)
    }
    return $text
}

function Invoke-HmpCommands([int]$Port, [string[]]$Commands)
{
    $client = $null
    for( $attempt=0; $attempt -lt 40; $attempt++ )
    {
        try
        {
            $candidate = New-Object System.Net.Sockets.TcpClient
            $async = $candidate.BeginConnect('127.0.0.1', $Port, $null, $null)
            if( $async.AsyncWaitHandle.WaitOne(250) )
            {
                $candidate.EndConnect($async)
                $client = $candidate
                break
            }
            $candidate.Close()
        }
        catch
        {
            if( $candidate )
            {
                $candidate.Close()
            }
        }
        Start-Sleep -Milliseconds 250
    }

    if( !$client -or !$client.Connected )
    {
        throw "Could not connect to xemu monitor on port $Port"
    }

    $stream = $client.GetStream()
    $stream.ReadTimeout = 250
    Start-Sleep -Milliseconds 100
    $output = Read-MonitorAvailable $stream

    $writer = New-Object System.IO.StreamWriter($stream, [System.Text.Encoding]::ASCII)
    $writer.NewLine = "`n"
    $writer.AutoFlush = $true

    foreach( $command in $Commands )
    {
        $writer.WriteLine($command)
        Start-Sleep -Milliseconds 700
        $output += Read-MonitorAvailable $stream
    }

    $client.Close()
    return $output
}

function Get-PcapAdapter()
{
    $adapter = Get-NetAdapter -ErrorAction Stop |
        Where-Object { $_.Status -eq 'Up' -and $_.InterfaceGuid } |
        Sort-Object { if( $_.Name -eq 'Ethernet' ) { 0 } else { 1 } }, InterfaceMetric |
        Select-Object -First 1

    if( !$adapter )
    {
        throw "No active network adapter with an InterfaceGuid was found for XEMU pcap System Link."
    }

    return $adapter
}

function Get-PcapInterfaceName()
{
    $adapter = Get-PcapAdapter
    return "\Device\NPF_$($adapter.InterfaceGuid)"
}

function Get-DebugUdpHost()
{
    if( $DebugUdpHost )
    {
        return $DebugUdpHost
    }

    $adapter = Get-PcapAdapter
    $ip = Get-NetIPAddress -AddressFamily IPv4 -InterfaceIndex $adapter.ifIndex -ErrorAction Stop |
        Where-Object { $_.IPAddress -ne '127.0.0.1' -and $_.IPAddress -notlike '169.254*' } |
        Select-Object -First 1

    if( !$ip )
    {
        throw "Could not find an IPv4 address for $($adapter.Name) to use as the UDP debug target."
    }

    return $ip.IPAddress
}

function Find-XisoTool()
{
    foreach( $candidate in $XisoToolCandidates )
    {
        $expanded = [System.IO.Path]::GetFullPath($candidate)
        if( Test-Path $expanded )
        {
            return $expanded
        }
    }
    throw "extract-xiso.exe not found. Checked: $($XisoToolCandidates -join ', ')"
}

function Copy-TreeFiles([string]$SourceDir, [string]$DestDir)
{
    if( !(Test-Path $SourceDir) )
    {
        return
    }

    New-Item -ItemType Directory -Force -Path $DestDir | Out-Null
    $sourceFull = [System.IO.Path]::GetFullPath($SourceDir).TrimEnd('\')
    Get-ChildItem -LiteralPath $sourceFull -Recurse -File | ForEach-Object {
        $rel = $_.FullName.Substring($sourceFull.Length).TrimStart('\')
        $dest = Join-Path $DestDir $rel
        $destParent = Split-Path -Parent $dest
        if( !(Test-Path $destParent) )
        {
            New-Item -ItemType Directory -Force -Path $destParent | Out-Null
        }
        Copy-Item -LiteralPath $_.FullName -Destination $dest -Force
    }
}

function Write-DebugUdpMarker([string]$RootDir, [string]$HostIp)
{
    $content = @"
; Generated by launch_xemu_syslink_pair.ps1.
; Direct host target first for XEMU pcap, broadcast second for hardware parity.
Dest=$HostIp`:14099
Dest=255.255.255.255:14099
"@
    Set-Content -LiteralPath (Join-Path $RootDir 'XboxDebugUDP.ini') -Value $content -Encoding ASCII
}

function Rebuild-SourceIso()
{
    $stage = Join-Path $BuildRoot 'xemu_syslink_stage_current'
    $newIso = Join-Path $BuildRoot 'ut99_xemu_current.new.iso'
    $debugHost = Get-DebugUdpHost

    $release = Join-Path $BuildRoot 'release'
    Require-Path $RuntimeSource 'Known-good UT99 runtime source'
    Require-Path (Join-Path $release 'default.xbe') 'Release default.xbe'

    foreach( $path in @($stage, $newIso) )
    {
        if( Test-Path $path )
        {
            Remove-Item -LiteralPath $path -Recurse -Force
        }
    }

    New-Item -ItemType Directory -Force -Path $stage | Out-Null
    robocopy $RuntimeSource $stage /E /NFL /NDL /NJH /NJS /NP /XD Logs Screenshots | Out-Null
    if( $LASTEXITCODE -gt 7 )
    {
        throw "robocopy runtime staging failed with exit code $LASTEXITCODE"
    }

    Require-Path (Join-Path $stage 'Maps') 'XEMU staging Maps'
    Require-Path (Join-Path $stage 'Textures') 'XEMU staging Textures'
    Require-Path (Join-Path $stage 'System\Botpack.u') 'XEMU staging Botpack.u'
    Require-Path (Join-Path $stage 'System\Engine.u') 'XEMU staging Engine.u'
    Require-Path (Join-Path $stage 'System\UTMenu.u') 'XEMU staging UTMenu.u'

    Copy-Item -LiteralPath (Join-Path $release 'default.xbe') -Destination (Join-Path $stage 'default.xbe') -Force
    Copy-TreeFiles (Join-Path $release 'System') (Join-Path $stage 'System')
    Copy-TreeFiles (Join-Path $release 'MenuAssets') (Join-Path $stage 'MenuAssets')
    Copy-TreeFiles (Join-Path $release 'MusicXbox') (Join-Path $stage 'MusicXbox')
    Copy-TreeFiles (Join-Path $release 'Textures') (Join-Path $stage 'Textures')
    Write-DebugUdpMarker $stage $debugHost

    $smokePath = Join-Path $stage 'XboxSystemLinkSmoke.ini'
    if( $Smoke )
    {
        Set-Content -LiteralPath $smokePath -Value '' -Encoding ASCII
    }
    elseif( Test-Path $smokePath )
    {
        Remove-Item -LiteralPath $smokePath -Force
    }

    if( Test-Path $IsoPath )
    {
        Remove-Item -LiteralPath $IsoPath -Force
    }

    $xisoTool = Find-XisoTool
    & $xisoTool -Q -m -c $stage $newIso
    if( $LASTEXITCODE -ne 0 )
    {
        throw "extract-xiso failed with exit code $LASTEXITCODE"
    }
    Move-Item -LiteralPath $newIso -Destination $IsoPath -Force
    Remove-Item -LiteralPath $stage -Recurse -Force
    Write-Host "Rebuilt source XISO: $IsoPath"
    Write-Host "UDP debug target: $debugHost`:14099"
    if( $Smoke )
    {
        Write-Host "System Link smoke marker: enabled"
    }
    else
    {
        Write-Host "System Link smoke marker: disabled"
    }
}

function Enable-PcapNetwork([int]$MonitorPort, [string]$PcapIfName)
{
    $commands = @(
        "netdev_add pcap,id=xemu-netdev,ifname=$PcapIfName",
        'netdev_add hubport,id=xemu-netdev-hubport,hubid=0,netdev=xemu-netdev',
        'set_link nvnet.0 on',
        'info network'
    )
    return Invoke-HmpCommands $MonitorPort $commands
}

function Write-XemuConfig(
    [string]$InstanceDir,
    [string]$Name,
    [string]$HddPath,
    [string]$EepromPath,
    [string]$DvdPath,
    [string]$BindAddr,
    [string]$RemoteAddr
)
{
    $ScreenshotDir = Join-Path $ScreenshotRoot $Name
    New-Item -ItemType Directory -Force -Path $ScreenshotDir | Out-Null

    $ConfigPath = Join-Path $InstanceDir 'xemu.toml'
    $content = @"
[general]
show_welcome = false
screenshot_dir = '$ScreenshotDir'
games_dir = '$InstanceDir'
skip_boot_anim = true
last_viewed_menu_index = 1

[general.updates]
check = false

[input]
auto_bind = false
background_input_capture = true

[input.keyboard_controller_scancode_map]
a = 4
b = 5
start = 22
dpad_up = 26
dpad_down = 7
dpad_left = 20
dpad_right = 8

[input.bindings]
port1_driver = 'usb-xbox-gamepad'
port1 = 'keyboard'
port2_driver = 'usb-xbox-gamepad'
port3_driver = 'usb-xbox-gamepad'
port4_driver = 'usb-xbox-gamepad'

[display.debug.video]
advanced_tree_state = true

[net]
enable = false
backend = 'udp'

[net.udp]
bind_addr = '$BindAddr'
remote_addr = '$RemoteAddr'

[sys.files]
bootrom_path = '$BootRom'
flashrom_path = '$FlashRom'
eeprom_path = '$EepromPath'
hdd_path = '$HddPath'
dvd_path = '$DvdPath'
"@
    Set-Content -LiteralPath $ConfigPath -Value $content -Encoding ASCII
    return $ConfigPath
}

function Get-SafeChildPath([string]$ParentDir, [string]$LeafName)
{
    $parentFull = [System.IO.Path]::GetFullPath($ParentDir).TrimEnd('\')
    $childFull = [System.IO.Path]::GetFullPath((Join-Path $parentFull $LeafName))
    if( !$childFull.StartsWith($parentFull + '\', [System.StringComparison]::OrdinalIgnoreCase) )
    {
        throw "Refusing to create or remove a path outside $parentFull`: $childFull"
    }
    return $childFull
}

function Ensure-InstanceIso([string]$InstanceDir, [string]$LeafName)
{
    $dest = Get-SafeChildPath $InstanceDir $LeafName
    if( Test-Path $dest )
    {
        Remove-Item -LiteralPath $dest -Force
    }

    try
    {
        Copy-Item -LiteralPath $IsoPath -Destination $dest -Force
    }
    catch
    {
        throw "Could not create per-instance ISO at $dest. Original error: $($_.Exception.Message)"
    }
    return $dest
}

function Remove-InstanceIso([string]$InstanceDir, [string]$LeafName)
{
    $dest = Get-SafeChildPath $InstanceDir $LeafName
    if( Test-Path $dest )
    {
        Remove-Item -LiteralPath $dest -Force
    }
}

function Initialize-Instance(
    [string]$InstanceDir,
    [string]$EepromSource,
    [string]$EepromName,
    [switch]$ClientMac
)
{
    New-Item -ItemType Directory -Force -Path $InstanceDir | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $InstanceDir 'EEPROM') | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $InstanceDir 'shaders') | Out-Null

    $eepromPath = Join-Path $InstanceDir "EEPROM\$EepromName"
    Copy-Item -LiteralPath $EepromSource -Destination $eepromPath -Force

    if( $ClientMac )
    {
        Set-ClientEepromMac $eepromPath
    }
    return $eepromPath
}

function Get-XConfigChecksum([byte[]]$Data, [int]$Offset, [int]$Count)
{
    $mask = [uint64]4294967295
    $eax = [uint64]0
    $ebx = [uint64]0
    for( $i=0; $i -lt $Count; $i += 4 )
    {
        $word = [uint64][BitConverter]::ToUInt32($Data, $Offset + $i)
        $sum = $eax + $word
        if( $sum -gt $mask )
        {
            $ebx++
        }
        $eax = $sum -band $mask
    }
    $sum = $eax + $ebx
    if( $sum -gt $mask )
    {
        $sum = ($sum -band $mask) + 1
    }
    return [uint32]($sum -band $mask)
}

function Set-UInt32LE([byte[]]$Data, [int]$Offset, [uint32]$Value)
{
    $bytes = [BitConverter]::GetBytes($Value)
    [Array]::Copy($bytes, 0, $Data, $Offset, 4)
}

function Set-ClientEepromMac([string]$EepromPath)
{
    $bytes = [System.IO.File]::ReadAllBytes($EepromPath)
    if( $bytes.Length -ne 256 )
    {
        throw "Unexpected EEPROM size for $EepromPath"
    }

    $factoryOffset = 0x30
    $factorySize = 48
    $macOffset = $factoryOffset + 16

    $bytes[$macOffset + 5] = ($bytes[$macOffset + 5] + 1) -band 0xFF
    if( $bytes[$macOffset + 5] -eq $bytes[$macOffset + 4] )
    {
        $bytes[$macOffset + 5] = ($bytes[$macOffset + 5] + 1) -band 0xFF
    }

    Set-UInt32LE $bytes $factoryOffset ([uint32]0)
    $checksum = Get-XConfigChecksum $bytes $factoryOffset $factorySize
    Set-UInt32LE $bytes $factoryOffset ([uint32]([uint64]4294967295 - [uint64]$checksum))

    $verify = Get-XConfigChecksum $bytes $factoryOffset $factorySize
    if( $verify -ne [uint32]4294967295 )
    {
        throw ("Client EEPROM factory checksum failed: 0x{0:X8}" -f $verify)
    }

    [System.IO.File]::WriteAllBytes($EepromPath, $bytes)
}

function Ensure-InstanceExe([string]$InstanceDir)
{
    $dest = Join-Path $InstanceDir 'xemu.exe'
    if( Test-Path $dest )
    {
        return $dest
    }

    try
    {
        New-Item -ItemType HardLink -Path $dest -Target $XemuExeSource | Out-Null
    }
    catch
    {
        Copy-Item -LiteralPath $XemuExeSource -Destination $dest -Force
    }
    return $dest
}

Require-Path $XemuExeSource 'xemu.exe'
Require-Path $BootRom 'MCPX boot ROM'
Require-Path $FlashRom 'Xbox BIOS'
if( !$RebuildIso )
{
    Require-Path $IsoPath 'UT99 XISO'
}
Require-Path $HostHdd 'Host HDD image'
Require-Path $ClientHdd 'Client HDD image'
Require-Path $HostEepromSource 'Host EEPROM source'
Require-Path $ClientEepromSource 'Client EEPROM source'

$ExpectedHostConfig = Join-Path $HostDir 'xemu.toml'
$ExpectedClientConfig = Join-Path $ClientDir 'xemu.toml'

if( $Stop )
{
    Stop-XemuForConfig $ExpectedHostConfig
    Stop-XemuForConfig $ExpectedClientConfig
    Start-Sleep -Milliseconds 500
    Remove-InstanceIso $HostDir 'ut99_xemu_current_host.iso'
    Remove-InstanceIso $ClientDir 'ut99_xemu_current_client.iso'
    Write-Host "Stopped UT99 System Link xemu pair."
    return
}

if( !$SetupOnly )
{
    Stop-XemuForConfig $ExpectedHostConfig
    Stop-XemuForConfig $ExpectedClientConfig
}

if( $RebuildIso )
{
    Rebuild-SourceIso
}

$HostEeprom = Initialize-Instance $HostDir $HostEepromSource 'eeprom_host.bin'
$ClientEeprom = Initialize-Instance $ClientDir $ClientEepromSource 'eeprom_client.bin' -ClientMac
$HostExe = Ensure-InstanceExe $HostDir
$ClientExe = Ensure-InstanceExe $ClientDir
$HostIso = Ensure-InstanceIso $HostDir 'ut99_xemu_current_host.iso'
$ClientIso = Ensure-InstanceIso $ClientDir 'ut99_xemu_current_client.iso'

$HostConfig = Write-XemuConfig $HostDir 'host' $HostHdd $HostEeprom $HostIso '127.0.0.1:9360' '127.0.0.1:9361'
$ClientConfig = Write-XemuConfig $ClientDir 'client' $ClientHdd $ClientEeprom $ClientIso '127.0.0.1:9361' '127.0.0.1:9360'

$HostHash = (Get-FileHash -Algorithm SHA1 $HostEeprom).Hash
$ClientHash = (Get-FileHash -Algorithm SHA1 $ClientEeprom).Hash
if( $HostHash -eq $ClientHash )
{
    throw "Host and client EEPROMs are identical; System Link needs unique MAC identities."
}

Write-Host "Host config:   $HostConfig"
Write-Host "Client config: $ClientConfig"
Write-Host "Host xemu:     $HostExe"
Write-Host "Client xemu:   $ClientExe"
Write-Host "Host HDD:      $HostHdd"
Write-Host "Client HDD:    $ClientHdd"
Write-Host "Source XISO:   $IsoPath"
Write-Host "Host XISO:     $HostIso"
Write-Host "Client XISO:   $ClientIso"
Write-Host "EEPROM SHA1:   host=$HostHash client=$ClientHash"
$PcapIfName = Get-PcapInterfaceName
Write-Host "Pcap iface:    $PcapIfName"

if( $SetupOnly )
{
    return
}

Stop-XemuForConfig $HostConfig
Stop-XemuForConfig $ClientConfig

$hostArgs = @('-config_path', $HostConfig, '-monitor', 'tcp:127.0.0.1:4478,server,nowait')
$clientArgs = @('-config_path', $ClientConfig, '-monitor', 'tcp:127.0.0.1:4479,server,nowait')

$hostProc = Start-Process -FilePath $HostExe -ArgumentList $hostArgs -WorkingDirectory $HostDir -WindowStyle Hidden -PassThru
Start-Sleep -Milliseconds 1200
$clientProc = Start-Process -FilePath $ClientExe -ArgumentList $clientArgs -WorkingDirectory $ClientDir -WindowStyle Hidden -PassThru
Start-Sleep -Seconds 2

$hostNetwork = Enable-PcapNetwork 4478 $PcapIfName
$clientNetwork = Enable-PcapNetwork 4479 $PcapIfName
$hostNetworkOk = $hostNetwork.IndexOf('type=pcap', [System.StringComparison]::OrdinalIgnoreCase) -ge 0
$clientNetworkOk = $clientNetwork.IndexOf('type=pcap', [System.StringComparison]::OrdinalIgnoreCase) -ge 0

[pscustomobject]@{
    HostPid = $hostProc.Id
    ClientPid = $clientProc.Id
    HostConfig = $HostConfig
    ClientConfig = $ClientConfig
    HostMonitor = '127.0.0.1:4478'
    ClientMonitor = '127.0.0.1:4479'
    HostNetwork = if( $hostNetworkOk ) { 'enabled' } else { 'monitor output did not confirm' }
    ClientNetwork = if( $clientNetworkOk ) { 'enabled' } else { 'monitor output did not confirm' }
}
