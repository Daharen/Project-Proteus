Branch and build quickstart
===========================

1) Build (Windows x64, VS2022)
------------------------------
- Install vcpkg dependencies:
  vcpkg install sqlite3:x64-windows openssl:x64-windows
- Configure:
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake ^
    -DVCPKG_TARGET_TRIPLET=x64-windows
- Build:
  cmake --build build --config Release

2) Migrate DB (canonical)
-------------------------
- Run migrations explicitly:
  .\build\Release\proteus.exe --migrate --db .\data\proteus.db
- Expected success output:
  MIGRATE_OK version=6

3) CLI query example
--------------------
- Query command:
  .\build\Release\proteus.exe --domain demo --prompt "hello" --db .\data\proteus.db

4) Run web server (canonical)
-----------------------------
- Serve UI + API:
  .\build\Release\proteus.exe serve --host 127.0.0.1 --port 8080 --db .\data\proteus.db --static_root .\web
- Open browser:
  http://127.0.0.1:8080/

5) Serve smoke test
-------------------
- Start + bind probe + clean shutdown:
  .\build\Release\proteus.exe serve --smoke --db .\data\proteus.db
- Expected success output includes:
  SMOKE_OK

Troubleshooting
---------------
- "connection refused": server is not running or bind failed; inspect startup diagnostics and bind error code.
- "no such column stable_player_id": migrations were not applied on an older DB. Use `--migrate` (all runtime open paths now auto-migrate to latest schema).
