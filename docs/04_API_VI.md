# Local LAN API v1 — contract cho app

Base URL: `http://<IP_ESP32>` in trong Serial, port 80. Mọi route hợp lệ cần header:

```http
Authorization: Bearer TOKEN_RIENG_CUA_NODE
```

Token riêng >=24 ký tự. Không gửi token qua query string. HTTP chưa mã hóa: chỉ dùng mạng thử nghiệm/LAN tin cậy; không port-forward ra Internet. Token có thể bị đọc trên đường truyền bởi tác nhân có khả năng quan sát mạng. Cấu hình mạng Wi-Fi được thực hiện linh hoạt qua BLE GATT hoặc SoftAP Web Portal (`/configure`); app Android native SmartFire AI hoặc CLI là client mục tiêu. Không nhúng token thật vào repo công khai.

## Trạng thái

`GET /v1/status` → 200:

```json
{
  "api_version": 1,
  "device_id": "smartfire-node-01",
  "room_id": "room-101",
  "boot_id": "EXAMPLE_ONLY",
  "latest_seq": 1,
  "status": {
    "uptime_ms": 1234,
    "state": "WARMING",
    "gas_raw": 1000,
    "flame": false,
    "gas_ready": false,
    "gas_fault": false,
    "alarm": false,
    "muted": false,
    "acknowledged": false,
    "testing": false,
    "horn_requested": false,
    "horn_enabled": false,
    "gas_commissioned": false,
    "temperature_celsius": null
  }
}
```

Ví dụ minh họa schema, không phải dữ liệu đo. `horn_requested=true` không chứng minh còi đã phát âm thanh; `horn_enabled=false` luôn giữ pin OFF. Cần cảm biến phản hồi riêng nếu muốn xác nhận âm thanh thực.

API trả snapshot mới khoảng 200ms; kiểm tra `uptime_ms` có tiến lên qua các response. Clock MCU là monotonic 32-bit, wrap sau khoảng 49,7 ngày; không dùng làm giờ UTC hoặc khóa duy nhất. App gắn `received_at` theo đồng hồ điện thoại và ghi thời điểm node uptime riêng. `boot_id` ngẫu nhiên đổi khi boot, không phải danh tính mật mã.

## Sự kiện và lưu SQLite

`GET /v1/events?since=0` → envelope cùng ID, `latest_seq`, `oldest_seq`, `events` có `seq`, `kind`, `request_id`, `result`, `status`.

- `kind`: `state`, `button`, `ack`, `silence`, `test`.
- `result`: `none`, `applied`, `rejected`, `expired`.
- Lưu unique key `(device_id, boot_id, seq)` trong SQLite; dùng UPSERT/IGNORE để không trùng khi retry.
- Lần đầu hoặc boot_id đổi: đọc từ since=0. Các lần sau since=seq cuối đã lưu thành công.
- Buffer giữ 32 event trong RAM; nếu oldest_seq > last_saved_seq+1, đánh dấu **history gap**, không tự điền dữ liệu tưởng tượng.
- `seq` cũng là uint32; với v0.1 nếu wrap, khởi động session cursor mới khi latest_seq giảm. Chưa có durable event replay, RTC/NTP hay lưu event khi tắt điện.
- Poll status khoảng 1 giây khi foreground; gợi ý timeout 2 giây, sau vài lần không nhận snapshot mới thì đánh dấu offline/stale. Offline không có nghĩa phòng an toàn.
- Nếu app mất kết nối và node có hơn 32 event, không thể lấy lại phần đã tràn. Muốn lịch sử đầy đủ lúc app đóng cần logger/gateway luôn chạy hoặc thiết kế thêm storage trên node.

App có thể dùng các bảng tối thiểu: `devices(device_id,room_id,ip,x,y,last_seen,...)`, `rooms`, `floors`, `extinguishers`, `events(device_id,boot_id,seq,received_at,node_uptime_ms,payload_json,...)`. Token cất bằng secure storage của OS, không trong cột SQLite plaintext. Schema/migration app sẽ được triển khai ở bước app, chưa có database app được tạo trong bộ này.

## Gửi lệnh

| Route | Method | Ý nghĩa |
|---|---|---|
| `/v1/commands/ack` | POST | Đã thấy ALARM, còi vẫn chạy |
| `/v1/commands/silence` | POST | Đề nghị im còi có giới hạn theo policy |
| `/v1/commands/test` | POST | Test output một giây nếu không đang ALARM |

Không cần body. 202 có `queued=true`, `boot_id`, `request_id`: **đã xếp hàng, chưa có nghĩa lệnh được áp dụng**. App tìm event cùng `request_id`/`boot_id` để xem applied/rejected/expired; nếu mất event thì đọc status và hiển thị kết quả chưa rõ, không báo thành công giả. Nút rời có request_id=0. App phải vô hiệu nút khi node stale/offline. Không có command ID idempotent do client cung cấp; đừng tự retry lệnh khi timeout. Gửi lại Silence trong cùng đợt bị policy từ chối, nhưng không có exactly-once delivery tổng quát.

401 = token sai/thiếu; 400 = since không hợp lệ; 503 = queue đầy/snapshot busy; 404 = route/method không hỗ trợ. HTTP server đồng bộ trong task mạng; client quá chậm có thể làm API khác chờ. Đây là API prototype, chưa harden để mở công cộng.

## Client thử bằng Python

Dùng `tools/node_client.py`, không cần thư viện ngoài. Tool hỏi token bằng input ẩn; không ghi token vào lịch sử command line.

```sh
python tools/node_client.py http://192.168.1.50 status
python tools/node_client.py http://192.168.1.50 events --since 0
python tools/node_client.py http://192.168.1.50 test
python tools/node_client.py http://192.168.1.50 ack
python tools/node_client.py http://192.168.1.50 silence
```

Đổi IP bằng địa chỉ thật trong Serial. Các lệnh điều khiển chỉ gửi theo thao tác có chủ ý; không dùng vòng lặp retry để giữ im còi. Thông báo khi app tắt/điện thoại khóa chưa được đảm bảo bởi LAN polling.
