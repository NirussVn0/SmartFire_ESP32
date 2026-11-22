# Build / Verification Status — 2026-11-22 (v1.0.1)

| Check | Result | Chi tiết |
|---|---|---|
| Pure C++ AlarmCore host tests | **PASS** | 15 scenarios, ASan + UBSan, LeakSanitizer disabled cho host limitation |
| ESP32 PlatformIO firmware build | **PASS** | `pio run -e esp32dev` biên dịch thành công 100% |
| BLE Provisioning GATT Service | **PASS** | Khởi tạo Service `4fafc201-1fb5-459e-8fcc-c5c9c331914b`, Config char write, Status char notify |
| SoftAP Web Recovery Portal | **PASS** | Trang cấu hình HTML nhúng tại `192.168.4.1`, xử lý form POST `/configure` |
| Flash NVS Persistence | **PASS** | Thư viện `Preferences` (namespace `"smartfire"`), đọc/ghi SSID & Password bền vững |
| Python manual API client syntax | **PASS** | `python3 -m py_compile tools/node_client.py` |
| Prebuilt app image checksum | **PASS** | esptool image_info |
| Wiring overview | **PASS** | Rendered và trực quan hóa chi tiết |
| Android App Integration (SmartFire AI v1.0.1) | **PASS** | Đã kết nối thử nghiệm BLE onboarding, quét status `/v1/status`, nhận diện `device_id` |
| Fire detection accuracy / ppm calibration | NOT MEASURED | Cần thiết bị phòng thí nghiệm chuyên dụng để hiệu chuẩn ppm |

Target: Original ESP32 classic, `esp32dev`, 4MB Flash, DIO 40MHz, Arduino framework, C++17.

Pinned platform: `espressif32 6.9.0`. Resolved packages:
- `framework-arduinoespressif32 3.20017.241212+sha.dcc1105b` (Arduino ESP32 2.0.17)
- `toolchain-xtensa-esp32 8.4.0+2021r2-patch5`
- `tool-esptoolpy 1.40501.0` (esptool 4.5.1)

Firmware v1.0.1 tích hợp toàn diện BLE GATT Server, SoftAP Captive Web Portal và NVS Flash storage, cho phép cấu hình linh hoạt mà không cần nạp lại mã nguồn.
