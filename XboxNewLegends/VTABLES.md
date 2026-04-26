# New Legends — Phase 2 Vtable / Method Table Reference

XBE base: 0x00010000  XDK: 4039  UE: 83/25

---

## 1. ImRenderDevice (URenderDevice subclass)

Two vtable pointers per object:
- `[obj+0x00]` → primary vtable @ **0x00255CF0** — 137 slots
- `[obj+0x28]` → secondary (base class) @ **0x00255CEC** — 138 slots (4 bytes before primary)

Init function: **0x00125300** (99 bytes)
- Sets both vtable pointers
- Calls 0x00036CB0 (ULinkerLoad/ctor, 146 bytes)
- Calls 0x00012000 (SEH-init, 79 bytes)
- Calls 0x0004A700 (pkg-loader, 86 bytes)
- **No D3D code in Init.** D3D creation is deferred to 0x000F3F00.

### Primary Vtable — 0x00255CF0 (137 slots)

| Slot | Offset | VA         | Notes |
|------|--------|------------|-------|
| 0    | +0x000 | 0x000373F0 | |
| 1    | +0x004 | 0x00011160 | trivial/empty |
| 2    | +0x008 | 0x00011160 | trivial/empty |
| 3    | +0x00C | 0x00125400 | thunk → Init (0x00125300) |
| 4    | +0x010 | 0x000270D0 | |
| 5    | +0x014 | 0x000111E0 | |
| 6    | +0x018 | 0x00011430 | trivial/empty |
| 7    | +0x01C | 0x00036F40 | |
| 8    | +0x020 | 0x00038E70 | |
| 9    | +0x024 | 0x0003B650 | |
| 10   | +0x028 | 0x00036FE0 | |
| 11   | +0x02C | 0x00039120 | |
| 12   | +0x030 | 0x0004B150 | trivial/empty |
| 13   | +0x034 | 0x00023D30 | |
| 14   | +0x038 | 0x00022330 | |
| 15   | +0x03C | 0x00037160 | |
| 16   | +0x040 | 0x0004B140 | trivial/empty |
| 17   | +0x044 | 0x00038E80 | |
| 18   | +0x048 | 0x00026D80 | |
| 19   | +0x04C | 0x00039960 | |
| 20   | +0x050 | 0x00040540 | |
| 21   | +0x054 | 0x00039E80 | |
| 22   | +0x058 | 0x000111E0 | |
| 23   | +0x05C | 0x00125370 | |
| 24   | +0x060 | 0x0004B140 | trivial/empty |
| 25   | +0x064 | 0x0018DDD0 | NYI stub → 0x00191360 |
| 26   | +0x068 | 0x00125380 | |
| 27-36| +0x06C–+0x090 | 0x0018DDD0 | NYI stubs |
| 37   | +0x094 | 0x00083360 | |
| 38   | +0x098 | 0x00083000 | |
| 39-40| +0x09C–+0x0A0 | 0x0018DDD0 | NYI stubs |
| 41   | +0x0A4 | 0x00083740 | |
| 42-49| +0x0A8–+0x0C4 | 0x0018DDD0 | NYI stubs |
| 50   | +0x0C8 | 0x00163190 | |
| 51   | +0x0CC | 0x00174F20 | |
| 52   | +0x0D0 | 0x00011440 | trivial/empty |
| 53-54| +0x0D4–+0x0D8 | 0x0004B140 | trivial/empty |
| 55-56| +0x0DC–+0x0E0 | 0x000111E0 | |
| 57   | +0x0E4 | 0x00011210 | |
| 58   | +0x0E8 | 0x00011160 | trivial/empty |
| 59   | +0x0EC | 0x0005D570 | |
| 60   | +0x0F0 | 0x00163180 | |
| 61   | +0x0F4 | 0x00011150 | trivial/empty |
| 62   | +0x0F8 | 0x0004B150 | trivial/empty |
| 63-66| +0x0FC–+0x108 | 0x0004B140 | trivial/empty |
| 67   | +0x10C | 0x00011160 | trivial/empty |
| 68   | +0x110 | 0x000111E0 | |
| 69-71| +0x114–+0x11C | 0x00011210 / 0x000111E0 | |
| 72   | +0x120 | 0x00163180 | |
| 73   | +0x124 | 0x001253B0 | |
| 74   | +0x128 | 0x0004B140 | trivial/empty |
| 75   | +0x12C | 0x000111E0 | |
| 76-77| +0x130–+0x134 | 0x0004B140 | trivial/empty |
| 78   | +0x138 | 0x000111E0 | |
| 79   | +0x13C | 0x00174F20 | |
| 80-81| +0x140–+0x144 | 0x000111A0 | |
| 82   | +0x148 | 0x00011160 | trivial/empty |
| 83   | +0x14C | 0x00163190 | |
| 84-85| +0x150–+0x154 | 0x00011210 | |
| 86-87| +0x158–+0x15C | 0x00163180 | |
| 88   | +0x160 | 0x00011210 | |
| 89   | +0x164 | 0x000111E0 | |
| 90   | +0x168 | 0x00163180 | |
| 91-92| +0x16C–+0x170 | 0x00011150 | trivial/empty |
| 93   | +0x174 | 0x001253C0 | |
| 94-95| +0x178–+0x17C | 0x000111E0 | |
| 96   | +0x180 | 0x001253F0 | thunk → 0x00125300 |
| 97   | +0x184 | 0x00046860 | |
| 98-99| +0x188–+0x18C | 0x0018DDD0 | NYI stubs |
| 100  | +0x190 | 0x00046850 | |
| 101  | +0x194 | 0x00011160 | trivial/empty |
| 102  | +0x198 | 0x000111A0 | |
| 103-111| +0x19C–+0x1BC | 0x0004B140 | trivial/empty |
| 112  | +0x1C0 | 0x000373F0 | |
| 113-114| +0x1C4–+0x1C8 | 0x00011160 | trivial/empty |
| 115  | +0x1CC | 0x001257E0 | thunk → 0x00125800 |
| 116  | +0x1D0 | 0x000270D0 | |
| 117  | +0x1D4 | 0x000111E0 | |
| 118  | +0x1D8 | 0x00011430 | trivial/empty |
| 119  | +0x1DC | 0x00036F40 | |
| 120  | +0x1E0 | 0x00038E70 | |
| 121  | +0x1E4 | 0x0003B650 | |
| 122  | +0x1E8 | 0x000BFAE0 | |
| 123  | +0x1EC | 0x00039120 | |
| 124  | +0x1F0 | 0x0004B150 | trivial/empty |
| 125  | +0x1F4 | 0x00023D30 | |
| 126  | +0x1F8 | 0x00022330 | |
| 127  | +0x1FC | 0x00037160 | |
| 128  | +0x200 | 0x0004B140 | trivial/empty |
| 129  | +0x204 | 0x00038E80 | |
| 130  | +0x208 | 0x00026D80 | |
| 131  | +0x20C | 0x00039960 | |
| 132  | +0x210 | 0x00040540 | |
| 133  | +0x214 | 0x00039E80 | |
| 134  | +0x218 | 0x001775C0 | |
| 135  | +0x21C | 0x00125680 | |
| 136  | +0x220 | 0x00125690 | last slot → calls 0x001AEE10 (D3D section) |

