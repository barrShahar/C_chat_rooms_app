CC     = gcc
FLAGS  = -g -Wall -Wextra -std=c11 -D_DEFAULT_SOURCE \
         -I.. \
         -I../utils

BUILD_DIR = build
TARGET    = $(BUILD_DIR)/test_network_protocol

$(shell mkdir -p $(BUILD_DIR))

all: $(TARGET)

$(TARGET): test_network_protocol.c
	$(CC) $(FLAGS) -o $@ $<

run: all
	@./$(TARGET)

valgrind: all
	valgrind --leak-check=full --track-origins=yes ./$(TARGET)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all run valgrind clean
