param(
    [string]$ConfigPath = 'C:\Games\Emulators\Xemu\UT99Soak\xemu.toml',
    [string]$RuntimeConfigPath = 'C:\Games\Emulators\Xemu\UT99Soak\xemu_soak_runtime_config',
    [string]$InstanceDir = 'C:\Games\Emulators\Xemu\UT99Soak',
    [string]$ScreenshotDir = 'C:\Programming\GitHub\UnrealTournament_1.40\UT99-Xbox\build_cli\xemu_jailbreak_soak_screenshots',
    [string]$BootRom = 'C:\Games\Emulators\Xemu\MCPX\mcpx_1.0.bin',
    [string]$FlashRom = 'C:\Games\Emulators\Xemu\BIOS\xbox-4627_debug.bin',
    [string]$EepromSource = 'C:\Games\Emulators\Xemu\EEPROM\eeprom.bin',
    [string]$HddPath = 'C:\Games\Emulators\Xemu\UT99Test\HDD\ut99_hdd.qcow2',
    [string]$DvdPath = 'C:\Games\Emulators\Xemu\UT99Soak\ut99_xemu_soak_current.iso',
    [ValidateSet('native', 'auto', '4x3', '16x9')]
    [string]$DisplayAspectRatio = 'auto',
    [ValidateSet('640x480', '720x480', '1280x720', '1280x800', '1280x960', '1920x1080', '2560x1440', '2560x1600', '2560x1920', '3840x2160')]
    [string]$DisplayWindowSize = '1280x960',
    [switch]$WriteToml
)

$ErrorActionPreference = 'Stop'

function Require-Path([string]$Path, [string]$Label)
{
    if( !(Test-Path -LiteralPath $Path) )
    {
        throw "$Label not found: $Path"
    }
}

Require-Path $BootRom 'MCPX boot ROM'
Require-Path $FlashRom 'Xbox BIOS'
Require-Path $EepromSource 'EEPROM source'
Require-Path $HddPath 'Xemu HDD'
Require-Path $DvdPath 'XISO'

New-Item -ItemType Directory -Force -Path $InstanceDir | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $InstanceDir 'EEPROM') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $InstanceDir 'shaders') | Out-Null
New-Item -ItemType Directory -Force -Path $ScreenshotDir | Out-Null

$EepromPath = Join-Path $InstanceDir 'EEPROM\eeprom_soak.bin'
Copy-Item -LiteralPath $EepromSource -Destination $EepromPath -Force

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

[display.window]
startup_size = '$DisplayWindowSize'
last_width = $($DisplayWindowSize.Split('x')[0])
last_height = $($DisplayWindowSize.Split('x')[1])

[display.ui]
fit = 'scale'
aspect_ratio = '$DisplayAspectRatio'

[display.debug.video]
advanced_tree_state = true

[net]
enable = false
backend = 'udp'

[sys.files]
bootrom_path = '$BootRom'
flashrom_path = '$FlashRom'
eeprom_path = '$EepromPath'
hdd_path = '$HddPath'
dvd_path = '$DvdPath'
"@

Set-Content -LiteralPath $RuntimeConfigPath -Value $content -Encoding ASCII
if( $WriteToml )
{
    Set-Content -LiteralPath $ConfigPath -Value $content -Encoding ASCII
}

[pscustomobject]@{
    ConfigPath = $ConfigPath
    ConfigPathUpdated = [bool]$WriteToml
    RuntimeConfigPath = $RuntimeConfigPath
    DvdPath = $DvdPath
    HddPath = $HddPath
    EepromPath = $EepromPath
    DisplayAspectRatio = $DisplayAspectRatio
    DisplayWindowSize = $DisplayWindowSize
}
