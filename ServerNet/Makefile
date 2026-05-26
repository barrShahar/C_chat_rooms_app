CC = gcc
FLAGS = -g -Wall -Wextra -std=c99 -I. -I../db
LDFLAGS = -L../db -lDataStructures

SRCS = TcpServerController.c \
TcpConnectionAcceptor.c \
TcpConnectionHandler.c \
TcpConnectionRecord.c \
network_utils.c

$(shell mkdir -p build)

OBJS = $(patsubst %.c, build/%.o, $(SRCS))
TARGET = build/out.test_app
TCP_CLIENT_TARGET = build/out.tcp_client
TCP_CLIENT_MULTI_TARGET = build/out.multi_client

TEST_DIR = tests
TEST_SRCS = $(TEST_DIR)/test_network_utils.c network_utils.c
TEST_BIN  = build/test_network_utils

all: $(TARGET) $(TCP_CLIENT_TARGET) $(TCP_CLIENT_MULTI_TARGET)

$(TARGET): $(OBJS) build/test_app.o
	$(CC) $(FLAGS) -o $@ $^ $(LDFLAGS)

$(TCP_CLIENT_TARGET): tests/tcp_client.c
	$(CC) $(FLAGS) -o $@ $^

$(TCP_CLIENT_MULTI_TARGET): tests/multi_client.c
	$(CC) $(FLAGS) -o $@ $^

build/%.o: %.c
	$(CC) $(FLAGS) -c -o $@ $<

build/%.o: tests/%.c
	$(CC) $(FLAGS) -c -o $@ $<

test: $(TEST_BIN)

$(TEST_BIN): $(TEST_SRCS)
	$(CC) $(FLAGS) -o $@ $^

clean:
	rm -rf build

.PHONY: all clean test
