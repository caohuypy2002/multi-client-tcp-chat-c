#!/bin/bash
# Kịch bản tự động mở 10 Client trên cùng 1 màn hình tmux để demo trước giảng viên
SESSION="chat_demo"
HOST=${1:-127.0.0.1}
PORT=${2:-5555}

if ! command -v tmux &> /dev/null; then
    echo "Lỗi: Chưa cài đặt tmux. Vui lòng chạy: sudo apt install tmux"
    exit 1
fi

tmux kill-session -t $SESSION 2>/dev/null || true

# Tạo session mới với client 1
tmux new-session -d -s $SESSION "./build/client $HOST $PORT User_1"

# Mở thêm 9 pane cho 9 client còn lại
for i in {2..10}; do
    tmux split-window -t $SESSION "./build/client $HOST $PORT User_$i"
    tmux select-layout -t $SESSION tiled
done

echo "Đã mở 10 client trong session tmux '$SESSION'."
echo "Đang kết nối vào tmux... (Để thoát tmux, nhấn Ctrl+B rồi gõ :kill-session)"
tmux attach-session -t $SESSION
