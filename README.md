# SmartFire ESP32 — firmware v1.0.1
### Hệ thống Cảm biến Cảnh báo Cháy sớm & Điều khiển Còi Báo động
**Phiên bản:** `v1.0.1` (Cập nhật: 11/2026)  
**Nền tảng vi điều khiển:** ESP32-WROOM-32 (Classic 30 chân), C++17, FreeRTOS Dual-Core  
**Ứng dụng quản trị tương thích:** SmartFire AI Android Native (`v1.0.1`)

---

## 1. NGUYÊN TẮC THIẾT KẾ CỐT LÕI

> **"Embedded firmware runs directly on the ESP32 and keeps working without an app or Wi-Fi network."**

Firmware nhúng được biên dịch và nạp trực tiếp vào bộ nhớ Flash của vi điều khiển ESP32, tự khởi chạy độc lập khi cấp điện. Máy tính chỉ cần thiết khi nạp chương trình (flash) hoặc gỡ lỗi (debug Serial). Trong trường hợp mất kết nối Wi-Fi, hỏng router hoặc điện thoại hết pin, **ESP32 vẫn hoàn toàn tự chủ quyết định kích hoạt còi báo động 12V tại chỗ** khi phát hiện khói gas hoặc lửa.

Bộ dự án này được thiết kế chuyên biệt cho **Mô hình nghiên cứu STEM / KHKT THPT** của Nhóm 2, Lớp 12D3.

---

## 2. CÓ GÌ TRONG PHIÊN BẢN v1.0.1?

| Chức năng | Trạng thái v1.0.1 | Chi tiết kỹ thuật |
|---|---|---|
| **Đọc cảm biến khí MQ-2** | Đã hoàn thiện | ADC1 GPIO34, lấy mẫu trung bình 8 lần (50ms/mẫu), lọc nhiễu |
| **Đọc cảm biến lửa Flame Sensor** | Đã hoàn thiện | Digital Output (DO) GPIO27, xác nhận tín hiệu liên tục 500ms |
| **Máy trạng thái cảnh báo (AlarmCore)** | Đã hoàn thiện | C++ thuần, Hysteresis chống rung, ưu tiên ALARM cao nhất, tự phục hồi an toàn |
| **Điều khiển còi hú 12V qua MOSFET** | Đã hoàn thiện | GPIO26 qua D4184; tích hợp Bench Interlock (`kHornEnabled`) chống ồn khi thử bàn |
| **Nút nhấn vật lý đa năng** | Đã hoàn thiện | GPIO18: Nhấn nhanh = Tạm tắt còi (Silence 30s), Nhấn giữ 2s = Thử còi (Test 1s) |
| **Cấu hình Wi-Fi qua BLE (Provisioning)** | **MỚI (v1.0.1)** | BLE GATT Service `4fafc201-1fb5-459e-8fcc-c5c9c331914b`, ghi SSID/Pass JSON, notify IP |
| **Trang cấu hình khẩn cấp (SoftAP Portal)** | **MỚI (v1.0.1)** | Khi mất Wi-Fi, tự phát mạng `SmartFire-Setup-XXXX`, Web portal tại `192.168.4.1` để cài Wi-Fi |
| **Lưu cấu hình Wi-Fi vào Flash NVS** | **MỚI (v1.0.1)** | Sử dụng thư viện `Preferences` (namespace `"smartfire"`), giữ cấu hình khi cúp điện |
| **Giao tiếp HTTP REST qua LAN** | Đã hoàn thiện | Port 80, xác thực bắt buộc `Authorization: Bearer <TOKEN>` $\ge$ 24 ký tự |
| **Đồng bộ sự kiện lịch sử** | Đã hoàn thiện | Bộ đệm xoay RAM 32 sự kiện, đánh dấu `historyGap` khi quá tải |
| **Định danh node phân tán** | Đã hoàn thiện | `device_id`, `room_id`, `boot_id` ngẫu nhiên sau mỗi lần khởi động, số thứ tự `seq` |
| **Ứng dụng điều khiển Android** | **ĐÃ HOÀN THIỆN** | **SmartFire AI v1.0.1 Native** (Bản đồ 2D, Dijkstra tìm lối thoát, AI Gemini, Local SQLite) |

> **Lưu ý an toàn về cảm biến:** Giá trị MQ-2 trả về là số đếm ADC thô `[0..4095]`, không tự quy đổi sang ppm khi chưa có buồng khí chuẩn. Firmware không sinh nhiệt độ ảo (`"temperature_celsius": null`).

