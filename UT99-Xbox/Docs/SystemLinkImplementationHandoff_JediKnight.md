# UT99 Xbox System Link Implementation Handoff For Jedi Knight

Date: 2026-06-23
Source repo: `C:\Programming\GitHub\UnrealTournament_1.40`
Target audience: another Codex instance wiring original Xbox System Link into the Jedi Knight Xbox port.

This document explains how System Link was wired for the UT99 Xbox port: the
XDK references used, the code shape, the secure XNet session flow, the
four-local-players-per-Xbox solution, and the XEMU smoke-test setup.

Do not use Xbox 360 tools, XNA tooling, or any 360-era networking assumptions.
This is original Xbox XDK/XNet/Winsock work.

## Current UT Baseline

The implementation to study is on `main` at:

- `8211328 Implement four-player system link per console`
- `16674f6 Fix UT99 XEMU syslink ISO staging`
- `6f811fa Harden Xbox system link alpha flow`

Main UT files touched:

- `UT99-Xbox/XboxDrv/src/XboxViewport.cpp`
- `IpDrv/Src/UnSocket.cpp`
- `IpDrv/Src/TcpNetDriver.cpp`
- `Engine/Src/UnLevel.cpp`
- `Engine/Src/UnLevAct.cpp`
- `Engine/Src/UnConn.cpp`
- `Engine/Src/UnLevTic.cpp`
- `Engine/Inc/UnConn.h`
- `UT99-Xbox/Tools/launch_xemu_syslink_pair.ps1`
- `UT99-Xbox/Tools/poll_xemu_ram_log.py`
- `UT99-Xbox/Tools/udp_debug_listener.py`

Last known automated XEMU smoke evidence:

- Two XEMU instances discovered each other.
- One became host/listen server, one became client.
- Four local players per XEMU instance became real active players, not dummy
  placeholders.
- Expected final proof lines included `gamePlayers=8`, four local slots on the
  host, four local slots on the client, `dummy=0`, `playerActor=1`, movement,
  acceleration, and proper host/client net modes.

Hardware alpha testing is still separate from this handoff.

## XDK References Used

The most important XDK reference was:

`C:\XDK\Samples\Xbox\Networking\WinsockPeer`

Useful files:

- `C:\XDK\Samples\Xbox\Networking\WinsockPeer\readme.txt`
- `C:\XDK\Samples\Xbox\Networking\WinsockPeer\WinsockPeer.cpp`
- `C:\Programming\GitHub\xbox\public\xdk\inc\winsockx.h`

Important reference conclusions:

1. `XNetStartup()` must happen before `WSAStartup()`.
2. A System Link host creates a secure session with `XNetCreateKey()` and
   registers it with `XNetRegisterKey()`.
3. The host advertises:
   - its `XNADDR`, from `XNetGetTitleXnAddr()`
   - the session key id, `XNKID`
   - the exchange/session key, `XNKEY`
4. A client joins by:
   - registering the host key with `XNetRegisterKey()`
   - translating the host `XNADDR` plus `XNKID` to a local-only `IN_ADDR` with
     `XNetXnAddrToInAddr()`
   - using that translated `IN_ADDR` for normal Winsock traffic
5. The host can verify an incoming secure peer with `XNetInAddrToXnAddr()`.
   If this fails for a required secure LAN connection, reject/drop the packet.
6. `XNetGetConnectStatus()` and `XNetConnect()` are needed because a translated
   secure address can exist before the association is fully connected.
7. The translated address often looks like `0.x.y.z`. That is valid for XNet
   secure Winsock traffic even though it is not a routable LAN IP.

The relevant XDK header comments in `winsockx.h` also mention:

- `XNET_STARTUP_BYPASS_SECURITY` is devkit-only and ignored by secure libs.
- `cfgKeyRegMax` defaults to four registered key pairs.
- `cfgSecRegMax` defaults to 32 security associations.
- Security associations are created for each unique `XNADDR`/`XNKID` pair
  passed to `XNetXnAddrToInAddr()`.

For UT, the implementation stayed with real secure XNet behavior rather than
leaning on devkit-only bypass flags.

## High-Level Architecture

UT separates System Link into two layers:

