# LED Test Harness (CMake)

This directory provides a standalone CMake-based test harness for development and CI. Production builds of the tools do not depend on CMake; this harness is strictly for testing.

## Prerequisites
- CMake 3.16+
- A C++ compiler (MSVC, MinGW, or Clang)
- Internet access during configure (to fetch GoogleTest)

## Configure & Build (MSVC)
```powershell
mkdir led\tests\build
cmake -S led\tests -B led\tests\build -G "Visual Studio 17 2022" -A x64
cmake --build led\tests\build --config Debug
ctest --test-dir led\tests\build --config Debug --output-on-failure
```

## Configure & Build (Ninja + MSVC)
```powershell
mkdir led\tests\build
cmake -S led\tests -B led\tests\build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build led\tests\build
ctest --test-dir led\tests\build --output-on-failure
```

## Notes
- Tests link to the project C source (`led/led.c`) and include headers from `led/`.
- GoogleTest is fetched automatically via `FetchContent`. If you need offline builds, prefetch the archive and adjust the URL.
- This harness does not modify or require the production build system.
