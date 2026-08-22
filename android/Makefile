CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11 -D_DEFAULT_SOURCE

BUILD_DIR := build-host
TRANSPORT_SRC := amy-service/src/main/cpp/amy_socket_transport.c
TRANSPORT_HDR := amy-service/src/main/cpp/amy_socket_transport.h
TEST_SRC := test_socket_transport.c
TEST_BIN := $(BUILD_DIR)/test_socket_transport

.PHONY: host-test clean

host-test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_SRC) $(TRANSPORT_SRC) $(TRANSPORT_HDR)
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -pthread -Iamy-service/src/main/cpp \
		$(TEST_SRC) $(TRANSPORT_SRC) -o $(TEST_BIN)

clean:
	rm -rf $(BUILD_DIR)
