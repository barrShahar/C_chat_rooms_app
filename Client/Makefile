CC    = gcc
FLAGS = -g -Wall -Wextra -std=c11 -D_DEFAULT_SOURCE \
        -I. \
        -I.. \
        -I../ClientNet \
        -I../utils \
        -I../db

SRCS = ClientApp.c Menu.c GroupWindows.c
OBJS = $(patsubst %.c, build/%.o, $(SRCS))

LIB_TARGET  = lib/libclientapp.a
TEST_TARGET = build/out.test_menu

$(shell mkdir -p build lib)

all: $(LIB_TARGET) $(TEST_TARGET)

$(LIB_TARGET): $(OBJS)
	ar rcs $@ $^

$(TEST_TARGET): build/test_menu.o build/Menu.o
	$(CC) $(FLAGS) -o $@ $^

build/%.o: %.c
	$(CC) $(FLAGS) -c -o $@ $<

build/%.o: tests/%.c
	$(CC) $(FLAGS) -c -o $@ $<

test: $(TEST_TARGET)
	@./$(TEST_TARGET)

valgrind: $(TEST_TARGET)
	valgrind --leak-check=full --track-origins=yes ./$(TEST_TARGET)

clean:
	rm -rf build lib

.PHONY: all clean test valgrind