---

## 3. TÀI LIỆU CHI TIẾT THEO THỨ TỰ ĐỌC

1. [Sơ đồ đấu dây & Kiểm tra điện áp](docs/01_WIRING_VI.md) — Chi tiết chân nối LM2596, ESP32, MQ-2, Flame và MOSFET D4184.
2. [Cài đặt công cụ, Biên dịch, Nạp Flash & Thử bàn](docs/02_FLASH_AND_COMMISSION_VI.md) — Hướng dẫn cài VS Code, PlatformIO và cấu hình ban đầu.
3. [Kiến trúc phân tầng & Vòng lặp điều khiển](docs/03_ARCHITECTURE_VI.md) — Phân tách trách nhiệm AlarmCore, Hardware và Transport Dual-Core.
4. [Đặc tả API HTTP LAN v1](docs/04_API_VI.md) — Cấu trúc JSON endpoints `/v1/status`, `/v1/events`, `/v1/commands/*`.
5. [Quy trình kiểm thử & Trạng thái nghiệm thu](docs/05_VERIFICATION_VI.md) — Kịch bản thử nghiệm còi, lửa, mất kết nối và chẩn đoán lỗi.
6. [Kiến trúc Cấu hình BLE & Đồng bộ](docs/06_BLE_PROVISIONING_SYNC_VI.md) — Chi tiết UUID, cấu trúc gói tin GATT và mã mẫu tích hợp Android.

---

## 4. CÁCH CẤU HÌNH THIẾT BỊ (3 PHƯƠNG THỨC)

Trong phiên bản **v1.0.1**, người dùng không cần sửa lại code để đổi Wi-Fi:

### Phương thức 1: Cấu hình qua Ứng dụng SmartFire AI (Khuyên dùng)
1. Cấp nguồn cho ESP32.
2. Mở ứng dụng Android **SmartFire AI** $\rightarrow$ Tab **Cảm biến** $\rightarrow$ Bấm **'+'** $\rightarrow$ Chọn **Quét BLE**.
3. Chọn thiết bị có tên dạng `SmartFire-Node-01` $\rightarrow$ Nhập tên Wi-Fi và Mật khẩu.
4. ESP32 nhận thông tin qua BLE, lưu vào NVS Flash, kết nối router và thông báo địa chỉ IP về app qua BLE Notification.

### Phương thức 2: Cấu hình qua Trình duyệt Web (SoftAP Captive Portal)
1. Khi ESP32 chưa có Wi-Fi hoặc không kết nối được router sau 15 giây, thiết bị tự phát Wi-Fi tên: **`SmartFire-Setup-XXXX`** (không mật khẩu).
2. Dùng điện thoại hoặc laptop kết nối vào Wi-Fi này $\rightarrow$ Mở trình duyệt truy cập: **`http://192.168.4.1/`**.
3. Nhập SSID và Password trên giao diện Web $\rightarrow$ Nhấn **"Lưu & Kết Nối"**.

### Phương thức 3: Cấu hình cứng qua mã nguồn (Dành cho Developer)
- Copy `include/Secrets.example.h` thành `include/Secrets.h`.
- Khai báo SSID, Mật khẩu, Token tĩnh, Device ID và Room ID.
- Nạp lại firmware bằng PlatformIO.

---

## 5. BẮT ĐẦU NHANH VỚI PLATFORMIO

```sh
# 1. Di chuyển vào thư mục firmware
cd SmartFire_ESP32

# 2. Biên dịch firmware
pio run -e esp32dev

# 3. Nạp firmware vào ESP32 qua cổng USB (giữ nút BOOT nếu mạch không tự reset)
pio run -e esp32dev -t upload

# 4. Mở Serial Monitor để theo dõi log
pio device monitor -b 115200
```

---

## 6. GIỚI HẠN PHẦN CỨNG & AN TOÀN

Đây là mô hình nghiên cứu khoa học kỹ thuật dành cho học sinh THPT, chưa qua kiểm định PCCC công nghiệp. Khi thử nghiệm:
- Luôn thử nghiệm với các tín hiệu an toàn (nút nhấn Test, ngắt kết nối dây mô phỏng, bật lửa gas ở cự ly an toàn).
- Tuyệt đối không để lửa thật không người trông coi hoặc tạo khói độc trong phòng kín.
- Mặc định cờ `kHornEnabled = false` trong `Config.h` để tránh còi hú bất ngờ trong quá trình phát triển trên bàn học.