1. Frontend/lobby discovery and launch coordination.
2. Real gameplay networking.

The lobby uses a small UDP broadcast protocol on ports `9777` through `9780`.
Gameplay uses Unreal's normal UDP net driver on port `7777`, but with Xbox
secure-session address translation inserted before travel/connect.

This separation mattered. The first probe only proved packets could be seen.
Actual gameplay needed the XDK secure-session path and net-driver integration.

## Lobby State Machine

The frontend state lives mostly in `UT99-Xbox/XboxDrv/src/XboxViewport.cpp`.

Constants and state:

- `GXboxSystemLinkBasePort = 9777`
- `GXboxSystemLinkPortCount = 4`
- `GXboxSystemLinkMaxPeers = 8`
- `GXboxSystemLinkGamePort = 7777`
- `EXboxSystemLinkRole`
  - `XSLR_Seeking`
  - `XSLR_Host`
  - `XSLR_Client`
- `EXboxSystemLinkPhase`
  - `XSLP_Discovery`
  - `XSLP_Ready`
  - `XSLP_ReadyConfirmed`
  - `XSLP_MapSelect`
  - `XSLP_Launching`
- `FXboxSystemLinkPeer`
- `FXboxSystemLinkProbe`
- Global state: `GXboxSystemLink`

Entry:

- Entering the System Link menu calls `XboxSystemLinkStart()`.
- Leaving the menu calls `XboxSystemLinkStop()`.
- Starting travel calls `XboxSystemLinkStopForTravel()` so the lobby socket is
  closed but the network stack/key state needed by gameplay is preserved.

Important functions:

- `XboxSystemLinkStart()`
- `XboxSystemLinkStopLobby()`
- `XboxSystemLinkStop()`
- `XboxSystemLinkStopForTravel()`
- `XboxSystemLinkTick()`
- `XboxSystemLinkSendProbe()`
- `XboxSystemLinkRecordPeer()`
- `XboxSystemLinkUpdateElection()`
- `XboxSystemLinkUpdateProgress()`
- `XboxSystemLinkScheduleHostLaunch()`
- `XboxSystemLinkCheckRemoteLaunch()`
- `XboxSystemLinkStartTravel()`

The menu calls `XboxSystemLinkTick()` while the System Link screen is active.
That tick:

1. Starts the socket if needed.
2. Expires stale peers.
3. Re-runs host election.
4. Sends a broadcast probe roughly every 0.5 seconds.
5. Reads all pending probe packets with non-blocking `recvfrom()`.
6. Records peer state.
7. Advances readiness/map-select/launch phases.

## Discovery Packet Versions

The final packet format is `UTXSL4`.

Older formats are still accepted for compatibility with earlier test builds:

- `UTXSL1`
- `UTXSL2`
- `UTXSL3`
- `UTXSL4`

`UTXSL4` contains:

- local machine id
- local probe port
- send counter
- role
- host id
- phase
- ready mask
- locked mask
- confirmed flag
- game type
- map index
- frag limit index
- time limit index
- skill index
- launch id
- launch ack id
- secure-info-present flag
- host `XNADDR` as hex
- session `XNKID` as hex
- session `XNKEY` as hex

The host only advertises secure info when:

- it is currently elected host
- it has a registered host session
- `XNetGetTitleXnAddr()` has produced a valid local `XNADDR`

The client does not invent secure state. It consumes the host's advertised
secure state.

## Host Election

Host election happens in `XboxSystemLinkUpdateElection()`.

The behavior is intentionally simple:

- If a peer is already advertising itself as host, prefer the earliest-seen host.
- Tie-break with lower machine id.
- If no host exists and no peers are seen after `GXboxSystemLinkHostClaimSeconds`
  (`1.5f`), the local machine claims host.
- If multiple seeking machines see each other, the lowest machine id becomes
  host.

When a local machine loses host role, UT unregisters the host session key. When
a local machine stops being a client, UT unregisters any client-registered host
key. This prevents stale XNet key state from crossing lobby sessions.

## Ready-Up Flow

System Link reuses the split-ready player slots but keeps the frontend as one
CityIntro-backed viewport until gameplay map travel.

Flow:

