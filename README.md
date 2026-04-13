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

## Usage

Run as admin.

After first run, reg vals will be set and you will need to restart your pc.

For this to work, your NIC must be in a link UP state (so we can process descriptors).

Either:

- Plug in an ethernet cable to another pc, router or anything that will give link UP

OR

- Short pins 1-2 & 3-6 on the RJ45 port to bring the link UP

## Build

```
$ cmake -B build
$ cmake --build build --config Release
```

## Notes

- I stripped most of the interesting stuff (cr3, stomping, etc, etc) but this is the base POC
