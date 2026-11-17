# Prebuilt bench image — thử nạp đầu tiên

Chỉ dành cho ESP32 classic/WROOM DevKit 4MB theo `esp32dev`. Không dùng cho C3/S3/C6, board bật secure boot/flash encryption, hoặc board chưa xác định đúng loại.

Cấu hình image: còi 12V khóa OFF; MQ-2 chưa commissioning; Wi-Fi/token để trống, mạng tắt. Local sensor logic, LED, nút và Serial vẫn có. Không dùng image này làm cấu hình giám sát hoàn chỉnh.

**Ưu tiên:** mở source bằng PlatformIO, nhập Config/Secrets của bạn rồi Upload. PlatformIO tự nạp đúng các thành phần và địa chỉ.

Nếu chỉ muốn nạp bản bench đã build:

1. Ngắt toàn bộ nguồn ngoài/12V, chỉ nối USB PC với ESP32. Còi chưa nối.
2. Cài esptool 4.5.1 trong Python environment của bạn: `python -m pip install esptool==4.5.1`.
3. Từ thư mục gốc dự án, đổi COM5 thành cổng thật rồi chạy:

```sh
python -m esptool --chip esp32 --port COM5 --baud 115200 write_flash 0x0 prebuilt/smartfire_bench_merged.bin
```

Đây là lệnh hướng dẫn người dùng, chưa được thực thi lên thiết bị trong phiên tạo bộ mã. Nó ghi đè firmware/partition tương ứng trên board; không phải thao tác đọc. Không dùng `firmware.bin` riêng ở offset 0x0.

4. Reset và mở Serial Monitor 115200. Khi chưa nối MQ-2, rail diagnostic có thể báo FAULT; đọc hướng dẫn đấu dây trước khi tiếp tục.

Merged image gồm:

| Offset | File |
|---|---|
| 0x1000 | bootloader.bin |
| 0x8000 | partitions.bin |
| 0xe000 | boot_app0.bin |
| 0x10000 | firmware.bin |

`smartfire_bench_merged.bin` đã chứa padding và các vùng trên, nạp ở 0x0. Không dùng file merged này như app OTA. OTA chưa được triển khai.