1. Machines discover each other.
2. Host election completes.
3. Each machine shows the System Link ready screen.
4. Local players press `A` to join.
5. Joined local players select character/team.
6. `A` locks the local slot.
7. Player 1 presses `Start` to confirm that Xbox.
8. The local machine advertises `ReadyConfirmed`.
9. Host waits until every machine in the group is confirmed.
10. Host transitions to map select.
11. Only the host selects map/mode settings.
12. Host starts the launch handshake.

Important checks:

- At least one local slot must be joined/locked.
- System Link confirmation is blocked until another Xbox is present.
- A machine is confirmed only if every joined local slot is locked.

Relevant functions:

- `XboxSystemLinkReadyMask()`
- `XboxSystemLinkLockedMask()`
- `XboxSystemLinkLocalReadyCanConfirm()`
- `XboxSystemLinkConfirmedMachineCount()`
- `XboxSystemLinkAllMachinesConfirmed()`
- `XboxMenuDrawSystemLink()`
- split-ready helpers around `GXboxSplitReadySlots`

## Secure XNet Session Flow

### Network Startup

UT initializes XNet in `IpDrv/Src/UnSocket.cpp`.

Function:

- `XboxInitXNetForIpDrv()`

It:

1. zeroes `XNetStartupParams`
2. sets `cfgSizeOfStruct = sizeof(Params)`
3. calls `XNetStartup(&Params)`
4. then performs normal socket startup

This follows the XDK rule that XNet must be initialized before Winsock.

### Host Session Creation

UT creates the host secure session in:

- `XboxSystemLinkEnsureHostSession()`

It:

1. unregisters any incompatible previous session
2. calls `XNetCreateKey(&SessionKeyId, &SessionKey)`
3. calls `XNetRegisterKey(&SessionKeyId, &SessionKey)`
4. stores session ownership in `GXboxSystemLink`
5. advertises the session in `UTXSL4`

Before launching, the host also calls:

- `XboxSystemLinkUpdateLocalXnAddr()`

That uses `XNetGetTitleXnAddr()` and rejects pending/none/troubleshoot states.

### Client Host Resolution

The client resolves the host in:

- `XboxSystemLinkResolveHostSecureAddress()`

It:

1. accepts a peer record containing host `XNADDR`, `XNKID`, and `XNKEY`
2. unregisters old key state if the host changed
3. calls `XNetRegisterKey(&HostPeer->SessionKeyId, &HostPeer->SessionKey)`
4. calls `XNetXnAddrToInAddr(&HostPeer->XnAddr, &HostPeer->SessionKeyId, &SecureAddr)`
5. calls `XNetConnect(SecureAddr)` if status is idle/lost
6. saves the translated `SecureAddr.s_addr`

The client travel address comes from:

- `XboxSystemLinkSelectClientTravelAddress()`

It prefers the translated XNet virtual address. The raw broadcast sender IP is
only a fallback for diagnostics/non-secure paths.

### Why The Raw LAN IP Was Not Enough

The raw UDP sender address from discovery is not the final gameplay address.
On original Xbox secure networking, the correct gameplay address is the local
`IN_ADDR` returned by `XNetXnAddrToInAddr()`.

Using the broadcast IP directly caused repeated failures during the UT work:

- clients got kicked back to frontend/CityIntro
- hosts waited forever or crashed/soft-crashed
- XEMU sometimes ended up at "Please insert an Xbox disc" after bad staging or
  bad travel sequences

The rule that fixed it: advertise XNADDR/key data in the lobby, translate it
before gameplay travel, and make the net driver secure-aware.

## Gameplay Net Driver Integration

UT gameplay networking uses `IpDrv.TcpNetDriver` despite the name; it is UDP
socket based in this engine.

Key file:

- `IpDrv/Src/TcpNetDriver.cpp`

Key helpers:

- `XboxIpDrvSetSecureTravelHost()`
- `XboxIpDrvClearSecureTravelHost()`
- `XboxTcpConfigureSecureTravelHostFromURL()`
- `XboxTcpTranslateSecureTravelHost()`
- `XboxTcpEnsureSecureAssociation()`
- `XboxTcpWaitForSecureAssociation()`
- `XboxTcpTranslateSecurePeer()`
- `XboxTcpEnsureSecurePeerAssociation()`
- `XboxTcpWaitForSecurePeerAssociation()`

