# Building Platform

The Platform tree is an independent CMake project and does not build or link the legacy runtime.

## Requirements

- Windows with WSL2 and Ubuntu 24.04 LTS (canonical development environment)
- CMake 3.20 or newer
- GCC with C++23 support (GCC 13.3 is the currently verified compiler)
- Ninja
- Python 3 only when using the ns-3 wrapper/build workflow

Keep both the source tree and build tree on the WSL Linux filesystem. All examples below use out-of-source build directories; do not generate build artifacts inside `Platform/`.

## Configure and test without ns-3

```sh
cmake -S Platform -B /path/to/build/platform -G Ninja \
  -DBUILD_TESTING=ON \
  -DPLATFORM_ENABLE_NS3=OFF
cmake --build /path/to/build/platform
ctest --test-dir /path/to/build/platform --output-on-failure
```

Disable all tests with `-DBUILD_TESTING=OFF`.

The repository does not download a test framework. The initial build smoke test uses CTest and a standalone C++23 executable.

GoogleTest is the recommended candidate for later unit tests, but its acquisition method is not frozen and this skeleton does not fetch or install it.

## Build with ns-3.47 core

ns-3 is disabled by default. Build and install the official ns-3.47 source release separately to a user-local prefix, such as `$HOME/.local/ns3/3.47`. Do not copy the ns-3 source into `Platform/`.

Point CMake at the installation prefix using `CMAKE_PREFIX_PATH`:

```sh
cmake -S Platform -B /path/to/build/platform-ns3 -G Ninja \
  -DBUILD_TESTING=ON \
  -DPLATFORM_ENABLE_NS3=ON \
  -DCMAKE_PREFIX_PATH="$HOME/.local/ns3/3.47"
cmake --build /path/to/build/platform-ns3
ctest --test-dir /path/to/build/platform-ns3 --output-on-failure
```

When an exact ns-3.47 package exporting `ns3::core` is found, CMake adds `platform_ns3_kernel_smoke_test`. The smoke test uses only `ns3::Simulator`, `ns3::Time`, and `ns3::EventId` from the core module.

When ns-3.47 is unavailable, configuration remains possible, the ns-3 smoke target is disabled with a clear diagnostic, and no replacement scheduler is introduced.

`PLATFORM_NS3_DIR` remains available as a cache input when the directory containing `ns3Config.cmake` must be specified directly. Neither it nor `CMAKE_PREFIX_PATH` may be hard-coded to a developer-specific absolute path in repository files.

## Dependency boundaries

- Only `kernel/` may use ns-3 scheduler APIs.
- Module targets are independent and use the `platform_<module>` naming scheme.
- CMake aliases use the `Platform::<module>` namespace.
- No aggregate target implicitly links every module.
- No Platform target links the legacy `src/main.cpp` or legacy runtime objects.

The exact compiler patch version and CI provider remain TBD. The frozen P0 baseline is Ubuntu 24.04, GCC, CMake, Ninja, C++23, and ns-3.47; Platform does not install or download ns-3.
