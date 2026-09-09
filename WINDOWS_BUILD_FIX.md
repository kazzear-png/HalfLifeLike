# Windows build recovery

The supplied log shows DiffuseGI.cpp compiling, engine.lib building, and
sandbox.exe linking. ALL_BUILD failed during repeated concurrent CMake
regeneration, inside GLFW FetchContent, rather than C++ compilation. The
missing VS Code CMake API JSON follows those interrupted regenerations.

The previous archive stamped engine/CMakeLists.txt 2026-09-09 22:38.
A source timestamp ahead of the PC clock can keep regeneration out of date;
the log alone cannot establish the PC clock or confirm that cause. This
archive uses fixed past timestamps to avoid that trigger on fresh extraction.
CMake 3.30+ now uses CMP0168 NEW (direct dependency population, no GLFW
population sub-build). CMP0135 NEW uses extraction-time dependency timestamps
on CMake versions supporting it. Automatic regeneration remains enabled.

## Build

Stop any active build/configure in VS Code. Extract this archive into a NEW
folder, then open its GameProject-realtime folder. Do not overlay an existing
build directory: fixed archive timestamps can otherwise leave old objects
newer than replacement sources.

In a Visual Studio Developer PowerShell, from that source folder:

```powershell
cmake -S . -B build-vs2026-x64 -G "Visual Studio 18 2026" -A x64 -T host=x64
cmake --build build-vs2026-x64 --config Release --parallel 12
ctest --test-dir build-vs2026-x64 -C Release --output-on-failure
.\build-vs2026-x64\bin\Release\sandbox.exe
```

Run these commands sequentially; do not start an IDE configure simultaneously.
Use the new build directory in VS Code and select Release. The old log used
Win32 Debug; use x64 Release for performance measurements. The old build folder
can remain untouched. If cmake is unavailable in your terminal, open Developer
PowerShell from Visual Studio (the log's bundled cmake.exe also works).

## Validation scope

Archive contents and timestamps were checked. No rendering source changed in
this repair. Windows Visual Studio/CMake are unavailable in the repair
environment, so these Windows commands have not been run here. Previous CPU
and shader validation results remain documented in REALTIME_GI.md.

CMake policy references:
- https://cmake.org/cmake/help/latest/policy/CMP0168.html
- https://cmake.org/cmake/help/latest/policy/CMP0135.html