The client travel URL includes secure options:

- `SessionID=<hex XNKID>`
- `ExchangeKey=<hex XNKEY>`
- `HostAddr=<hex XNADDR>`

`UTcpNetDriver::InitConnect()` checks `?LAN` and calls
`XboxTcpConfigureSecureTravelHostFromURL()`. That function decodes the URL
options, registers the host key, translates the host `XNADDR` to a secure
virtual `IN_ADDR`, and replaces `ConnectURL.Host` with that translated address.

`UTcpipConnection::LowLevelSend()` ensures the secure association is connected
before first send and retries/refreshes if XNet reports a lost association.

`UTcpNetDriver::InitListen()` marks LAN listen sockets as requiring secure
peers:

- `RequireSecurePeers = LocalURL.HasOption(TEXT("LAN"))`

During `TickDispatch()`, when a new sender is not yet matched to a connection,
the host calls:

- `XNetInAddrToXnAddr(FromAddr.sin_addr, &SecureXnAddr, NULL)`

If that fails while `RequireSecurePeers` is true, UT drops the packet instead
of accepting a non-secure LAN sender.

This was a major source of confidence. It means the lobby can use broadcast UDP
for discovery, but gameplay must pass through real XNet secure association.

## Travel URL Construction

The launch URL is built in:

- `XboxSystemLinkBuildSelectedMapURL()`

Host/listen URL:

```text
<map>?Game=<gameclass>?FragLimit=<n>?TimeLimit=<n>?MinPlayers=0?MaxPlayers=16?Difficulty=<n>?Listen?LAN<player-options>
```

Client URL:

```text
<secure-host-ip>:7777?LAN?SessionID=<xnkid-hex>?ExchangeKey=<xnkey-hex>?HostAddr=<xnaddr-hex><player-options>
```

Immediately before `SetClientTravel()`:

- host calls `XboxIpDrvClearSecureTravelHost()`
- client calls `XboxIpDrvSetSecureTravelHost(...)` when secure host info is
  available
- both call `XboxSystemLinkStopForTravel()` to close the lobby socket while
  keeping network/session state alive

Then:

- `Client->Engine->SetClientTravel(Viewport, URL, 0, TRAVEL_Absolute)`

## Launch Handshake

The launch handshake exists to stop the host from travelling before clients
have registered the secure key, translated the host address, and acknowledged
the launch id.

Host path:

1. `XboxSystemLinkScheduleHostLaunch()`
2. ensure all machines confirmed
3. ensure host session exists
4. ensure local `XNADDR` exists
5. increment `LaunchId`
6. advertise phase `XSLP_Launching`
7. wait for client `LaunchAckId`
8. broadcast a commit by setting host `LaunchAckId = LaunchId`
9. travel after the commit delay

Client path:

1. `XboxSystemLinkCheckRemoteLaunch()`
2. see host phase `XSLP_Launching`
3. resolve secure host address
4. send `LaunchAckId = HostPeer->LaunchId`
5. wait until host also advertises `LaunchAckId == LaunchId`
6. set pending travel after a short client delay
7. call `XboxSystemLinkStartTravel()`

This two-step ack/commit flow fixed repeated cases where one side travelled
early and the other side fell back to frontend.

## Four Local Players Per Xbox

UT needed four proper players per Xbox in a System Link match. These could not
be visual hacks or dummy cameras.

The final model is:

- one network connection per Xbox
- one primary player joined through the engine's normal network login path
- up to three additional local players created over the same connection by a
  custom control-channel command

### Client Side

After the client has connected and the primary player is open, UT sends extra
join commands:

- `XboxSystemLinkSendClientChildJoins()`

For each joined local slot 1 through 3:

```text
XSLJOIN SLOT=<slot> URL=<slot-player-options>
```

The options are built from the same split-ready player setup data used for
local splitscreen:

- name
- class
- skin/face where applicable
- team
- voice

The client tracks:

- `GXboxSystemLinkChildJoinSentMask`
- `GXboxSystemLinkChildBoundMask`

### Server Side

The server handles the control-channel command in:

- `Engine/Src/UnLevel.cpp`
- `ULevel::NotifyReceivedText()`

