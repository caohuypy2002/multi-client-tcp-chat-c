# ĐẶC TẢ GIAO THỨC TRUYỀN THÔNG ỨNG DỤNG CHAT (TCP/IP)

## 1. Cơ chế đóng gói thông điệp (Message Framing)
- **Giao thức vận chuyển:** TCP (Byte stream, kết nối tin cậy).
- **Ranh giới thông điệp:** Mỗi thông điệp là một dòng văn bản chuẩn UTF-8 kết thúc bằng ký tự xuống dòng `\n` (ASCII `0x0A`).
- **Tương thích:** Ký tự `\r` (ASCII `0x0D`) phía trước `\n` sẽ được bên nhận tự động bỏ qua (tương thích telnet / PuTTY Raw).
- **Giới hạn kích thước:** Độ dài tối đa 1 dòng là `1024` byte (kể cả `\n`). Tin nhắn quá giới hạn sẽ bị máy chủ từ chối với mã lỗi `ERR TOO_LONG`.

## 2. Thiết kế "Server chào trước" (Server Greets First)
Khác với mô hình thông thường nơi Client kết nối rồi gửi ngay dữ liệu, hệ thống này áp dụng nguyên lý:
**Server gửi phản hồi ngay sau khi `accept()` thành công**:
1. Nếu còn chỗ: Server gửi `HELLO <count> <max>\n` (báo hiệu đã giữ slot cho client).
2. Nếu hết chỗ (đủ 10 người): Server gửi `REJECT FULL <thông điệp>\n` rồi mới đóng socket.

### Vì sao phải thiết kế như vậy? (Giải thích bảo vệ đồ án)
Nếu Client gửi ngay khi kết nối mà Server đang đầy:
- Client gọi `send()` đẩy dữ liệu vào kernel buffer.
- Server gọi `close()` ngay lập tức trong khi trong buffer socket vẫn còn dữ liệu chưa đọc.
- Chuẩn TCP bắt buộc hệ điều hành phát gói tin **RST** (Reset) thay vì **FIN**.
- Hậu quả: Client nhận lỗi `ECONNRESET` ("Connection reset by peer") và **không kịp đọc thông điệp giải thích vì sao bị từ chối**!
- Với thiết kế "Server chào trước", Client đợi nhận `HELLO` mới gửi `JOIN`. Do đó khi bị đầy, kết nối đóng một cách êm đẹp và Client luôn đọc trọn vẹn thông báo từ chối.

---

## 3. Danh mục các lệnh và phản hồi

### A. Từ Client gửi lên Server
| Lệnh | Cú pháp | Ý nghĩa |
|---|---|---|
| `JOIN` | `JOIN <nickname>` | Đăng ký nickname để vào phòng chat (chỉ gồm `[A-Za-z0-9_]`, 1-16 ký tự) |
| `MSG` | `MSG <nội dung>` | Gửi tin nhắn tới toàn bộ mọi người trong phòng chat |
| `PM` | `PM <người_nhận> <nội dung>` | Gửi tin nhắn riêng tư tới 1 người cụ thể |
| `LIST` | `LIST` | Yêu cầu xem danh sách người dùng đang trực tuyến |
| `INFO` | `INFO` | Yêu cầu xem thông tin tiến trình/luồng xử lý của Server (`PID/TGID`, `TID/LWP`, `FD`) |
| `PONG` | `PONG` | Phản hồi lại gói tin `PING` định kỳ của server |
| `QUIT` | `QUIT` | Rời khỏi phòng chat một cách chủ động |

### B. Từ Server gửi về Client
| Thông điệp | Cú pháp | Ý nghĩa |
|---|---|---|
| `HELLO` | `HELLO <count> <max>` | Chào mừng kết nối ban đầu, thông báo sĩ số hiện tại |
| `WELCOME` | `WELCOME <name> <count> <max>` | Xác nhận đăng ký nickname thành công, chính thức vào phòng |
| `REJECT` | `REJECT FULL\|TIMEOUT\|TOO_MANY_TRIES <msg>` | Từ chối kết nối và ngắt socket |
| `MSG` | `MSG <HH:MM:SS> <người_gửi> <nội dung>` | Chuyển tiếp tin nhắn nhóm |
| `PM` | `PM <HH:MM:SS> <từ> <tới> <nội dung>` | Chuyển tiếp tin nhắn riêng (gửi cho người nhận và bản sao cho người gửi) |
| `JOINED` | `JOINED <HH:MM:SS> <tên> <count> <max>` | Thông báo có người mới vào phòng |
| `LEFT` | `LEFT <HH:MM:SS> <tên> <count> <max> <lý_do>` | Thông báo có người rời phòng (`QUIT`, `LOST`, `TIMEOUT`, `KICKED`) |
| `USERS` | `USERS <count> <max> <tên1,tên2,...>` | Cập nhật danh sách người dùng online |
| `INFO` | `INFO Server_PID=... \| Active_Worker_Threads=...` | Trả về bảng chẩn đoán tiến trình & luồng nhân Linux (`SYS_gettid`) |
| `ERR` | `ERR <MÃ_LỖI> <thông điệp>` | Báo lỗi cú pháp, trùng tên, người nhận không tồn tại, v.v. |
| `PING` | `PING` | Gói tin thăm dò tình trạng kết nối |
| `BYE` | `BYE <thông điệp>` | Server thông báo đóng kết nối hoặc tắt máy chủ |

---

## 4. Ví dụ phiên hội thoại điển hình

```text
[Client kết nối tới 127.0.0.1:5555]
Server -> Client: HELLO 1 10
Client -> Server: JOIN An
Server -> Client: WELCOME An 1 10
Server -> Client: USERS 1 10 An

[Client thứ hai kết nối]
Server -> Client 1: JOINED 19:15:00 Binh 2 10
Server -> Client 1: USERS 2 10 An,Binh

[Client gửi tin nhắn nhóm]
Client 1 -> Server: MSG Xin chao ca phong!
Server -> Client 1: MSG 19:15:05 An Xin chao ca phong!
Server -> Client 2: MSG 19:15:05 An Xin chao ca phong!

[Client 1 gửi tin riêng cho Binh]
Client 1 -> Server: PM Binh Ban co ranh khong?
Server -> Client 1: PM 19:15:10 An Binh Ban co ranh khong?
Server -> Client 2: PM 19:15:10 An Binh Ban co ranh khong?

[Client 1 thoát]
Client 1 -> Server: QUIT
Server -> Client 1: BYE Tam biet ban!
Server -> Client 2: LEFT 19:15:20 An 1 10 QUIT
Server -> Client 2: USERS 1 10 Binh
```
