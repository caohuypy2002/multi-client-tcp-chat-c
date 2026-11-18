# BÁO CÁO BÀI TẬP LỚN: XÂY DỰNG ỨNG DỤNG CHAT TCP/IP ĐA NGƯỜI DÙNG BẰNG NGÔN NGỮ C TRÊN LINUX

**Môn học:** Lập trình mạng (Network Programming / SoC Programming)  
**Giảng viên hướng dẫn:** Thầy Văn Võ  
**Sinh viên thực hiện:** Nguyễn Đăng Cao Huy  
**Lớp:** FPT Automotive  

---

## 1. Giới thiệu đề tài & Mục tiêu
Bài tập lớn yêu cầu thiết kế và cài đặt một hệ thống trò chuyện (Chat Application) hoạt động trên nền tảng mạng TCP/IP bằng ngôn ngữ C thuần trên hệ điều hành Linux.
- **Mô hình:** Client - Server (1 Server điều phối trung tâm, các người dùng là Client).
- **Yêu cầu cốt lõi:** Cho phép tối đa **10 người dùng** có thể kết nối và trò chuyện cùng lúc (đồng thời); người thứ 11 kết nối phải bị từ chối với thông báo rõ ràng.

---

## 2. Cơ sở lý thuyết
- **Mô hình Client-Server:** Server là trung tâm chuyển tiếp (Hub/Star topology), các client không kết nối trực tiếp với nhau mà gửi tin lên server để server phát lại (broadcast).
- **Giao vận tin cậy TCP:** Sử dụng socket hướng kết nối (`SOCK_STREAM`), đảm bảo gói tin không bị mất mát, bảo toàn đúng thứ tự các câu thoại.
- **Kỹ thuật Đa luồng (POSIX Threads - pthread):** Server sử dụng mô hình "Mỗi client một luồng" kết hợp với các khoá tương hỗ (`pthread_mutex_t`) để đồng bộ hoá việc truy cập tài nguyên dùng chung.
- **Vấn đề Framing trong TCP:** Do TCP truyền dữ liệu dưới dạng dòng byte (stream of bytes), ứng dụng tự định nghĩa giao thức phân tách theo dòng kết thúc bằng `\n` và bộ đệm trượt (`linebuf`) để tránh hiện tượng dính gói hoặc cắt đôi gói tin.

---

## 3. Phân tích yêu cầu & Bảng truy vết

| Mã yêu cầu | Mô tả chi tiết | Hiện thực trong mã nguồn |
|---|---|---|
| **R1** | Chat văn bản thời gian thực | `src/server/handler.c`, `src/client/main.c` |
| **R2** | Socket TCP thuần, chuẩn POSIX | `src/common/net_utils.c` |
| **R3** | 1 Server điều phối, mô hình hình sao | `src/server/main.c`, `src/server/clients.c` |
| **R4** | Tối đa 10 người đồng thời | Hằng số `MAX_CLIENTS` trong `include/config.h` |
| **R5** | Ứng dụng Client dòng lệnh tiện ích | `src/client/main.c`, `src/client/ui.c` |
| **R6** | 100% C11 trên hệ điều hành Linux | `Makefile` (`-std=c11 -D_POSIX_C_SOURCE=200809L`) |
| **I1** | Không nghẽn luồng giữa các client | Đa luồng `client_handler_thread` + timeout `SO_SNDTIMEO` |
| **I3** | Từ chối lịch sự người thứ 11 | `clients_reserve_slot()` trong `src/server/main.c` |
| **I4** | Tự động giải phóng chỗ khi thoát | Hàm `clients_cleanup_slot()` trong `src/server/clients.c` |
| **I5** | Nickname duy nhất và danh sách online | `protocol_validate_nickname()`, lệnh `USERS` |
| **I6** | Message framing xử lý dính gói | Module `src/common/linebuf.c` |
| **I8** | Tiếng Việt UTF-8 không lỗi | `protocol_sanitize_text()` giữ byte `>= 0x80` |
| **I10** | Không rò rỉ bộ nhớ, an toàn luồng | Quy tắc FD Ownership, `pthread_join`, kiểm thử bằng Valgrind |
| **D1** | Minh chứng đa luồng trực tiếp (Vấn đáp) | Lệnh `/threads` (`/info`) truy vấn `PID/TGID` & `TID/LWP` (`SYS_gettid`) |

---

## 4. Thiết kế hệ thống & Kiến trúc

