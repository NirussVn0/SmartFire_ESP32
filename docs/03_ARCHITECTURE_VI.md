# Architecture — clean code vừa đủ, phát triển lâu dài

**Keep safety decisions independent of transport and presentation.**  
Quyết định cảnh báo cục bộ độc lập với cách truyền dữ liệu và giao diện. App lỗi hoặc mất mạng không được biến thành điều kiện tắt local alarm.

## 1. Ba lớp và một điểm điều phối

| Thành phần | Trách nhiệm | Không làm |
|---|---|---|
| AlarmCore | Phân tích Inputs + thời gian; state machine, ACK, Silence, Test | Không đọc GPIO, không HTTP, không gọi AI |
| Hardware | Đọc ADC/digital, debounce nút, điều khiển LED/còi | Không quyết định bản đồ/phòng |
| Transport | Wi-Fi STA, HTTP token, snapshot/events, queue lệnh | Không ghi GPIO còi, không sửa trực tiếp state của core |
| main.cpp | Điều phối sample → update → command → output → publish | Không trộn xử lý HTTP vào loop |

`AlarmCore` là C++ thuần nên chạy được host test trên máy tính. Không thêm interface/abstract factory khi chưa có nhu cầu; tách trách nhiệm bằng file/module trước. Thay HTTP bằng MQTT sau này chỉ cần đổi Transport/contract, giữ nguyên policy.

## 2. Luồng chạy

- Arduino loop trên core 1 của ESP32 classic: mỗi 50ms lấy mẫu, cập nhật policy, xử lý nút và tối đa 4 lệnh đã xếp hàng, điều khiển output.
- Network task trên core 0, priority 1: reconnect Wi-Fi theo khoảng 15 giây, HTTP, Serial. Không có vòng `while chưa kết nối Wi-Fi` trong setup.
- Network gửi lệnh qua FreeRTOS queue tối đa 8 phần tử; queue đầy trả 503. Lệnh chờ quá 2 giây bị từ chối. Request có thể đến giữa vòng đọc; tuổi lệnh dùng thời điểm xử lý mới để tránh lỗi unsigned underflow.
- Loop là nơi duy nhất sửa AlarmCore. Nó publish snapshot + 32 event vào bus có mutex. Loop dùng try-lock (không chờ); task HTTP sao chép dữ liệu rồi thả khóa trước khi serialize/gửi mạng.
- HTTP chậm vẫn có thể làm mất khả năng truy cập API/Serial tạm thời; local sensing không đợi HTTP. Đây là thiết kế cô lập độ trễ, chưa phải chứng minh real-time deadline trên hardware.

Không có bảo đảm hard real-time, chưa có watchdog/application supervision riêng. Brownout, lỗi CPU, nguồn mất, cảm biến đứt dây, ADC sai và lỗi điện vẫn cần kiểm thử/phần cứng bổ sung.

## 3. State machine

`WARMING / UNCONFIGURED / NORMAL → SUSPECTED → ALARM → trạng thái phù hợp khi clear liên tục`.

- Flame kích liên tục 500ms → ALARM, kể cả trong warm-up hoặc MQ chưa commissioning.
- MQ raw >= on liên tục 2 giây → ALARM, chỉ khi đã commissioning và hết warm-up.
- SUSPECTED khi flame vừa kích hoặc MQ >= off nhưng chưa đủ điều kiện ALARM. Chỉ có LED/trạng thái, chưa còi do nguy cơ.
- ALARM có ưu tiên hiển thị cao nhất; `gas_fault` vẫn là cờ riêng, không bị che mất trong telemetry.
- Để clear ALARM: flame không kích, không gas fault; MQ đã ready thì raw phải <= off, giữ 10 giây. Khi gas chưa ready, recovery xét flame và fault; sau recovery trạng thái vẫn WARMING/UNCONFIGURED, không khẳng định đầy đủ bảo vệ.
- ADC sát rail <=5 hoặc >=4090 liên tục 5 giây → `gas_fault=true`. High rail sau commissioning vẫn có thể kích ALARM; không bỏ tín hiệu cao chỉ vì nó cũng có thể là lỗi. Đọc về giữa dải sẽ xóa cờ rail fault ở mẫu kế tiếp.
- Rail diagnostic không phát hiện mọi trường hợp đứt dây, sensor hỏng, heater hỏng hoặc mất nguồn; flame DO đứt dây với pull-up có thể trông như “không lửa”. Không tuyên bố self-test đầy đủ.

