# Từ chưa biết nhúng đến chạy node đầu tiên

**Build converts source code into firmware; flashing writes it to the ESP32.**  
Build (biên dịch) biến C++ thành mã máy cho ESP32. Flash (nạp) ghi mã đó vào bộ nhớ flash. Mỗi lần sửa Config/Secrets/code, build và upload lại; ESP32 tự chạy chương trình mới khi reset/cấp nguồn.

## Bước 1 — Xác định board và cáp

Dùng board ESP32-WROOM classic, không phải C3/S3/C6. Cáp USB phải truyền dữ liệu; cáp chỉ sạc không hiện cổng COM. Windows mở Device Manager kiểm tra Ports; chỉ cài CH340 driver từ nguồn nhà sản xuất/nhà bán đáng tin nếu thiếu. Linux kiểm tra quyền cổng serial theo distro. Chưa nối nguồn 12V/còi ở bước này.

## Bước 2 — Mở dự án

Cài VS Code, cài PlatformIO IDE, giải nén bộ này. File → Open Folder → chọn `SmartFire_ESP32` có `platformio.ini`, không mở thư mục cha của ZIP. Đợi PlatformIO cài platform/toolchain lần đầu; cần Internet. Framework Arduino dùng C++, không có nghĩa phải dùng Arduino IDE. Không mở riêng `main.cpp` rồi nhấn Run như chương trình PC.

`platformio.ini` đã khóa `espressif32@6.9.0`, `esp32dev`, Arduino framework. Giữ các phiên bản này để tái lập bản đầu; khi nâng framework, build và chạy lại test. Không cần cài thêm thư viện Arduino bên ngoài.

## Bước 3 — Secrets và cấu hình

Copy `include/Secrets.example.h` thành `include/Secrets.h`. Sửa:

```cpp
constexpr char kWifiSsid[] = "TEN_WIFI_2_4_GHZ";
constexpr char kWifiPassword[] = "MAT_KHAU_WIFI";
constexpr char kApiToken[] = "TOKEN_NGAU_NHIEN_CUA_RIENG_BAN";
constexpr char kDeviceId[] = "smartfire-node-01";
constexpr char kRoomId[] = "room-101";
```

Tạo token riêng bằng Python: `python -c "import secrets; print(secrets.token_hex(24))"`. Không dùng nguyên chuỗi minh họa. Nếu để trống SSID hoặc token dưới 24 ký tự, firmware sẽ kích hoạt chế độ **Provisioning Mode** (phát BLE Service `4fafc201-1fb5-459e-8fcc-c5c9c331914b` và SoftAP `SmartFire-Setup-XXXX` tại `192.168.4.1` để người dùng cài Wi-Fi qua ứng dụng SmartFire AI hoặc trình duyệt Web). Wi-Fi hỗ trợ băng tần 2,4GHz; điện thoại và ESP32 cần ở trong cùng lớp mạng LAN, tránh bật tính năng AP Isolation (cách ly thiết bị khách).

Trong `Config.h`, lần đầu giữ:

```cpp
constexpr bool kHornEnabled = false;
constexpr bool kGasCommissioned = false;
```

Đây là **bench interlock** (khóa đầu ra khi thử bàn) hiển thị rõ trên telemetry, không phải cấu hình vận hành cuối. Flame vẫn được xử lý trong warm-up; LED và `horn_requested` thể hiện yêu cầu còi, nhưng `horn_enabled=false` nghĩa là GPIO còi giữ OFF.

## Bước 4 — Build, Upload, Monitor

Trong PlatformIO Terminal:

```sh
pio run -e esp32dev
pio run -e esp32dev -t upload
pio device monitor -b 115200
```

Hoặc dùng Project Tasks → esp32dev → General → Build / Upload / Monitor. Cổng tự nhận; nếu chọn sai, `pio device list`, rồi thêm `upload_port = COM5` và `monitor_port = COM5` trong ini bằng cổng thật (Linux thường `/dev/ttyUSB0`). Đóng Serial Monitor trước khi upload nếu cổng đang bận.

