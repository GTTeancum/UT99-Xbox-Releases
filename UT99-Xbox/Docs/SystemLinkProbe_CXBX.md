# UT99 Xbox System Link Probe

This is a first-pass connectivity probe, not the final Unreal gameplay net driver.
It proves whether two Xbox/CXBX-R instances can see each other over UDP broadcast.

## What It Does

- Opens a small UDP socket when you enter `SYSTEM LINK`.
- Binds to the first available port in `9777` through `9780`.
- Broadcasts a `UTXSL1` packet once per second to all four probe ports.
- Lists every other instance that replies with the same probe packet.
- Writes status to `D:\ut99.log` as `XSL probe ...`.

## CXBX-R Setup

1. Create two separate CXBX-R game folders.
   - Example:
     - `C:\Games\Emulators\CXBX\UT99x_A`
     - `C:\Games\Emulators\CXBX\UT99x_B`
2. Copy the whole UT99 Xbox install into both folders.
3. Put the new `default.xbe` in both folders.
4. Open two CXBX-R instances.
5. In each instance, load its own copy of `default.xbe`.
6. In both instances, press `Start`, then open `SYSTEM LINK`.

## Expected Result

Each instance should show:

```text
SYSTEM LINK TEST
DISCOVERY PROBE
LOCAL ID  XXXXXXXX    PORT 9777/9778/9779/9780
PEERS FOUND  1
```

The peer row should show the other instance's ID, IP address, port, and packet count.
The packet count should keep increasing.

The log should contain lines like:

```text
XSL probe net init xnet=0 wsa=0 ready=1
XSL probe started id=0x12345678 port=9777
XSL peer discovered id=0x87654321 addr=192.168.x.x port=9778
XSL probe status id=0x12345678 port=9777 peers=1 sent=10 lastErr=0
```

## If It Does Not Find Peers

- Make sure both CXBX-R instances are running at the same time.
- Make sure both are on the `SYSTEM LINK` screen.
- Make sure the two instances are not launching from the exact same folder.
- Check whether Windows Firewall prompts for CXBX-R network access.
- Check `ut99.log` for:
  - `XSL probe net init ... ready=0`
  - `XSL probe socket failed`
  - `XSL probe bind failed`

## Caveat

This confirms local LAN packet discovery only. The final System Link implementation
still needs proper Xbox secure-session key exchange using `XNetCreateKey`,
`XNetRegisterKey`, `XNetXnAddrToInAddr`, and the Unreal net driver path.
