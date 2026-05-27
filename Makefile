CC      = gcc
CFLAGS  = -g -Wall -Wextra -std=c11 -D_DEFAULT_SOURCE
INCLUDES = -I. -Iutils -IServerNet -IServerMng -Idb

BUILD_DIR = build
LIB_DIR   = ServerNet/lib
LDFLAGS   = -L$(LIB_DIR) -ltcpserver -Ldb -lDataStructures -lpthread
TARGET    = $(BUILD_DIR)/out.serverMain
LOG_DIR   = logs

SRVMNG_SRCS = $(wildcard ServerMng/*.c)
SRVMNG_OBJS = $(patsubst %.c, $(BUILD_DIR)/%.o, $(SRVMNG_SRCS))

UTILS_SRCS = $(wildcard utils/*.c)
UTILS_OBJS = $(patsubst %.c, $(BUILD_DIR)/%.o, $(UTILS_SRCS))

_ := $(shell mkdir -p $(BUILD_DIR)/ServerMng $(BUILD_DIR)/utils)
# _ := $(shell cd ServerNet && $(MAKE) && $(MAKE) clean 2>&1)
_ := $(shell mkdir -p $(LOG_DIR))

MAIN_OBJ = $(BUILD_DIR)/serverMain.o

all: $(TARGET)

$(TARGET): $(SRVMNG_OBJS) $(UTILS_OBJS) $(MAIN_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
