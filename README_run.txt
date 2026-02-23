Branch and build quickstart
===========================

1) Confirm branch selection
---------------------------
- List local branches and identify current branch:
  git branch
- Canonical clean baseline branch:
  git checkout codex/fix-merge-conflict-markers-in-repository
  git pull
- Topology-focused branch (if you want to test that work directly):
  git checkout codex/introduce-topologyseed-and-perturbation-surface
  git pull

2) Install SQLite3 with vcpkg (Windows x64)
--------------------------------------------
- Install package:
  vcpkg install sqlite3:x64-windows
- Set vcpkg root/toolchain in PowerShell:
  $env:VCPKG_ROOT="$Env:USERPROFILE\vcpkg"
  $toolchain="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"

3) Configure and build with CMake (Visual Studio 2022)
-------------------------------------------------------
- Configure from repository root:
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_TOOLCHAIN_FILE=$toolchain `
    -DVCPKG_TARGET_TRIPLET=x64-windows
- Build release:
  cmake --build build --config Release

4) Run tests and launch
-----------------------
- Run tests:
  ctest --test-dir build --build-config Release
- Launch server:
  .\build\Release\proteus.exe --serve --dev --port 8104 --static_dir .\web
- Open browser at:
  http://localhost:8104

5) Merge topology branch (optional)
-----------------------------------
- Verify clean working tree:
  git status
- Merge from canonical baseline:
  git checkout codex/fix-merge-conflict-markers-in-repository
  git merge codex/introduce-topologyseed-and-perturbation-surface
- Resolve conflicts, commit merge, then re-run configure/build/tests.
