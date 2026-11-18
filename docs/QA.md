# TỔNG HỢP 20 CÂU HỎI VẤN ĐÁP BẢO VỆ ASSIGNMENT LẬP TRÌNH MẠNG (C/LINUX)

---

### Câu 1: Tại sao bài này dùng TCP mà không dùng UDP?
**Trả lời:**
Ứng dụng chat văn bản đòi hỏi tính **toàn vẹn dữ liệu** (không mất tin nhắn), **đúng thứ tự** các câu thoại và có cơ chế thiết lập kết nối tin cậy (bắt tay 3 bước). TCP đảm bảo truyền tải tin cậy theo luồng byte, có kiểm soát luồng và chống nghẽn. UDP không đảm bảo thứ tự, có thể mất gói, chỉ phù hợp cho truyền dữ liệu thời gian thực chấp nhận mất mát như Voice/Video call.

### Câu 2: Vòng đời của một socket TCP phía Server diễn ra như thế nào?
**Trả lời:**
1. `socket()`: Tạo file descriptor endpoint mạng (AF_INET, SOCK_STREAM).
2. `bind()`: Gán socket với một địa chỉ IP (INADDR_ANY) và cổng cục bộ.
3. `listen()`: Chuyển socket sang trạng thái bị động, sẵn sàng xếp hàng các kết nối đến (backlog).
4. `accept()`: Lấy một kết nối đã hoàn thành bắt tay 3 bước ra khỏi hàng đợi và tạo một socket mới dành riêng cho client đó.
5. `recv()` / `send()`: Đọc và ghi dữ liệu song công (duplex).
6. `shutdown()` / `close()`: Đóng từng hướng hoặc giải phóng hoàn toàn socket.

### Câu 3: Làm thế nào server phục vụ tối đa 10 người cùng lúc?
**Trả lời:**
Sử dụng mô hình đa luồng (`pthread`):
- Luồng chính (`main thread`) chỉ chạy vòng lặp `accept()` để tiếp nhận kết nối.
- Mỗi client được cấp phát một luồng riêng (`client_handler_thread`) để độc lập thực hiện `recv()` và xử lý lệnh.
- Nhờ vậy, nếu một client bị nghẽn mạng hoặc đọc chậm, các client khác vẫn gửi/nhận tin nhắn bình thường.

### Câu 4: Làm sao đảm bảo không bao giờ vượt quá 10 người, kể cả khi 20 người kết nối cùng lúc?
**Trả lời:**
Bằng cơ chế **giữ chỗ nguyên tử (atomic reservation) dưới khoá `g_lock`**:
Ngay khi `accept()` trả về socket, server lập tức khoá `pthread_mutex_lock(&g_lock)`. Nếu số slot đã đủ 10, server mở khoá, gửi thông báo `REJECT FULL` và gọi `close()` ngay lập tức mà **không khởi tạo luồng mới**. Vì việc kiểm tra và tăng số lượng diễn ra trong vùng tương hỗ (critical section), hiện tượng race condition không thể xảy ra.

### Câu 5: Vấn đề "TCP là luồng byte" (Message Framing) là gì và bạn giải quyết thế nào?
**Trả lời:**
TCP không có khái niệm ranh giới gói tin. Một lần gọi `recv()` có thể nhận được nửa câu (fragmented) hoặc nhiều câu dính liền nhau (coalesced).
Em giải quyết bằng module `linebuf`: Dùng bộ đệm tích luỹ byte nhận được, dùng `memchr()` tìm ký tự xuống dòng `\n`. Khi thấy `\n`, bóc tách trọn vẹn 1 dòng, loại bỏ `\r`, đưa vào xử lý và dùng `memmove()` dồn các byte dư thừa về đầu buffer để đợi gói tiếp theo.

### Câu 6: Tại sao cần `signal(SIGPIPE, SIG_IGN)` và cờ `MSG_NOSIGNAL`?
**Trả lời:**
Khi một client đột ngột đóng kết nối hoặc rút dây mạng, nếu server cố tình gọi `send()` vào socket đã đóng, kernel Linux mặc định sẽ phát tín hiệu `SIGPIPE` tới tiến trình server. Tín hiệu này mặc định sẽ **giết chết (crash) toàn bộ server**.
Bằng cách bỏ qua `SIGPIPE` và thêm cờ `MSG_NOSIGNAL` khi gọi `send()`, hàm `send()` sẽ chỉ trả về `-1` với mã lỗi `EPIPE`, giúp server xử lý lỗi an toàn mà không bị dừng đột ngột.