Thông số thời gian/ngưỡng là policy thử nghiệm, phải được đánh giá; không phải thông số của tiêu chuẩn PCCC.

## 4. ACK / Silence / Test

| Lệnh | Điều kiện và tác dụng |
|---|---|
| ACK | Chỉ khi ALARM; đánh dấu đã đọc, không tắt còi |
| Silence | Chỉ ALARM, không flame đang kích, không gas fault, chưa dùng Silence trong đợt này |
| Thời hạn Silence | 30 giây, cảm biến và ALARM vẫn chạy; chưa phục hồi thì còi tự kêu lại |
| Escalation | Flame xuất hiện hoặc gas fault → hủy mute ngay tại vòng xử lý kế tiếp |
| Silence lặp | Từ chối; không được kéo dài vô hạn. Chỉ có lại sau một lần recovery thực sự |
| Test | Không đang ALARM/đang Test; kích yêu cầu còi 1 giây, đọc cảm biến vẫn chạy |
| Nút vật lý | Short release = Silence; giữ >=2 giây = Test; giữ lâu không làm tắt bảo vệ |

Không có endpoint `disable`, `poweroff`, `reset_alarm` hoặc nút ngắt hết hệ thống. Khi bảo trì bàn, ngắt nguồn có chủ ý; app phải coi thiếu heartbeat là offline. Nếu cần remote maintenance, thiết kế riêng có thời hạn/quyền hạn/trạng thái rõ ràng, chưa thêm vào v0.1.

## 5. Ghép với ý tưởng app của bạn

**Local-first means the app owns persistent user data while each node owns its alarm logic.**  
Local-first nghĩa là app giữ dữ liệu người dùng bền vững, còn từng node giữ logic báo động của chính nó.

| Yêu cầu | Nơi triển khai |
|---|---|
| Sensor fusion cơ bản | AlarmCore trên ESP32 |
| Còi + LED + nút | Hardware trên ESP32 |
| Cảnh báo đang chạy trên màn chính | App foreground đọc status/events |
| Map 2D + node/phòng | App + SQLite, tọa độ normalized |
| Vị trí người dùng | Chọn phòng hoặc QR; chưa có indoor positioning tự động |
| Bình chữa cháy gần nhất | App tính theo map; khoảng cách không xác nhận lối đi an toàn |
| Nhiều node | App quản lý nhiều device_id và địa chỉ LAN, token riêng |
| Lịch sử bền vững | App SQLite; ESP32 không cần cài SQLite |
| Push khi app đóng/ngoài LAN | Thêm thành phần luôn hoạt động + dịch vụ notification phù hợp, chưa làm |
| Đồng bộ toàn bộ còi | Cần giao thức/gateway riêng và fail-safe policy; chưa triển khai |
| AI Jev/LLM | Lớp phân tích phụ; cần xác nhận dịch vụ cụ thể, không có key/call AI trong firmware này |

AI không có quyền im còi hoặc ghi đè local hazard rule. Nếu sau này huấn luyện TinyML, chạy shadow mode (đánh giá song song), ghi prediction nhưng chưa cho nó thay quyết định khi chưa có thử nghiệm độc lập.

## 6. Mở rộng theo thứ tự

1. Hoàn thành một node thật: nguồn/điện áp, polarity, cảm biến, ngưỡng, offline test.
2. App LAN + SQLite, status/events, freshness, ACK/Silence và logging.
3. Map và QR, nhiều node, danh mục bình chữa cháy.
4. BME280/nhiệt phù hợp: thêm Inputs, policy, host test, thực nghiệm fusion; không chỉ gắn sensor rồi gọi là AI.
5. Provisioning + NVS có schema version; không hardcode Wi-Fi khi chuẩn bị chia sẻ thiết bị.
6. Nếu cần truy cập xa: transport bảo mật, danh tính thiết bị, authorization, offline queue có giới hạn và bảo vệ flash wear.
7. Nguồn dự phòng/giám sát nguồn, watchdog, storage recovery và fault injection. OTA chỉ sau khi có xác thực và rollback phù hợp; không bật OTA không mật khẩu.

Không thêm backend database ở v0.1. Khi cần thành phần luôn chạy, vẫn có thể giữ SQLite làm database duy nhất trên app như lựa chọn hiện tại; gateway chỉ truyền/notify, phải nêu rõ giới hạn khi app không hoạt động.
