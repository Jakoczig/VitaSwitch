# Building and testing VitaSwitch

Use the complete repository, including its original `sce_sys` artwork. A source
or documentation overlay alone is not a complete build tree.

## VitaSDK build

Install VitaSDK, set `VITASDK` to its installation directory, and add its `bin`
directory to `PATH`. Record the exact SDK release used for a published build.

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake"
cmake --build build
```

The current target produces `build/vita-switcher.self` and
`build/vita-switcher.vpk`. The VPK must retain title ID `SWCH00001` and use
application version `01.22` for release 1.22.

The LiveArea template also displays `v1.22`. Keep the CMake application version
and the visible LiveArea label in sync when preparing future releases.

## Host regression tests

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic \
  -I tests/mock_include main.c tests/mock_vita.c -o tests/vitaswitch_host
python3 tests/test_switch.py
```

The existing GitHub Actions workflow additionally compiles with AddressSanitizer
and UndefinedBehaviorSanitizer. Its flags are recorded in
`.github/workflows/host-tests.yml`.

The host harness substitutes filesystem and reboot calls. Passing these tests
is not the same as compiling or running a PS Vita executable.