### D3D Initialization (NOT in Init — deferred to 0x000F3F00)

Function @ **0x000F3F00** contains all D3D setup:
1. Zero-fills two 0x200-byte `D3DPRESENT_PARAMETERS` structs on stack (EBP-0x23C and EBP-0x43C)
2. `GetAdapterDisplayMode` via CALL [reg+0x38] at 0x000F4126 — dynamic resolution
3. `CreateDevice` attempt 1 via CALL [EAX+0x3C] at **0x000F40C8**
4. On failure: `GetAdapterDisplayMode` again at 0x000F41AE
5. `CreateDevice` attempt 2 via CALL [EDX+0x3C] at **0x000F4147** with NULL hwnd (PUSH 53 53)
6. `CreateDevice` attempt 3 via CALL [EDX+0x3C] at **0x000F419F**

IDirect3D8* global: **[0x002961A4]** — written from exactly one site: 0x0003220C.

---

## 2. FMallocXBox (Memory Allocator)

Vtable @ **0x00260B28** — 7 slots  
GMalloc global @ **[0x00385198]**

Slab allocator: 49 size classes, large-alloc threshold 32769 → NtAllocateVirtualMemory.

| Slot | VA         | Name              | Size  |
|------|------------|-------------------|-------|
| 0    | 0x001732E0 | Malloc            | 495 B |
| 1    | 0x00173570 | Free              | 194 B |
| 2    | 0x00173700 | Realloc           | 252 B |
| 3    | 0x00173850 | Exec              | 279 B |
| 4    | 0x00173040 | HeapCheck         | 328 B |
| 5    | 0x00173190 | GetAllocationSize | 133 B |
| 6    | 0x0004B140 | (empty RET)       |   1 B |