### 4.1. Kiến trúc phân lớp
```text
+-------------------------------------------------------+
|                 Lớp Ứng dụng (App)                    |
|   Server Main (Accept Loop)   |   Client Main (Poll)  |
+-------------------------------------------------------+
|                 Lớp Điều phối & UI                    |
|   Clients Manager / Handler   |   UI Formatting/Colors|
+-------------------------------------------------------+
|               Lớp Dùng chung (Common)                 |
|   Protocol (Parser/Sanitizer) |   Linebuf (Framing)   |
|   Net Utils (send_all/timeout)|   Thread-safe Logging |
+-------------------------------------------------------+
|                Lớp Hệ thống (POSIX API)               |
|      sys/socket.h, pthread.h, poll.h, unistd.h        |
+-------------------------------------------------------+
```

### 4.2. Quản lý đồng bộ và Khoá
- `g_lock`: Bảo vệ mảng thông tin `g_clients[10]`. Mọi thao tác tìm slot, đổi tên, broadcast, cập nhật số lượng đều diễn ra dưới khoá này.
- `send_lock` (từng client): Đảm bảo các luồng khác nhau không ghi xen kẽ các byte vào cùng một socket descriptor.
- **Thứ tự khoá:** Luôn khoá `g_lock` trước, sau đó mới khoá `send_lock` của client cụ thể. Không bao giờ đảo ngược thứ tự để loại trừ Deadlock.

---

## 5. Kết quả kiểm thử thực nghiệm

Hệ thống đã trải qua bộ kiểm thử tự động gồm 3 tầng: Unit Test, Integration Test và Stress Test.

| Test ID | Nội dung kiểm thử | Kết quả mong đợi | Kết quả thực tế |
|---|---|---|---|
| **UT-01** | Framing: Gửi 1 dòng trọn vẹn | Tách ra đúng 1 dòng | **PASS** |
| **UT-02** | Framing: Gửi 3 lệnh dính liền trong 1 gói | Tách tuần tự đủ 3 lệnh | **PASS** |
| **UT-03** | Framing: Gửi từng byte một | Chỉ trả về khi gặp `\n` | **PASS** |
| **UT-04** | Framing: Dòng vượt quá 1024 byte | Trả về mã lỗi -1 | **PASS** |
| **UT-05** | Validate: Nickname trùng hoặc ký tự lạ | Từ chối, báo lỗi cụ thể | **PASS** |
| **IT-01** | Đua 20 luồng + kết nối 10 client đồng thời | Đúng 10 vào phòng, 10 nhận `REJECT FULL` | **PASS** |
| **IT-02** | Kết nối client thứ 11 khi đủ 10/10 | Nhận `REJECT FULL`, server không sập | **PASS** |
| **IT-03** | 1 client thoát và client mới vào | Client mới vào thành công | **PASS** |
| **IT-04** | Trùng nickname / tên cấm `admin` | Nhận `ERR NAME_TAKEN` / `ERR RESERVED` | **PASS** |
| **IT-05** | E9 (`PM` > 512B), E13 (ANSI escape), E14 (`PM`), E18 (burst), E16 (`Ctrl+C`) | Đầy đủ bảo vệ & server thoát mã 0 | **PASS** |
| **VAL-01**| Quét rò rỉ bộ nhớ & FD bằng Valgrind (`--track-fds=yes` trên `server` + `itest`) | `definitely lost: 0 bytes`, 0 FD rò | **PASS** |
| **SAN-01**| Chạy full `make test` dưới `ASan+UBSan` và `ThreadSanitizer` | 0 lỗi bộ nhớ, 0 data race warning | **PASS** |
| **CPP-01**| Quét tĩnh `make cppcheck` (`--enable=all --check-level=exhaustive`) | 12/12 file sạch lỗi | **PASS** |

---

## 6. Hạn chế và Hướng phát triển
- **Hạn chế:** Mô hình mỗi client 1 luồng phù hợp cho quy mô phòng chat nhỏ (<= 100 người); nếu mở rộng lên hàng chục nghìn kết nối đồng thời sẽ tiêu tốn bộ nhớ stack của hệ thống.
- **Hướng phát triển:**
  1. Chuyển đổi mô hình Server sang kiến trúc bất đồng bộ hướng sự kiện sử dụng `epoll()` kết hợp Thread Pool (Reactor Pattern).
  2. Bổ sung mã hoá bảo mật đường truyền bằng OpenSSL (TLS/SSL).
  3. Bổ sung cơ sở dữ liệu (SQLite/PostgreSQL) để lưu trữ lịch sử tin nhắn và tài khoản người dùng.
