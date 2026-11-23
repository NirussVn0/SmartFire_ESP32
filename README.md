# SmartFire ESP32 — Firmware Cảnh Báo Cháy 2 Mức & OTA Wi-Fi (`v2.2.0-2level-sensors`)

> **Sản phẩm STEM Nhóm 2 · Lớp 12D3 · SOV Labs**  
> **Vi điều khiển:** ESP32-WROOM-32 (Dual-Core FreeRTOS, C++17, PlatformIO / Arduino)  
> **Ứng dụng điều khiển tương thích:** [`SmartFire AI`](https://github.com/NirussVn0/Smartfire-AI) (Android Native)  
> **Giấy phép:** [MIT License](LICENSE)

---

## 1. Nguyên Tắc Thiết Kế Cốt Lõi

> **"Embedded firmware runs directly on the ESP32 and keeps working without an app or Wi-Fi network."**

- **Độc lập phần cứng:** Toàn bộ logic phát hiện cháy (`AlarmCore`), lọc mẫu và điều khiển còi 12V / LED chạy trên vòng lặp sensing riêng (`loop()`), hoàn toàn không bị block bởi Wi-Fi hay HTTP. Ngay cả khi mất Wi-Fi hoặc tắt điện thoại, **ESP32 vẫn tự chủ hú còi khi có cháy**.
- **Hỗ trợ Wi-Fi + BLE song song ổn định:** Sử dụng `WiFi.setSleep(WIFI_PS_MIN_MODEM)` tương thích chuẩn Wi-Fi/BLE coexistence trên ESP-IDF (tránh lỗi `coex_core_enable`), công suất phát tối đa `WIFI_POWER_19_5dBm`, lọc mượt `wifi_rssi` bằng EMA + spike clamp và đếm `wifi_reconnects`.

---

## 2. Tính Năng Chính Trong `v2.2.0-2level-sensors`

| Thành phần | Chi tiết kỹ thuật |
|---|---|
| **MQ-2 (Khí cháy / Khói)** | ADC1 `GPIO34`, lấy mẫu trung bình 8 lần mỗi `50ms`, phát hiện lỗi rail `<=5` hoặc `>=4090` |
| **Flame Sensor (Lửa IR)** | Digital `GPIO27` (`active-low` mặc định), xác nhận độc lập theo 2 mức thời gian (`flame_warn_ms` / `flame_alarm_ms`) |
| **Nhiệt độ (Tùy chọn mở rộng)** | Hỗ trợ ngưỡng 2 mức `temp_warn_c` / `temp_alarm_c` và cờ `temp_enabled` trong `AlarmCore` (mặc định trả `temperature_celsius: null` nếu chưa gắn module nhiệt) |
| **Phân cấp 2 mức & Bật/Bỏ qua từng cảm biến** | **Mức 1 (`SUSPECTED`)** cảnh báo sớm và **Mức 2 (`ALARM`)** báo cháy khẩn cấp; hỗ trợ bật/tắt bỏ qua riêng từng cảm biến (`mq2_enabled`, `temp_enabled`, `flame_enabled`) |
| **Còi 12V & Nút nhấn vật lý** | Còi `GPIO26` qua MOSFET D4184 (khóa an toàn mặc định `kHornEnabled = false`, bật từ App); nút `GPIO18` nhấn ngắn = Silence 30s, giữ 2s = Test còi 1s |
| **Cấu hình từ xa qua Wi-Fi (`/v1/config`)** | `GET/POST /v1/config` nhận JSON từ App Android và lưu bền vững vào Flash NVS (namespace `sf_cfg`) |
| **Nạp Code Không Dây Wi-Fi OTA (`/v1/ota`)** | `POST /v1/ota` nhận file `firmware.bin` qua HTTP multipart (xác thực Bearer Token), flash bằng `Update.h` và tự reboot — không cần rút cắm USB hay bấm nút `BOOT` sau lần nạp đầu |
| **BLE Provisioning & SoftAP Portal** | BLE GATT `4fafc201-1fb5-459e-8fcc-c5c9c331914b` + Captive Portal `SmartFire-Setup-XXXX` (`192.168.4.1`) lưu Wi-Fi vào NVS |

---

## 3. Sơ Đồ Chân Mặc Định (`include/Config.h`)

| Chân ESP32 | Chức năng | Ghi chú |
|---|---|---|
| `GPIO34` | Cảm biến khí/khói MQ-2 (Analog) | Qua mạch chia áp an toàn về ≤ 3.3V |
| `GPIO27` | Cảm biến ngọn lửa Flame (DO) | `INPUT_PULLUP`, mặc định `kFlameActiveLow = true` |
| `GPIO26` | Điều khiển MOSFET D4184 (Còi 12V) | `kHornActiveHigh = true`, mặc định khóa `kHornEnabled = false` |
| `GPIO18` | Nút nhấn vật lý Silence / Test | `INPUT_PULLUP` (nhấn ngắn = Silence, giữ ≥ 2s = Test) |
| `GPIO25` | LED trạng thái hệ thống | Nháy nhịp bình thường / sáng liên tục khi báo động |

---

## 4. API HTTP LAN (`Port 80`, Bearer Token ≥ 24 ký tự)

Mọi endpoint `/v1/*` yêu cầu header `Authorization: Bearer <kApiToken>`:

- `GET /v1/status` — Trả về trạng thái realtime, `wifi_rssi`, `wifi_rssi_raw`, `wifi_reconnects`, cấu hình 2 mức và phiên bản firmware (`firmware_version`).
- `GET /v1/events?since=<seq>` — Lấy lịch sử tối đa 32 sự kiện gần nhất từ vòng đệm RAM.
- `GET /v1/config` — Đọc cấu hình 2 mức & bật/tắt cảm biến đang lưu trong NVS (`sf_cfg`).
- `POST /v1/config` — Cập nhật cấu hình qua Wi-Fi và lưu vào NVS (`mq2_enabled`, `gas_off`, `gas_warn`, `gas_on`, `temp_enabled`, `temp_warn_c`, `temp_alarm_c`, `flame_enabled`, `flame_warn_ms`, `flame_alarm_ms`, `horn_enabled`, `gas_commissioned`, `tx_power_1m`, `path_loss_n`).
- `POST /v1/ota` — Nạp file `firmware.bin` mới qua Wi-Fi (multipart field `firmware`).
- `POST /v1/commands/ack` · `/v1/commands/silence` · `/v1/commands/test` — Gửi lệnh điều khiển bất đồng bộ tới `AlarmCore`.

---

## 5. Cài Đặt, Biên Dịch & Nạp Firmware

### Bước 1: Tạo file bí mật `include/Secrets.h`
Copy `include/Secrets.example.h` thành `include/Secrets.h` (file `Secrets.h` đã được `.gitignore`, không đẩy lên GitHub) và điền `kDeviceId`, `kRoomId`, `kApiToken` (≥ 24 ký tự).

### Bước 2: Biên dịch & Nạp qua USB (lần đầu tiên)
```powershell
cd SmartFire_ESP32

# Biên dịch tạo .pio/build/esp32dev/firmware.bin
pio run -e esp32dev

# Nạp qua USB (nếu board không tự vào download mode, nối IO0-GND hoặc giữ BOOT khi reset)
pio run -e esp32dev -t upload

# Xem Serial Monitor 115200 baud
pio device monitor -b 115200
```

### Bước 3: Nạp OTA qua Wi-Fi (từ các lần sau — không cần USB / nút BOOT)
- **Từ App Android SmartFire AI:** Vào **Cài đặt → Cập Nhật Code ESP32 Qua Wi-Fi (OTA `.bin`) → Chọn file `.bin` → PUSH OTA**.
- **Hoặc từ máy tính (PowerShell):**
```powershell
curl.exe -X POST "http://<IP_ESP32>/v1/ota" `
  -H "Authorization: Bearer <API_TOKEN>" `
  -F "firmware=@.pio\build\esp32dev\firmware.bin"
```

---

## 6. Tài Liệu Kỹ Thuật Chi Tiết

1. [Sơ đồ đấu dây & Kiểm tra điện áp](docs/01_WIRING_VI.md)
2. [Cài đặt công cụ, Biên dịch & Nạp Flash](docs/02_FLASH_AND_COMMISSION_VI.md)
3. [Kiến trúc phân tầng & Vòng lặp điều khiển](docs/03_ARCHITECTURE_VI.md)
4. [Đặc tả API HTTP LAN v1](docs/04_API_VI.md)
5. [Quy trình kiểm thử & Nghiệm thu](docs/05_VERIFICATION_VI.md)
6. [Kiến trúc BLE Provisioning & Đồng bộ](docs/06_BLE_PROVISIONING_SYNC_VI.md)

---

## 7. An Toàn Thử Nghiệm & Giấy Phép

- Đây là mô hình nghiên cứu STEM / KHKT THPT, chưa qua kiểm định PCCC công nghiệp; không để lửa thật không người trông coi hoặc tạo khói độc trong phòng kín.
- Phát hành theo giấy phép **MIT License** — xem chi tiết tại file [`LICENSE`](LICENSE).
