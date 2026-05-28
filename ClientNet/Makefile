CC = gcc
FLAGS = -g -Wall -Wextra -std=c11 -D_DEFAULT_SOURCE \
-I. \
-I../utils \
-I../config

SRCS = ClientController.c
OBJS = $(patsubst %.c, build/%.o, $(SRCS))

LIB_TARGET = lib/libclientcontroller.a
TEST_TARGET = build/out.test_client_net

$(shell mkdir -p build lib)

all: $(LIB_TARGET) $(TEST_TARGET)

$(LIB_TARGET): $(OBJS)
	ar rcs $@ $^

$(TEST_TARGET): $(OBJS) build/test_client_net.o build/network_utils.o
	$(CC) $(FLAGS) -o $@ $^

build/%.o: %.c
	$(CC) $(FLAGS) -c -o $@ $<

build/%.o: tests/%.c
	$(CC) $(FLAGS) -c -o $@ $<

build/%.o: ../utils/%.c
	$(CC) $(FLAGS) -c -o $@ $<

valgrind: all
	valgrind --leak-check=full --track-origins=yes --fair-sched=yes ./$(TEST_TARGET)

clean:
	rm -rf build

.PHONY: all clean valgrind
