#!/usr/bin/env bash
# Script tu dong bien dich, chay kiem thu va khoi dong Server + Client tren Ubuntu VM
set -e

echo "=================================================================="
echo "  CHUAN BI DEMO TRUC TIEP CHAT APPLICATION (C11 / POSIX SOCKET)   "
echo "=================================================================="

# Copy tu Shared Folder (/mnt/chat-app hoac /media/sf_chat-app) ve ~/chat-app
SRC_DIR=""
if [ -d "/mnt/chat-app/src" ]; then
    SRC_DIR="/mnt/chat-app"
elif [ -d "/media/sf_chat-app/src" ]; then
    SRC_DIR="/media/sf_chat-app"
else
    SRC_DIR="$(cd "$(dirname "$0")/.." && pwd)"
fi

WORK_DIR="$HOME/chat-app"
if [ "$SRC_DIR" != "$WORK_DIR" ]; then
    rm -rf "$WORK_DIR"
    mkdir -p "$WORK_DIR"
    cp -r "$SRC_DIR"/* "$WORK_DIR"/
fi

cd "$WORK_DIR"
chmod +x scripts/*.sh

echo "[1/3] Bien dich toan bo du an voi co canh bao nghiem ngat (-Wall -Wextra -Werror)..."
make clean && make all

echo ""
echo "[2/3] Chay bo kiem thu tu dong (Unit Test + Integration Test + Smoke Test)..."
make test

echo ""
echo "[3/3] Khoi dong mo hinh 10 Clients + 1 Server tren tmux..."
bash scripts/run_10_clients.sh 127.0.0.1 5555