### Câu 7: Vì sao trong thiết kế lại để "Server chào trước" (Server Greets First)?
**Trả lời:**
Để tránh lỗi **TCP RST**. Nếu Client gửi `JOIN` ngay khi kết nối mà Server đã đầy (10/10), Server sẽ gọi `close()`. Nếu trong socket buffer của server vẫn còn dữ liệu client gửi tới mà chưa kịp đọc, kernel sẽ gửi gói **RST** thay vì FIN. Khi đó, client sẽ bị lỗi `ECONNRESET` và bị ngắt kết nối mà không kịp đọc dòng thông báo "Phòng chat đã đầy". Để server chào trước giúp client chưa gửi gì khi bị từ chối, đảm bảo đọc trọn vẹn thông báo.

### Câu 8: Tại sao lại cần khoá `send_lock` riêng cho từng client bên cạnh khoá `g_lock`?
**Trả lời:**
Để bảo vệ tính toàn vẹn dữ liệu khi ghi vào socket. Khi nhiều luồng cùng broadcast hoặc gửi tin nhắn riêng tới một client, nếu không có `send_lock`, các byte của các tin nhắn khác nhau có thể bị ghi xen kẽ làm hỏng cấu trúc dòng văn bản. Thứ tự khoá luôn tuân thủ: `g_lock` trước, sau đó mới đến `send_lock` của từng client để chống deadlock.

### Câu 9: Quy tắc sở hữu File Descriptor (FD Ownership) là gì?
**Trả lời:**
Chỉ duy nhất luồng handler sở hữu slot mới được phép gọi `close(fd)` trong hàm `clients_cleanup_slot()`. Khi server tắt hoặc muốn ngắt một client, luồng khác chỉ được phép gọi `shutdown(fd, SHUT_RDWR)` để đánh thức hàm `recv()` của luồng handler; luồng handler sẽ tự thoát và đóng socket. Quy tắc này triệt tiêu hoàn toàn lỗi **Double Close** và lỗi **FD Reuse Race Condition** (đóng nhầm fd của kết nối mới).

### Câu 10: Server phát hiện client thoát đột ngột (tắt app, rút dây mạng) bằng cách nào?
**Trả lời:**
1. Khi client tắt bình thường: hàm `recv()` ở server trả về `0` (nhận gói FIN).
2. Khi client sập hoặc ngắt kết nối: `recv()` trả về `-1` với lỗi `ECONNRESET`.
3. Khi client bị treo hoặc ngắt mạng không báo trước: cơ chế **Heartbeat (PING/PONG)**: Sau 15s không hoạt động, server gửi `PING`. Sau 45s không nhận được phản hồi, server chủ động ngắt kết nối với lý do `TIMEOUT`.

### Câu 11: Hàm `htons()` và `ntohs()` dùng để làm gì?
**Trả lời:**
Dùng để chuyển đổi thứ tự byte (Endianness) giữa kiến trúc máy chủ (Host Byte Order - thường là Little Endian trên x86/ARM) và chuẩn giao thức mạng (Network Byte Order - Big Endian):
- `htons()`: Host to Network Short (16-bit, dùng cho số hiệu cổng port).
- `ntohs()`: Network to Host Short.

### Câu 12: Cờ `SO_REUSEADDR` có tác dụng gì?
**Trả lời:**
Khi server tắt, socket vừa đóng sẽ rơi vào trạng thái `TIME_WAIT` (thường kéo dài 1-2 phút) để đảm bảo các gói tin cũ trên mạng tiêu biến hết. Nếu không có cờ `SO_REUSEADDR`, việc khởi động lại server ngay lập tức trên cùng port sẽ bị lỗi `Address already in use`. Cờ này cho phép tái sử dụng cổng ngay lập tức.

### Câu 13: Tại sao ở Client lại dùng `poll()` thay vì tạo thêm một luồng để nhận tin?
**Trả lời:**
`poll()` cho phép lập trình theo mô hình bất đồng bộ hướng sự kiện (I/O Multiplexing) đơn luồng. Chỉ với 1 luồng, client có thể giám sát đồng thời cả bàn phím (`STDIN_FILENO`) và socket mạng. Cách này vừa nhẹ tài nguyên, vừa loại bỏ hoàn toàn các lỗi xung đột tài nguyên giữa luồng đọc phím và luồng in tin nhắn.

