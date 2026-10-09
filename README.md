# VitaSwitch

VitaSwitch is a PS Vita homebrew utility that switches the **taiHEN plugin
configuration**, **PSVshell profiles**, and **VitaGrafix configuration** between
portable and docked presets. The program deliberately has no in-app UI: after
a successful switch, it requests a reboot. **Version 1.22** focuses on safe
file handling and crash recovery; it has **not yet been validated on real Vita
hardware**.

## Before using it

1. **Back up** `ur0:tai/config.txt`, `ur0:data/PSVshell/profiles/`, and
   `ux0:data/VitaGrafix/config.txt` (for whichever components you use). Keep a
   copy somewhere off the PS Vita. VitaShell or a similar file manager can do this.
2. Install the VPK and launch it once for first-run setup. It creates portable
   and docked backups **without replacing existing profiles**, then exits.
3. Edit your docked or portable configuration as desired, or use AutoPlugin 2
   while the corresponding mode is active.
4. Launch VitaSwitch again to switch modes. It updates the mode marker and
   requests a cold reboot only after the configuration transaction commits.

The current mode is recorded in `ur0:tai/switchstate.txt` (`0` = portable,
`1` = docked). The existing `ur0:tai/switchconf.txt` setup marker is retained
for compatibility with older installations, but plugin presence is checked on
each switch rather than using its old cached values. Installing PSVshell or
VitaGrafix after VitaSwitch setup is supported; the first switch will initialize
missing mode profiles from the plugin's current configuration.

## How version 1.22 protects configurations

- Reads and writes check error returns; short writes are retried.
- Copies are staged and completed before the original active configuration is
  renamed, and both mode backups are retained.
- The active and outgoing backups are moved aside under `.vsw-old` names until
  all configured components have switched and the mode marker has been written.
- `ur0:tai/vitaswitch.transaction` records the old state. A separate
  `vitaswitch.committed` marker makes post-commit cleanup resumable.
- If an operation fails before commit, VitaSwitch restores the original paths
  and **does not reboot**. On the next launch after an interrupted transaction,
  it attempts recovery and exits; launch it once more to switch.
- Fatal errors are written to `ur0:tai/vitaswitch-error.txt`.

### Important limits

A **multi-file or multi-directory switch is not a single atomic filesystem
operation**. An abrupt power cut can happen between moving the active
`ur0:tai/config.txt` to `ur0:tai/config.txt.vsw-old` and putting its replacement
in place. The original configuration should still exist under its recovery
name, but the missing active file may affect boot. If VitaSwitch cannot run,
**use VitaShell or another recovery method** to inspect the files, keep an
additional backup of everything, and restore the `.vsw-old` item to its original
name. Do not delete `.vsw-old` files before recovering. The transaction should
normally repair this on the next VitaSwitch launch if the app can start.

The program assumes the Vita supports exclusive file creation, directory/file
renames within one volume, and `sceIoSyncByFd`. These operations must be tested
on a real device before distributing the update widely. It intentionally fails
closed if it cannot establish a safe transition. The app still relies on the
`ur0:tai/config.txt` setup and does not automatically switch an `ux0:tai`
installation.

## Build

Install [VitaSDK](https://vitasdk.org/), set `VITASDK`, and add its `bin`
directory to `PATH`. Then build using CMake and the Vita toolchain:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake"
cmake --build build
```

The build emits a `.self` and `.vpk`. The source explicitly links
`SceIofilemgr_stub`, `SceKernelThreadMgr_stub`, and `ScePower_stub`.

## Host-side regression tests

Linux host tests use a Vita I/O compatibility shim with injected I/O errors and
simulated process crashes. They **do not replace VitaSDK compilation or physical
hardware testing**.

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  -fno-omit-frame-pointer -g -I tests/mock_include \
  main.c tests/mock_vita.c -o tests/vitaswitch_host
python3 tests/test_switch.py
```

## License

MIT License. See `LICENSE`.
