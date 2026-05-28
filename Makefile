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
_ := $(shell mkdir -p $(LOG_DIR))

MAIN_OBJ = $(BUILD_DIR)/serverMain.o

# Subdirectories that own their libraries and must be built before the
# top-level link step. ServerNet produces libtcpserver.a (used via -ltcpserver
# in LDFLAGS); ClientNet produces libclientcontroller.a. Without listing them
# here the linker would fail with "cannot find -ltcpserver" on a clean build.
SUBDIRS = ServerNet ClientNet

# .PHONY on the subdir names forces make to always enter each directory even
# though a directory with that name already exists on disk. Without .PHONY,
# make would treat the directory as an up-to-date target and silently skip the
# sub-make, so library sources would never be recompiled when they change.
.PHONY: all clean $(SUBDIRS)

# $(SUBDIRS) is listed before $(TARGET) so libraries exist by link time.
# Without this ordering a clean build would fail because libtcpserver.a has
# not been created yet when $(TARGET) tries to link against it.
all: $(SUBDIRS) $(TARGET)

# $@ expands to the directory name, so one rule drives all subdirectories.
# Each sub-make checks its own sources and only rebuilds what changed.
$(SUBDIRS):
	$(MAKE) -C $@

$(TARGET): $(SRVMNG_OBJS) $(UTILS_OBJS) $(MAIN_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# Propagate clean into subdirectories so their build/ and lib/ artifacts are
# removed too. Without this, stale libraries in ServerNet/lib/ and
# ClientNet/lib/ would survive a clean and could mask build errors.
clean:
	rm -rf $(BUILD_DIR)
	for dir in $(SUBDIRS); do $(MAKE) -C $$dir clean; done