Nếu treo ở Connecting, giữ BOOT khi bắt đầu upload và thả khi quá trình ghi bắt đầu; thao tác cụ thể tùy board auto-reset. Nếu nạp được mà không có log: baud 115200, cổng đúng, nhấn EN/RESET. Không dùng việc bấm BOOT làm nút Test của ứng dụng; Test nằm ở GPIO18.

Sau khi cấu hình mạng, Serial in `ip=...`; dùng địa chỉ đó ở app/client. `0.0.0.0` là chưa kết nối. `boot=...` đổi sau mỗi reboot. JSON in mỗi khoảng 1 giây; dữ liệu mạng có thể trễ khi client chặn task HTTP, nhưng vòng local không chờ task đó.

## Bước 5 — Test không tạo cháy

Tắt nguồn trước mỗi lần đổi dây. Dùng nút GPIO18, LED GPIO25 và tín hiệu giả lập; không tạo cháy/khói độc để thử.

- Giữ nút 2 giây: `testing=true` trong 1 giây, LED bật; còi chỉ chạy nếu đã bật interlock.
- Flame DO active-low: khi module đã tháo khỏi GPIO27, nối GPIO27 xuống GND bằng công tắc thử; giữ >500ms → ALARM. Không chập ngõ ra module đang được cấp nguồn.
- Thả công tắc, flame không còn kích: phải qua 10 giây clear liên tục mới hết ALARM nếu không còn bằng chứng khí/lỗi khác.
- Mô phỏng MQ: tháo AO khỏi điểm chia áp, giữ GND chung; dùng biến trở 10k giữa 3V3 và GND, wiper qua R1 10k tới điểm ADC/R2 xuống GND. Cách này cho tối đa khoảng 1,65V tại ADC. Đặt ngưỡng thử tạm phù hợp với dải đo được, ghi rõ là cấu hình simulation và khôi phục sau test. Không cấp điện áp lớn hơn thiết kế để ép vượt ngưỡng.
- Ngắt Wi-Fi/router trong khi tín hiệu giả lập còn kích: kiểm tra LED/còi tiếp tục theo logic. Ghi video/thời gian từ thực nghiệm thật, không suy ra kết quả chỉ từ unit test.

## Bước 6 — MQ-2 commissioning

1. Cấp heater 5V ổn định; điều kiện hóa ban đầu theo manual Winsen (ít nhất 48 giờ trong điều kiện thử chuẩn), đặt trên bề mặt phù hợp và theo dõi thiết bị nóng.
2. Quan sát `gas_raw` sau khi ổn định với divider đúng, ở môi trường sạch và các tình huống nhiễu an toàn có kiểm soát. Cần kiểm tra lại độ ổn định sau mỗi cold start; 180 giây không phải cam kết sensor đã sẵn sàng.
3. Thu log nhiều phiên, chọn `kGasOn`, `kGasOff` theo kết quả, giữ `off < on`; nếu baseline đã cao hơn off, không cứ bật commissioning để bỏ qua vấn đề.
4. Chọn boot warm-up đủ theo đo thực tế; đặt `kGasCommissioned=true` sau khi có kết quả. Đây là xác nhận cấu hình bằng tay, không phải firmware tự chứng nhận chất lượng cảm biến.
5. Thực nghiệm lại false alarm, detection latency và môi trường nhiễu. Không thể suy ra mức ppm chính xác từ raw ADC chưa hiệu chuẩn.

## Bước 7 — Bật còi 12V

Đo/xác nhận polarity D4184 khi còi chưa nối, kiểm tra đầu ra OFF khi reset, có pull resistor phù hợp. Sau đó `kHornEnabled=true`, build/upload lại, rút USB PC trước khi cấp ngoài nếu board chưa có power-path được xác nhận. Thử giữ nút 2 giây để kiểm tra còi 1 giây và thử ngắt mạng/reset. Nếu D4184 không kích chắc bằng 3,3V, cần tầng driver/level interface phù hợp; không tăng điện áp GPIO.

## Bước 8 — Thêm node thứ hai

Đổi `device_id` thành `smartfire-node-02`, `room_id` thành phòng thật; token riêng mỗi node. Build/upload vào board thứ hai. App lưu node/IP/token và polling riêng; không nhân bản ID. ESP32 không tự suy ra vị trí trong phòng, không tự biết bình chữa cháy nằm đâu. Vị trí do app quản lý trong SQLite.