### Method Signatures (UT99 FMalloc interface mapping)

```cpp
// slot 0: void* Malloc(DWORD Count, const TCHAR* Tag)
// slot 1: void  Free(void* Original)
// slot 2: void* Realloc(void* Original, DWORD Count, const TCHAR* Tag)
// slot 3: UBOOL Exec(const TCHAR* Cmd, FOutputDevice& Ar)   [dumps heap stats]
// slot 4: void  HeapCheck()                                  [validates slab chains]
// slot 5: DWORD GetAllocationSize(void* Original)            [NL-specific extension]
// slot 6: (padding / unused RET)
```

---

## 3. FFileManagerLinear (GFileManager)

No virtual dispatch vtable found — FFileManagerLinear appears non-polymorphic at call sites.

GFileManager global @ **[0x002C2F10]**  
Constructor @ **0x001728B0**

### Object Layout (from code trace)

| Offset | Value/Type | Description |
|--------|-----------|-------------|
| +0x04  | 83 (DWORD) | UE package version |
| +0x08  | 400 (DWORD) | unknown |
| +0x0C  | 25 (DWORD) | UE licensee version |
| +0x30  | HANDLE | active file handle |
| +0x38  | DWORD | bytes transferred |

appPlatformInit writes GFileManager @ 0x0004BAD8.

---

## 4. UAudioSubsystem

No vtable found in .rdata for audio subsystem.

DSOUND section: VA **0x001E2C60**  
Audio tick function: **0x0004B040** — called every frame from main loop.

DirectSound initialization and buffer management is embedded in the DSOUND section (external static lib, not reconstructable from NL .text symbols). The NL game uses DirectSound for all audio. Our XboxAudio stub is correct for the UT99 Xbox port since DirectSound is not available on XDK — use XAudio/xact instead.

---

## 5. XboxClient / UViewport subclass

`CreateViewport` @ **0x0016C1E0** — 459 bytes  
Vtable slot used inside CreateViewport: CALL [EDX+0x7C] (slot 31)

CreateViewport calls logging helper 0x0002F8B0 (debugf variant) multiple times.

No separate vtable pointer decoded for XboxClient — the viewport is created through the client factory, not directly. The UViewport interface is called via slot 31 of the primary ImRenderDevice vtable for resize/mode changes.

---

## Key Globals Summary

| Symbol | VA |
|--------|-----|
| GMalloc | [0x00385198] |
| GFileManager | [0x002C2F10] |
| IDirect3D8* | [0x002961A4] |
| GLog | [0x002C512C] |
| GIsRunning | [0x002C5180] |
| appInit guard | [0x002C5128] |
| tick_frequency | [0x00296440] |

## Key Function Summary

| Name | VA |
|------|----|
| appInit guard | 0x00037290 |
| appInit body | 0x000372A0 |
| appPlatformInit | 0x0004BAD8 area |
| FFileManagerLinear::ctor | 0x001728B0 |
| ImRenderDevice::Init | 0x00125300 |
| D3D setup function | 0x000F3F00 |
| FMallocXBox::Malloc | 0x001732E0 |
| FMallocXBox::GetAllocationSize | 0x00173190 |
| Audio tick | 0x0004B040 |
| XboxClient::CreateViewport | 0x0016C1E0 |
| debugf wrapper | 0x0002F910 |
| logf wrapper | 0x0002F8B0 |