When it receives `XSLJOIN`:

1. validate that the connection is open and has a primary actor
2. validate slot `1..3`
3. append `?XSLOT=<slot>` to the child request
4. call `SpawnPlayActor(Connection, ROLE_AutonomousProxy, FURL(...))`
5. add the child player actor to the connection's child actor list
6. reply with `XSLJOINOK SLOT=<slot>` or `XSLJOINFAIL`

The child actor special case is in:

- `Engine/Src/UnLevAct.cpp`
- `ULevel::SpawnPlayActor()`

When `XSLOT` is present and the `UPlayer*` is a `UNetConnection` with an
existing primary actor:

1. set `Actor->Player = Player`
2. add actor to `Conn->XboxChildActors`
3. call `Actor->eventPossess()`
4. mark it always relevant
5. do not replace the connection's primary `Actor`

Connection cleanup is in:

- `Engine/Src/UnConn.cpp`

When the connection closes, UT destroys all `XboxChildActors` so stale child
pawns do not remain in the level.

### Client Viewport Binding

The client waits for replicated autonomous child actors, then binds them to the
extra local viewports.

Relevant functions in `XboxViewport.cpp`:

- `XboxSystemLinkFindUnboundAutonomousChild()`
- `XboxSystemLinkCountUnboundAutonomousChildren()`
- `XboxSystemLinkBindClientChildActors()`
- `XboxSplitActivateAfterMapLoad()`

The client does not activate splitscreen fully until the required child actors
exist. This prevents viewports from binding to dummy actors and passing a fake
smoke test.

### Server Relevancy

The server's relevancy logic also had to account for child player viewpoints.
This was patched in:

- `Engine/Src/UnLevTic.cpp`

The server collects child viewers from `Connection->XboxChildActors` and tests
actor visibility against those viewpoints too. Without this, the server can
replicate correctly for the primary player but starve actors that only child
players can see.

## Logging And Debugging

UT's logging is a critical part of the workflow.

### File Log

`FXboxLogger` opens before Unreal initialization:

- `UT99-Xbox/XboxLaunch/inc/FXboxLogger.h`
- `UT99-Xbox/XboxLaunch/src/XboxLaunch.cpp`

It writes to `ut99.log` with fallbacks:

1. `D:\ut99.log`
2. `T:\ut99.log`
3. `E:\UT99\ut99.log`
4. `E:\ut99.log`
5. relative `ut99.log`

On this project, the usual hardware log path is:

`C:\Programming\GitHub\UnrealTournament_1.40\UT99-Xbox\build_cli\release\ut99.log`

for the staged/release folder the Xbox is running from.

Important log prefixes:

- `XSL` for System Link lobby/launch
- `XNET` for net driver secure association
- `XSPLIT` for split/local player viewport work
- `XSPAWN` for login/spawn actor work
- `XMENU` for menu transitions
- `MEM` for memory snapshots

### UDP Debug Mirror

Marker:

- `UT99-Xbox/XboxDebugUDP.ini`

Listener:

```powershell
python UT99-Xbox\Tools\udp_debug_listener.py --host 0.0.0.0 --port 14099
```

The listener writes by default to:

`UT99-Xbox\build_cli\release\systemlink_udp_debug.log`

This is useful when a crash or dashboard return prevents the normal file log
from being flushed or copied back.

### XEMU RAM Log Mirror

UT also has a RAM log mirror that can be harvested through XEMU's HMP monitor.

Poll command:

```powershell
python UT99-Xbox\Tools\poll_xemu_ram_log.py --ports 4478,4479
```

Defaults:

- map: `UT99-Xbox\build_cli\release\UnrealTournament.map`
- xbe: `UT99-Xbox\build_cli\release\default.xbe`
- output dir: `UT99-Xbox\build_cli\xemu_ram_logs`

This was useful because XEMU can soft-crash back to "Please insert an Xbox
disc" while the process is still alive. The RAM mirror can still contain the
last log lines if the normal file log is gone or stale.

## XEMU Pair Setup

The main helper is:

`UT99-Xbox/Tools/launch_xemu_syslink_pair.ps1`

Important paths baked into the script:

