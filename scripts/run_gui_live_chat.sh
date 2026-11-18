#!/usr/bin/env bash
export XDG_RUNTIME_DIR=/run/user/1000
export WAYLAND_DISPLAY=wayland-0
export DISPLAY=:0
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus

gsettings set org.gnome.desktop.session idle-delay 0 2>/dev/null || true
gsettings set org.gnome.desktop.screensaver lock-enabled false 2>/dev/null || true

cd ~/Desktop/chat-app

# Kill any old instances
pkill -f "./build/server" 2>/dev/null || true
pkill -f "./build/client" 2>/dev/null || true
tmux kill-session -t live_chat 2>/dev/null || true
sleep 0.5

# Create tmux session with 4 panes
tmux new-session -d -s live_chat -x 220 -y 55

# Split into Left (Pane 0) and Right (Pane 1)
tmux split-window -h -t live_chat:0.0
# Split Left into Top-Left (Pane 0: Server) and Bottom-Left (Pane 1: Client 11 + ps -eLf)
tmux split-window -v -t live_chat:0.0
# Split Right into Top-Right (Pane 2: Client An) and Bottom-Right (Pane 3: Client Binh)
tmux split-window -v -t live_chat:0.2

# Enable pane titles so it's crystal clear on screen
tmux set-option -t live_chat pane-border-status top
tmux set-option -t live_chat pane-border-format " #[bold]#{pane_index}: #{pane_title} "
tmux select-pane -t live_chat:0.0 -T "SERVER DIEU PHOI (./build/server -p 5555 -m 10 -v)"
tmux select-pane -t live_chat:0.1 -T "KIEM THU CLIENT THU 11 (REJECT FULL) & LUONG NHAN LINUX (ps -eLf)"
tmux select-pane -t live_chat:0.2 -T "CLIENT 1: An (TUONG TAC TRUC TIEP - GO TIN NHAN TAI DAY)"
tmux select-pane -t live_chat:0.3 -T "CLIENT 2: Binh (TUONG TAC TRUC TIEP - NHAN GROUP & WHISPER)"

# 1. Start Server in Pane 0
tmux send-keys -t live_chat:0.0 "cd ~/Desktop/chat-app && ./build/server -p 5555 -m 10 -v" C-m
sleep 0.8

# 2. Start Client 1 (An) in Pane 2 and Client 2 (Binh) in Pane 3
tmux send-keys -t live_chat:0.2 "cd ~/Desktop/chat-app && ./build/client 127.0.0.1 5555 An" C-m
sleep 0.3
tmux send-keys -t live_chat:0.3 "cd ~/Desktop/chat-app && ./build/client 127.0.0.1 5555 Binh" C-m
sleep 0.3

# 3. Start 8 background clients (User_03 .. User_10) so the room reaches 10/10 capacity!
for i in $(seq 3 10); do
    name=$(printf "User_%02d" "$i")
    (
        echo "JOIN $name"
        sleep 3600
    ) | nc 127.0.0.1 5555 >/dev/null 2>&1 &
    sleep 0.1
done
sleep 0.5

# 4. In Pane 1 (Bottom-Left), test Client 11 getting REJECT FULL and show ps -eLf kernel threads
tmux send-keys -t live_chat:0.1 "cd ~/Desktop/chat-app && clear" C-m
sleep 0.2
tmux send-keys -t live_chat:0.1 "echo '=== 1. THU KET NOI NGUOI THU 11 KHI PHONG DA DU 10/10 ===' && ./build/client 127.0.0.1 5555 NguoiThu11 || true" C-m
sleep 1.0
tmux send-keys -t live_chat:0.1 "echo '' && echo '=== 2. KIEM TRA LUONG THUC TE TRONG NHAN LINUX (ps -eLf) ===' && ps -eLf -q \$(pgrep -f './build/server')" C-m
sleep 0.8

# 5. Live conversation between An (Pane 2) and Binh (Pane 3)
tmux send-keys -t live_chat:0.2 "Chao ca lop! Minh la An, dang test chat TCP/IP C11 tren Ubuntu!" C-m
sleep 0.5
tmux send-keys -t live_chat:0.3 "Chao An nha! Minh la Binh, nhan duoc tin nhan broadcast roi!" C-m
sleep 0.5
tmux send-keys -t live_chat:0.2 "/w Binh Nhan tin rieng (Whisper) thu xem sao nhe!" C-m
sleep 0.5
tmux send-keys -t live_chat:0.2 "/threads" C-m
sleep 0.5

# Focus Pane 2 (An) so the user can type immediately in the window
tmux select-pane -t live_chat:0.2

# Open ptyxis maximized attached to live_chat tmux session
pkill -f ptyxis 2>/dev/null || true
sleep 0.5
systemd-run --user --scope ptyxis --maximize -- tmux attach-session -t live_chat >/dev/null 2>&1 &
sleep 3
