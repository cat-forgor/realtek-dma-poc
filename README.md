# realtek poc

```
rt26cx21x64.sys exploit (Realtek PCIe GbE/2.5GbE/5GbE family)

OID 0xFF816871 maps the nic's mmio bar into the calling proc's virtual addr space, (MmLockedPagesSpecifyCache), reachable from um (\\.\RealTekCard{GUID}).

This IOCTL (0x0012C818) is gated by a fucking registry value lmfao (DrvMode=4)

Once we have mmio reg access, we program the nic's tx/rx descriptor rings in loopback mode to r/w arbitrary phys addrs,
(AllocateUserPhysicalPages gives us PFNs).
```

## Requirements

- A compatible realtek adapter which matches [`nic.h`](./src/nic.h).

## Build

```
$ cmake -B build
$ cmake --build build --config Release
```

## Notes

- I stripped most of the interesting stuff (cr3, stomping, etc, etc) but this is the base POC
