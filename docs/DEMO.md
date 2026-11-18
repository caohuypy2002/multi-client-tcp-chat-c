# KỊCH BẢN DEMO BẢO VỆ ASSIGNMENT TRƯỚC GIẢNG VIÊN (5 PHÚT)

Mục tiêu: Trình diễn đầy đủ, tự tin tất cả các yêu cầu khắt khe của thầy Văn Võ trong thời gian ngắn nhất.

---

## Chuẩn bị trước khi thầy gọi (1 phút)
1. Mở Terminal trên Ubuntu, di chuyển vào thư mục dự án:
   ```bash
   cd ~/chat-app
   make clean && make all
   ```
2. Đảm bảo đã biên dịch sạch sẽ không có cảnh báo nào (`0 errors, 0 warnings`).

---

## Kịch bản 5 bước trình diễn

### Bước 1: Khởi động Server (30 giây)
- Chạy lệnh khởi động server ở chế độ chi tiết:
  ```bash
  ./build/server -p 5555 -m 10 -v
  ```
- **Chỉ cho thầy thấy:**
  - Server tự động phát hiện và in danh sách các card mạng IPv4 nội bộ (`print_local_ips`).
  - Trạng thái log chuyên nghiệp có timestamp: `[INFO] Server đã sẵn sàng (Giới hạn tối đa: 10 client đồng thời)`.

### Bước 2: Trình diễn 10 Client đồng thời bằng tmux (1.5 phút)
- Mở một tab Terminal mới, chạy script tự động:
  ```bash
  bash scripts/run_10_clients.sh 127.0.0.1 5555
  ```
- **Thầy sẽ thấy:**
  - Màn hình tmux tự động chia 10 ô cửa sổ nhỏ cho 10 client từ `User_1` đến `User_10`.
  - Mọi client đều nhận thông báo chào mừng và danh sách người dùng `(10/10)`.
  - Chọn một ô bất kỳ gõ `Xin chào mọi người!`, tất cả 10 ô đồng loạt nhận được tin nhắn tức thì có timestamp máy chủ!

### Bước 3: Thử nghiệm Người thứ 11 kết nối (Bị từ chối) (1 phút)
- Mở thêm một Terminal thứ ba, cố gắng kết nối client thứ 11:
  ```bash
  ./build/client 127.0.0.1 5555 Nguoi_11
  ```
- **Kết quả hiển thị:**
  - Client in ra: `*** THÔNG BÁO TỪ SERVER *** Phòng chat đã đủ 10/10 người, vui lòng thử lại sau.`
  - Kết nối đóng lại lịch sự, không hề làm sập server hay ảnh hưởng 10 người đang chat.

### Bước 4: Thử nghiệm Thoát và Nhường chỗ (1 phút)
- Ở ô `User_1` trong tmux, nhấn `Ctrl+D` hoặc gõ `/quit`.
- Server thông báo: `User_1 đã rời phòng chat (9/10) [QUIT]`.
- Chạy lại Client thứ 11:
  ```bash
  ./build/client 127.0.0.1 5555 Nguoi_11
  ```
- **Kết quả:** `Nguoi_11` lập tức tham gia phòng chat thành công vì chỗ trống đã được giải phóng!
- Thử nghiệm gửi tin nhắn riêng: `/w User_2 Alo riêng nhe`. Chỉ có `User_2` và `Nguoi_11` nhận được tin nhắn màu tím.

### Bước 5: Thoát Server an toàn và Kiểm tra rò rỉ (1 phút)
- Quay lại cửa sổ Server, nhấn `Ctrl+C`.
- Server gửi thông điệp `BYE Server đang tắt.` tới tất cả client, đóng các luồng một cách có trật tự trong vòng 3 giây và in: `Server đã dừng hoàn tất. Tạm biệt!`.
- Chạy kiểm tra bộ nhớ trước mặt thầy:
  ```bash
  make valgrind
  ```
- Chỉ vào dòng: `definitely lost: 0 bytes`, `ERROR SUMMARY: 0 errors`.

---
*Ghi chú: Điểm số tối đa cho phong thái chuyên nghiệp, code C chuẩn POSIX, an toàn bộ nhớ và giao diện rõ ràng.*
