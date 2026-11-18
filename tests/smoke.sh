#!/bin/bash
# Kịch bản kiểm tra khói (smoke test) bằng netcat (nc), CLI port check & client pipe
set -e

PORT=15556
echo "=== BẮT ĐẦU SMOKE TEST VỚI NETCAT & CLIENT ==="

# 0. Kiểm tra validate cổng không hợp lệ (-p abc) phải báo lỗi khác 0
if ./build/server -p abc >/dev/null 2>&1; then
    echo "[FAIL] Server chấp nhận -p abc!"
    exit 1
fi
echo "[PASS] Server từ chối cổng không hợp lệ (-p abc) đúng chuẩn!"

# 1. Khởi động server nền
./build/server -p $PORT -m 2 > /dev/null 2>&1 &
SERVER_PID=$!
sleep 0.3

cleanup() {
    kill -2 $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
}
trap cleanup EXIT

# 1b. Kiểm tra E17: chạy thêm server trùng cổng phải báo lỗi khác 0
if ./build/server -p $PORT >/dev/null 2>&1; then
    echo "[FAIL] Server không báo lỗi khi cổng đang bị chiếm (E17)!"
    exit 1
fi
echo "[PASS] Server từ chối khởi động khi cổng $PORT đang bận (E17)!"

# 2. Kiểm tra client thật chạy qua pipe nhiều lệnh liên tiếp (/users, /threads, /quit)
CLI_OUT=$(printf "Xin chao tu pipe\n/users\n/threads\n/quit\n" | ./build/client 127.0.0.1 $PORT SmokeBot --no-color)
echo "$CLI_OUT" | grep -q "Xin chao tu pipe"
echo "$CLI_OUT" | grep -q "CHẨN ĐOÁN LUỒNG NHÂN LINUX"
echo "[PASS] Client pipe nhiều lệnh (MSG + /users + /threads + /quit) hiển thị đầy đủ!"

# 3. Kiểm tra chào ban đầu qua netcat
RES1=$(echo "" | nc -q 1 127.0.0.1 $PORT | head -n 1)
if [[ "$RES1" =~ ^HELLO ]]; then
    echo "[PASS] Server chào HELLO thành công!"
else
    echo "[FAIL] Server không chào HELLO ($RES1)!"
    exit 1
fi

# 4. Kiểm tra từ chối khi đầy
exec 3<>/dev/tcp/127.0.0.1/$PORT
exec 4<>/dev/tcp/127.0.0.1/$PORT
sleep 0.2

RES_FULL=$(echo "" | nc -q 1 127.0.0.1 $PORT | head -n 1)
if [[ "$RES_FULL" =~ ^REJECT\ FULL ]]; then
    echo "[PASS] Người thứ 3 bị từ chối REJECT FULL chính xác!"
else
    echo "[FAIL] Không nhận được REJECT FULL ($RES_FULL)!"
    exit 1
fi

exec 3>&-
exec 4>&-

echo ">>> SMOKE TEST THÀNH CÔNG! <<<"
