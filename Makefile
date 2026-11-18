CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wstrict-prototypes -Wmissing-prototypes -Werror -g -O2 -pthread -Iinclude -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
LDFLAGS ?= -pthread

BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
SRC_COMMON = src/common/log.c src/common/net_utils.c src/common/linebuf.c src/common/protocol.c
SRC_SERVER = src/server/clients.c src/server/handler.c src/server/main.c
SRC_CLIENT = src/client/ui.c src/client/main.c

OBJ_COMMON = $(patsubst src/common/%.c, $(OBJ_DIR)/common/%.o, $(SRC_COMMON))
OBJ_SERVER = $(patsubst src/server/%.c, $(OBJ_DIR)/server/%.o, $(SRC_SERVER))
OBJ_CLIENT = $(patsubst src/client/%.c, $(OBJ_DIR)/client/%.o, $(SRC_CLIENT))

.PHONY: all clean server client test run-tests asan tsan valgrind cppcheck run-server

all: server client

server: $(BUILD_DIR)/server
client: $(BUILD_DIR)/client

$(OBJ_DIR)/common/%.o: src/common/%.c
	@mkdir -p $(OBJ_DIR)/common
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/server/%.o: src/server/%.c
	@mkdir -p $(OBJ_DIR)/server
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/client/%.o: src/client/%.c
	@mkdir -p $(OBJ_DIR)/client
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/server: $(OBJ_COMMON) $(OBJ_SERVER)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/client: $(OBJ_COMMON) $(OBJ_CLIENT)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# --- KIỂM THỬ ---
test: server client $(BUILD_DIR)/test_linebuf $(BUILD_DIR)/test_protocol $(BUILD_DIR)/itest run-tests

run-tests: server client $(BUILD_DIR)/test_linebuf $(BUILD_DIR)/test_protocol $(BUILD_DIR)/itest
	@echo "=== CHẠY UNIT TEST ==="
	./$(BUILD_DIR)/test_linebuf
	./$(BUILD_DIR)/test_protocol
	@echo "=== CHẠY INTEGRATION TEST ==="
	./$(BUILD_DIR)/itest
	@echo "=== CHẠY SMOKE TEST (BASH/NC/CLIENT) ==="
	@bash tests/smoke.sh
	@echo ">>> TẤT CẢ TEST ĐÃ PASS! <<<"

$(BUILD_DIR)/test_linebuf: tests/test_linebuf.c src/common/linebuf.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/test_protocol: tests/test_protocol.c src/common/protocol.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/itest: tests/itest.c src/common/net_utils.c src/common/linebuf.c src/common/log.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# --- SANITIZERS (biên dịch sạch cả server, client và toàn bộ test) ---
asan:
	$(MAKE) clean
	$(MAKE) test CFLAGS="$(CFLAGS) -fsanitize=address,undefined" LDFLAGS="$(LDFLAGS) -fsanitize=address,undefined"
	@echo ">>> ASan + UBSan đã hoàn tất 100% không lỗi! <<<"

tsan:
	$(MAKE) clean
	$(MAKE) test CFLAGS="$(CFLAGS) -fsanitize=thread" LDFLAGS="$(LDFLAGS) -fsanitize=thread"
	@echo ">>> ThreadSanitizer đã hoàn tất với 0 data race! <<<"

# --- VALGRIND & CPPCHECK ---
valgrind: server client $(BUILD_DIR)/test_linebuf $(BUILD_DIR)/test_protocol $(BUILD_DIR)/itest
	valgrind --leak-check=full --show-leak-kinds=all --track-fds=yes --error-exitcode=1 ./$(BUILD_DIR)/test_linebuf
	valgrind --leak-check=full --show-leak-kinds=all --track-fds=yes --error-exitcode=1 ./$(BUILD_DIR)/test_protocol
	valgrind --leak-check=full --show-leak-kinds=all --track-fds=yes --trace-children=yes --error-exitcode=1 ./$(BUILD_DIR)/itest
	@echo ">>> Valgrind (kể cả server & FD tracking) hoàn toàn sạch sẽ! <<<"

# Bỏ qua thông tin checkersReport và constParameterPointer bắt buộc cho chữ ký chuẩn POSIX (pthread/main)
cppcheck:
	cppcheck --enable=all --check-level=exhaustive --suppress=missingIncludeSystem --suppress=constParameterPointer --suppress=checkersReport --error-exitcode=1 -Iinclude src tests

run-server: server
	./$(BUILD_DIR)/server -p 5555 -m 10 -v

clean:
	rm -rf $(BUILD_DIR) logs/*.log
