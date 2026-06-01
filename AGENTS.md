# obs-srtla-sender-plugin

Parent: [`../AGENTS.md`](../AGENTS.md)

OBS Studio plugin (Linux only) that runs `srtla_send` inside OBS for bonded SRTLA streaming. C++/CMake/Qt6. Bidirectional sync between OBS stream settings and SRTLA config.

---

## ROLE IN THE GROUP

Desktop tool. **NOT in the device image.** Not listed in `fetch-debs.sh` REPOS — never fetched, built, or packaged by `image-building-pipeline`. Deployed separately on the streamer's desktop/laptop running OBS.

Runtime dep: `srtla_send` binary must be present at `/usr/bin/srtla_send` (from `ceralive/srtla`).

---

## WHERE TO LOOK

| Task | Location |
|------|----------|
| Plugin entry point / OBS integration | `src/obs-srtla-sender-plugin.cpp` |
| SRTLA sender management logic | `src/srtla-sender.cpp` / `.h` |
| Network interface detection | `src/network-monitor.cpp` |
| CMake build config | `CMakeLists.txt` |
| Qt6 UI forms | `src/*.ui` |

---

## BUILD DEPS

Requires both sibling repos built and installed first:

1. `ceralive/srt` — BELABOX fork of libsrt (NOT standard Haivision). Must be installed via `sudo make install`.
2. `ceralive/srtla` — provides `srtla_send` binary; install to `/usr/bin/srtla_send`.

Then:
```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

OBS 28.0.0+ and Qt6 required. Ubuntu 20.04+ tested.

---

## RUNTIME

- Spawns `/usr/bin/srtla_send` as a subprocess when streaming starts.
- Auto-detects active network interfaces (Ethernet, WiFi, cellular) for bonding.
- Supports fixed or random local ports, custom SRT latency, stream ID auth.

---

## ANTI-PATTERNS

- Don't link against system/Haivision libsrt — must use `ceralive/srt` fork.
- Don't assume Windows/macOS compatibility — Linux only.
- Don't add this to `image-building-pipeline/fetch-debs.sh` REPOS.
