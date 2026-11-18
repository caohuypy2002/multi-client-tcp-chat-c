#!/usr/bin/env bash
# Script chạy toàn bộ bộ kiểm thử thực tế trên Ubuntu Desktop
export XDG_RUNTIME_DIR=/run/user/1000
export WAYLAND_DISPLAY=wayland-0
export DISPLAY=:0
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus

gsettings set org.gnome.desktop.session idle-delay 0 2>/dev/null || true

rm -rf ~/Desktop/chat-app
cp -r /mnt/chat-app ~/Desktop/chat-app
cd ~/Desktop/chat-app
chmod +x scripts/*.sh tests/*.sh

cat << 'EOF' > /tmp/run_tests_view.sh
#!/usr/bin/env bash
set -e
cd ~/Desktop/chat-app
echo "=== [1/5] BUILD & UNIT + INTEGRATION + SMOKE TESTS ==="
make clean && make test
echo "=== [2/5] CPPCHECK STATIC ANALYSIS ==="
make cppcheck
echo "=== [3/5] VALGRIND (--track-fds=yes) ==="
make valgrind
echo "=== [4/5] ADDRESS SANITIZER + UBSAN ==="
make asan
echo "=== [5/5] THREAD SANITIZER ==="
make tsan
make clean && make all
echo ""
echo "======================================================================"
echo " [HOAN TAT] TOAN BO 5 TANG KIEM THU THUC TE DA PASS 100%!"
echo "======================================================================"
exec bash
EOF
chmod +x /tmp/run_tests_view.sh

pkill -f ptyxis 2>/dev/null || true
sleep 0.5
systemd-run --user --scope ptyxis --maximize -- /tmp/run_tests_view.sh >/dev/null 2>&1 &
sleep 3
