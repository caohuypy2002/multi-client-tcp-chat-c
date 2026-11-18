#!/bin/bash
# Kịch bản chạy nhanh: Khởi động server trong nền và mở client đầu tiên
PORT=${1:-5555}

make all
echo "Khởi động Server trên cổng $PORT..."
./build/server -p $PORT &
SERVER_PID=$!
sleep 0.5

echo "Server đang chạy với PID=$SERVER_PID"
echo "Bật Client kết nối..."
./build/client 127.0.0.1 $PORT

kill -2 $SERVER_PID 2>/dev/null || true