- XEMU root: `C:\Games\Emulators\Xemu`
- runtime source: `C:\Games\Emulators\CXBX\UT99x`
- source ISO: `UT99-Xbox\build_cli\ut99_xemu_current.iso`
- host instance dir: `C:\Games\Emulators\Xemu\UT99SyslinkHost`
- client instance dir: `C:\Games\Emulators\Xemu\UT99SyslinkClient`
- host HDD: `C:\Games\Emulators\Xemu\UT99Test\HDD\ut99_hdd.qcow2`
- client HDD: `C:\Games\Emulators\Xemu\UT99Fresh\HDD\ut99_hdd.qcow2`
- EEPROM source: `C:\Games\Emulators\Xemu\EEPROM\eeprom.bin`
- monitor ports:
  - host: `127.0.0.1:4478`
  - client: `127.0.0.1:4479`

The script writes separate `xemu.toml` files for host and client.

Key XEMU config choices:

- `show_welcome = false`
- `skip_boot_anim = true`
- per-instance `screenshot_dir`
- `background_input_capture = true`
- simple keyboard controller mapping for smoke tests
- unique host/client EEPROM files
- per-instance ISO copies

Do not point both XEMU instances at the same ISO file. The script creates
per-instance copies:

- `ut99_xemu_current_host.iso`
- `ut99_xemu_current_client.iso`

This was necessary because shared/locked/stale ISO state repeatedly produced
"Please insert an Xbox disc" failures.

The script also changes the client EEPROM MAC and recomputes the factory
checksum. Identical EEPROM/MAC identities are bad for System Link testing.

### Build And Launch Commands

Build the XBE:

```powershell
python UT99-Xbox\Tools\build_xbox_cli.py --config Release
```

Build the current XISO and launch a two-XEMU smoke pair:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File UT99-Xbox\Tools\launch_xemu_syslink_pair.ps1 -RebuildIso -Smoke
```

Run the four-local-players-per-Xbox stress marker:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File UT99-Xbox\Tools\launch_xemu_syslink_pair.ps1 -RebuildIso -Smoke -FourPlayerStress
```

Stop both configured instances and delete their per-instance ISOs:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File UT99-Xbox\Tools\launch_xemu_syslink_pair.ps1 -Stop
```

The `-Stop` path intentionally deletes the per-instance ISO copies instead of
leaving stale multi-gigabyte files around.

### Network Modes

The launcher supports three local network modes.

Default pcap mode:

- Uses XEMU monitor commands to attach `pcap`.
- Chooses the first active Windows adapter, preferring `Ethernet`.
- Uses `\Device\NPF_{InterfaceGuid}`.
- Best for XEMU plus real Xbox on the LAN.
- Requires Npcap.

UDP backend mode:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File UT99-Xbox\Tools\launch_xemu_syslink_pair.ps1 -RebuildIso -Smoke -UdpBackend
```

- Uses XEMU's configured UDP backend.
- Host bind: `127.0.0.1:9360`
- Client bind: `127.0.0.1:9361`
- Good for same-machine two-XEMU testing when pcap is noisy.

