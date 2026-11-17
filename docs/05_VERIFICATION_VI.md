# Verification — phân biệt điều đã test và điều cần đo

**Passing a software test does not prove the physical detector is reliable.**  
Test phần mềm chạy đúng không chứng minh hệ cảm biến và còi thật đã hoạt động đáng tin cậy.

## Host test có sẵn

Linux/macOS/WSL có g++ và sanitizer:

```sh
bash tools/test_core.sh
```

Hoặc Windows có g++:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -Ilib/AlarmCore tests/core_tests.cpp -o core_tests.exe
./core_tests.exe
```

Host test không yêu cầu ESP32. Script dùng AddressSanitizer + UndefinedBehaviorSanitizer; tắt riêng LeakSanitizer vì môi trường chạy hiện tại không hỗ trợ duyệt process. AlarmCore không cấp phát heap.

15 kịch bản đã chạy thành công trong môi trường tạo bộ này:

1. Trạng thái NORMAL và còi OFF khi không nguy cơ.
2. WARMING và UNCONFIGURED phân biệt rõ.
3. Flame vẫn vào ALARM trong warm-up, đủ 500ms.
4. MQ đủ 2 giây mới vào ALARM khi ready.
5. MQ chưa ready không kích bằng ngưỡng minh họa.
6. Tín hiệu MQ ngắt quãng reset bộ xác nhận.
7. ACK giữ còi; Silence giới hạn 30s, không gia hạn được.
8. Flame mới xuất hiện hủy Silence.
9. Lỗi gas hủy Silence và chặn clear ALARM.
10. Hysteresis + giữ clear 10s; recovery cho phép Silence ở đợt mới.
11. ACK/Silence bị từ chối khi không ALARM; Test không chặn phát hiện thật.
12. High rail được đánh dấu fault và vẫn có thể kích ALARM khi MQ ready.
13. Flame timer qua rollover uint32.
14. Bộ xác nhận reset khi điều kiện không liên tục.
15. Mute timer qua rollover uint32.

## Test trên board — CHƯA thực hiện

| Test | Cách đo | Điều kiện pass dự kiến |
|---|---|---|
| Nguồn | Multimeter tại 12V, buck 5V, ADC, Flame DO | Đúng điện áp và polarity đã xác nhận |
| Boot/reset | Quan sát GPIO26 và còi qua power cycle/reset | Không có xung bật còi ngoài dự kiến; pull resistor giữ OFF |
| Flame giả lập | Công tắc GPIO27, module tháo khỏi chân thử | Cảnh báo theo delay 500ms + sampling jitter |
| MQ giả lập | Biến trở/nguồn tín hiệu giới hạn, ngưỡng bench rõ ràng | Cảnh báo theo thời gian xác nhận/hysteresis |
| Silence | ALARM do MQ giả lập, nhấn ngắn/POST | Giữ ALARM, im 30s, tái kêu nếu nguy cơ còn |
| Escalation | Trong mute, kích flame giả lập | Mute hủy tại vòng xử lý kế tiếp |
| Nút | Bounce, giữ lâu, nhấn lặp | Không tắt hệ thống, không test lặp vô hạn |
| Wi-Fi mất | Tắt router trong ALARM | Local LED/còi vẫn chạy; app offline/stale |
| HTTP chậm | Client chậm, nhiều request thử có giới hạn | Đo sampling/output jitter; không chỉ dựa vào thiết kế task |
| Nguồn mất | Ngắt adapter | Thiết bị tắt vì chưa có pin; app coi offline |
| API | Thiếu/sai token, GET/POST, queue đầy | Reject đúng; 202 được xác nhận bằng event |
| Event gap/reboot | Ghi >32 event, reset | App phát hiện gap và boot_id mới |
| Long run | Chạy liên tục với log heap và latency | Ghi số đo thật; chưa có cam kết ổn định 24/7 |

Không ghi “đã pass” cho bảng này trước khi đo trên thiết bị thật. Chưa có test nhiệt/EMI, thời lượng pin, tuổi thọ, độ phủ cảm biến, chứng nhận, precision/recall từ dữ liệu thật. Chưa có test tích hợp app vì app chưa viết.

## Bằng chứng biên dịch

Xem `BUILD_STATUS.md` ở thư mục gốc để biết kết quả build tại thời điểm đóng gói. Source test và compile target là hai mức xác minh khác nhau. Không có thiết bị USB ESP32 kết nối trong phiên tạo bộ này, vì vậy không có xác nhận upload/hardware-in-the-loop.
