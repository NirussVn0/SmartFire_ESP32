# Build / verification status — 2026-10-25

| Check | Result |
|---|---|
| Pure C++ AlarmCore host tests | PASS — 15 scenarios, ASan + UBSan, LeakSanitizer disabled for host limitation |
| ESP32 default bench build | PASS — `python3 -m platformio run -d SmartFire_ESP32` |
| ESP32 Wi-Fi/API enabled compile | PASS — temporary dummy SSID/token only; not connected or flashed |
| Python manual API client syntax | PASS — `python3 -m py_compile tools/node_client.py` |
| Prebuilt app image checksum + validation hash | PASS — esptool image_info |
| Wiring overview | Rendered and visually inspected |
| Flash to physical ESP32 | NOT PERFORMED — no attached board |
| Physical wiring / sensor / horn / Wi-Fi tests | NOT PERFORMED |
| Live HTTP endpoint/phone integration | NOT PERFORMED |
| Fire detection accuracy / ppm calibration | NOT MEASURED |

Target: original ESP32, `esp32dev`, 4MB flash, DIO 40MHz, Arduino framework, C++17.

Pinned platform: espressif32 6.9.0. Resolved packages:

- framework-arduinoespressif32 3.20017.241212+sha.dcc1105b (Arduino ESP32 2.0.17)
- toolchain-xtensa-esp32 8.4.0+2021r2-patch5
- tool-esptoolpy 1.40501.0 (esptool 4.5.1)

Default bench build: 411269 bytes program / 1310720-byte app partition; 27892 bytes static RAM / 327680 bytes.
Wi-Fi-enabled compile: 789181 bytes program / 1310720-byte app partition; 47488 bytes static RAM / 327680 bytes.
These linker figures do not include all runtime heap/task/client allocations and do not prove runtime stability.

The temporary compile-test Secrets.h was removed. Packaged source and prebuilt image use NO configured Wi-Fi credentials, `kHornEnabled=false`, `kGasCommissioned=false`. Rebuild with your own configuration for commissioning. No real user credentials are in the package.

`docs/build_bench.log` contains the final default build output. The merged binary is produced from that final build, not from the temporary network-enabled compile.