Multicast socket mode:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File UT99-Xbox\Tools\launch_xemu_syslink_pair.ps1 -RebuildIso -Smoke -McastBackend
```

- Uses HMP `netdev_add socket` with multicast endpoint `230.0.0.1:9362`.
- Useful as another same-machine virtual network option.

The script attaches the network through XEMU monitor commands:

- `netdev_add pcap,...`
- `netdev_add socket,...`
- `netdev_add hubport,...`
- `set_link nvnet.0 on`
- `info network`

It verifies the monitor output contains the expected backend type.

## XEMU Smoke Markers

The launcher can place marker files into the staged XISO:

- `XboxSystemLinkSmoke.ini`
- `XboxSystemLink4PStress.ini`

UT detects these at runtime:

- `XboxSystemLinkSmokeEnabled()`
- `XboxSystemLinkFourPlayerStressEnabled()`

Smoke behavior:

1. Open the menu automatically.
2. Enter System Link automatically.
3. Wait for peer discovery.
4. Join and lock local slots.
5. Confirm readiness.
6. Host waits for all machines.
7. Host reaches map select.
8. Host picks a lightweight test map if present, currently `DM-Fractal.unr`.
9. Host schedules launch.
10. Client acks launch.
11. Host commits launch.
12. Both travel into the match.
13. Gameplay status logs prove active players.

For four-player stress, each XEMU joins/locks four local slots.

Expected good log progression:

- `XSL SMOKE opened System Link lobby`
- `XSL lobby started`
- `XSL peer update`
- `XSL lobby role`
- `XSL SMOKE local slots joined/locked/confirmed`
- `XSL all machines confirmed -> host map select`
- `XSL host launch scheduled`
- `XSL client launch acked`
- `XSL host launch commit announced`
- `XSL client launch commit received`
- `XSL launch travel`
- `XNET InitListen ... lan=1`
- `XNET InitConnect ... lan=1`
- `XNET secure URL translated`
- `XSLJOIN request`
- `XSLJOIN succeeded`
- `XSL child bound`
- final smoke gameplay status with all players active

For the final four-player-per-Xbox proof, look for:

- host/listen net mode
- client net mode
- `gamePlayers=8`
- local slots `Player1` through `Player4` on host
- local slots `Player5` through `Player8` on client
- every local slot has `dummy=0`
- every local slot has `playerActor=1`
- movement/acceleration data changes

## Screenshots

The launcher sets per-instance `screenshot_dir` under:

`UT99-Xbox\build_cli\xemu_syslink_screenshots`

Use XEMU's own screenshot/menu path or monitor-backed screenshot capture. Do
not force-focus windows for screenshots during unattended testing.

## Common Failure Modes And Fixes

### Both Instances Show "Please insert an Xbox disc"

Likely causes:

- stale/bad XISO
- both instances sharing one ISO
- XEMU still has a previous ISO locked
- wrong runtime source staged

Fix:

1. stop both configured XEMU instances with `-Stop`
2. rebuild XBE
3. rebuild XISO with `-RebuildIso`
4. ensure the script creates separate host/client ISO copies

Do not load unrelated games or reference repos to fix this. For UT, the correct
runtime source was `C:\Games\Emulators\CXBX\UT99x`.

### Peers See Each Other But Gameplay Fails

Likely causes:

- client used raw broadcast IP instead of XNet virtual address
- host did not advertise secure info
- client did not register key
- `XNetXnAddrToInAddr()` failed
- host travelled before client ack/commit
- net driver ignored URL secure options

Logs to inspect:

- `XSL host session registered`
- `XSL host address translated`
- `XSL client session registered`
- `XSL client launch acked`
- `XSL client launch commit received`
- `XNET secure URL translated`
- `XNET dropped non-secure LAN packet`

### Only One Player Per Xbox Is Real

Likely causes:

- child join commands not sent after connection opened
- server rejected `XSLJOIN`
- child actors spawned but client never bound them to viewports
- server relevancy ignores child views

Logs to inspect:

- `XSL child join sent`
- `XSLJOIN request`
- `XSLJOIN succeeded`
- `XSL child bound`
- final smoke status per local slot

### Peers Flap Or Elect Wrong Host

Likely causes:

- duplicate EEPROM/MAC
- identical local machine ids after reset
- stale peer records not expiring
- two instances launched from same config/folder

The UT launcher avoids this by using separate instance folders and altering the
client EEPROM MAC.

### Dashboard Return Or Soft Crash Loses Log

Use:

```powershell
python UT99-Xbox\Tools\poll_xemu_ram_log.py --ports 4478,4479
```

or enable UDP mirror with `XboxDebugUDP.ini` and listen with:

```powershell
python UT99-Xbox\Tools\udp_debug_listener.py
```

## Jedi Knight Adaptation Checklist

The reusable parts for Jedi Knight are the XNet and test harness patterns. The
engine-specific parts must be adapted to Jedi Knight's networking/player model.

1. Identify the existing network driver/socket layer.
   - Find where the game calls `XNetStartup`, `WSAStartup`, `socket`, `bind`,
     `connect`, `sendto`, `recvfrom`, and any connect/listen URL parsing.
   - XNet startup must precede Winsock startup.

2. Add a small System Link lobby layer.
   - UDP broadcast discovery on a small fixed port range.
   - local machine id.
   - host election.
   - ready/confirm state.
   - host map/options state.
   - launch id and launch ack id.

3. Advertise secure host information.
   - Host creates/registers `XNKID`/`XNKEY`.
   - Host obtains `XNADDR`.
   - Host serializes `XNADDR`, `XNKID`, and `XNKEY` into the lobby packet.
   - Use hex encoding. Keep packets compact and parseable.

4. Translate secure addresses before gameplay.
   - Client registers host key.
   - Client calls `XNetXnAddrToInAddr()`.
   - Client calls `XNetConnect()`/checks `XNetGetConnectStatus()`.
   - Client gameplay connect uses translated `IN_ADDR`.

5. Make the gameplay net driver secure-aware.
   - Client connect path must consume the advertised secure key material.
   - Host listen path should reject non-secure senders when in LAN/System Link
     mode by using `XNetInAddrToXnAddr()`.
   - Sends should handle idle/lost XNet association with a short wait/retry.

6. Add an ack/commit launch handshake.
   - Host advertises launch id.
   - Client resolves secure address and acks launch id.
   - Host waits for every confirmed machine's ack.
   - Host broadcasts commit.
   - Client travels only after commit.
   - Host travels after commit delay.

7. Decide how Jedi Knight should represent multiple local players.
   - If JK already supports splitscreen, do not create fake viewports.
   - Prefer one network peer/machine with multiple local player entities over
     four independent sockets from the same Xbox.
   - Add explicit child-player join messages if the engine only creates one
     remote player per connection.
   - Ensure server visibility/relevancy considers every local player's camera.

8. Add hard logging first.
   - file log early in boot
   - System Link prefix
   - XNet prefix
   - launch id/ack/state logs
   - secure address translation logs
   - player-count proof logs
   - optional UDP/RAM mirror for XEMU crashes

9. Build the XEMU test pair before hardware iteration.
   - separate instance folders
   - separate EEPROMs/MACs
   - separate ISO copies
   - monitor ports
   - pcap for hardware/XEMU, UDP or mcast for same-machine virtual testing
   - clean stale ISOs on stop

10. Define proof before declaring success.
    - discovery works
    - host/client roles stable
    - every machine confirms
    - host reaches map select
    - client receives selected map
    - secure address translation succeeds
    - both machines travel
    - gameplay starts
    - every expected local player is active, unique, controllable, and visible
    - exit/travel cleanup does not leave sockets, keys, or dummy players alive

## What Not To Repeat

- Do not treat discovery as gameplay.
- Do not use the broadcast sender IP as the final gameplay connect address when
  secure XNet info is available.
- Do not let host travel before clients acknowledge secure launch.
- Do not run both XEMU instances from the same config, HDD, EEPROM, or ISO.
- Do not leave stale ISOs in the XEMU folders.
- Do not rely on hidden/fake players for four-player-per-Xbox proof.
- Do not bypass the real game networking layer with a smoke-test shortcut.
- Do not make frontend split-screen render four CityIntro viewports before
  actual gameplay loads.

## Minimum Source Reading Order For Jedi Knight Work

For UT reference, read in this order:

1. `C:\XDK\Samples\Xbox\Networking\WinsockPeer\readme.txt`
2. `C:\XDK\Samples\Xbox\Networking\WinsockPeer\WinsockPeer.cpp`
3. `C:\Programming\GitHub\xbox\public\xdk\inc\winsockx.h`
4. `IpDrv/Src/UnSocket.cpp`
5. `UT99-Xbox/XboxDrv/src/XboxViewport.cpp`
6. `IpDrv/Src/TcpNetDriver.cpp`
7. `Engine/Src/UnLevel.cpp`
8. `Engine/Src/UnLevAct.cpp`
9. `Engine/Src/UnConn.cpp`
10. `Engine/Src/UnLevTic.cpp`
11. `UT99-Xbox/Tools/launch_xemu_syslink_pair.ps1`
12. `UT99-Xbox/Tools/poll_xemu_ram_log.py`

For Jedi Knight, first find the analogous files for:

- network startup
- gameplay listen/connect
- frontend System Link menu
- player spawn/login
- local splitscreen player creation
- server relevancy/visibility
- map travel
- input-to-local-player binding

Then port the flow deliberately, not line-for-line.