### Câu 14: Ký tự `\r\033[K` ở client có tác dụng gì khi in tin nhắn?
**Trả lời:**
Khi người dùng đang gõ dở một câu thì có tin nhắn từ người khác gửi đến, việc in trực tiếp sẽ làm vỡ giao diện. `\r` đưa con trỏ về đầu dòng, `\033[K` là mã escape ANSI để xoá sạch dòng hiện tại. Sau đó chương trình in tin nhắn mới và vẽ lại dấu nhắc `> `, giúp giao diện terminal luôn gọn gàng.

### Câu 15: Bạn phòng chống tấn công chèn mã ANSI (ANSI Escape Injection) như thế nào?
**Trả lời:**
Trong hàm `protocol_sanitize_text()`, trước khi broadcast nội dung tin nhắn, server duyệt qua từng byte: mọi byte điều khiển có mã `< 0x20` (như ESC `0x1B`, chuông `\a`) và `0x7F` đều bị thay thế bằng dấu cách `' '`. Các byte `>= 0x80` được giữ nguyên để hỗ trợ tiếng Việt có dấu chuẩn UTF-8.

### Câu 16: Khi truyền tham số slot cho `pthread_create`, vì sao không được truyền `&i`?
**Trả lời:**
Nếu truyền con trỏ tới biến đếm cục bộ `&i` của vòng lặp, do luồng mới chạy bất đồng bộ, giá trị của `i` trong luồng chính có thể đã thay đổi trước khi luồng mới kịp đọc. Điều này dẫn đến việc nhiều luồng cùng nhận chung một chỉ số slot. Do đó phải ép kiểu giá trị trực tiếp qua con trỏ: `(void*)(intptr_t)slot`.

### Câu 17: Làm sao tắt server an toàn mà không để lại thread rác hay socket treo?
**Trả lời:**
Sử dụng luồng riêng bắt tín hiệu `sigwait(SIGINT/SIGTERM)`:
1. Đặt biến cờ `g_running = 0`.
2. Đóng socket lắng nghe `listen_fd` để đánh thức `accept()`.
3. Gọi `clients_shutdown_all()` gửi thông báo `BYE` và gọi `shutdown()` cho tất cả client.
4. Chờ các luồng handler dọn dẹp xong qua biến điều kiện `pthread_cond_timedwait()` tối đa 3 giây.
5. Giải phóng các mutex và thoát tiến trình với mã 0.

### Câu 18: Sự khác biệt giữa `select()`, `poll()` và `epoll()` là gì?
**Trả lời:**
- `select()`: Giới hạn tối đa 1024 fd (`FD_SETSIZE`), phải quét tuyến tính mảng fd O(N), copy bitmap giữa user và kernel space mỗi vòng lặp.
- `poll()`: Dùng mảng cấu trúc `pollfd`, không bị giới hạn 1024 fd, nhưng vẫn quét O(N).
- `epoll()` (Linux): Hướng sự kiện O(1), đăng ký fd một lần vào kernel qua cây đỏ-đen, chỉ trả về danh sách các fd có sự kiện.

### Câu 19: Nếu muốn mở rộng hệ thống lên 10.000 người dùng đồng thời, bạn sẽ thay đổi kiến trúc thế nào?
**Trả lời:**
Mô hình "1 luồng cho mỗi client" (`thread-per-client`) sẽ gây cạn kiệt bộ nhớ stack và quá tải context switch của CPU.
Để phục vụ 10.000 người (bài toán C10K), em sẽ chuyển sang:
1. Dùng cơ chế I/O bất đồng bộ hướng sự kiện `epoll` trên Linux (Non-blocking I/O).
2. Kết hợp mô hình `Reactor Pattern` với một `Thread Pool` nhỏ (khoảng 4-8 luồng tương ứng số nhân CPU) để xử lý logic và mã hoá.

### Câu 20: Bạn đã kiểm tra rò rỉ bộ nhớ và lỗi luồng như thế nào?
**Trả lời:**
- Rò rỉ bộ nhớ: Sử dụng công cụ **Valgrind** với các cờ `--leak-check=full --show-leak-kinds=all --track-fds=yes`. Kết quả đạt 0 byte rò rỉ, 0 lỗi.
- Lỗi tranh chấp luồng (Data Race): Biên dịch với cờ **ThreadSanitizer** (`-fsanitize=thread`) và AddressSanitizer (`-fsanitize=address,undefined`), chạy mô phỏng 10 client gửi tin nhắn liên tục mà không có bất kỳ cảnh báo race nào.
