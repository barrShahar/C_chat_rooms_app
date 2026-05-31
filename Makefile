CC      = gcc
CFLAGS  = -g -Wall -Wextra -std=c11 -D_DEFAULT_SOURCE
INCLUDES = -I. -Iutils -IServerNet -IServerMng -IClientNet -IClient -Idb

BUILD_DIR = build
LIB_DIR   = ServerNet/lib
LDFLAGS   = -L$(LIB_DIR) -ltcpserver -Ldb -lDataStructures -lpthread
# The client links the data-structures archive (HashMap, used by GroupWindows)
# and -lrt for the POSIX message queue. No -lpthread: the client is single-threaded.
CLIENT_LDFLAGS = -LClient/lib -lclientapp -LClientNet/lib -lclientcontroller -Ldb -lDataStructures -lrt
TARGET        = $(BUILD_DIR)/out.serverMain
CLIENT_TARGET = $(BUILD_DIR)/out.client
# Standalone chat-window programs spawned by the client (need -lrt for the queue).
CHAT_SENDER   = $(BUILD_DIR)/out.chat_sender
CHAT_RECEIVER = $(BUILD_DIR)/out.chat_receiver
LOG_DIR   = logs

SRVMNG_SRCS = $(wildcard ServerMng/*.c)
SRVMNG_OBJS = $(patsubst %.c, $(BUILD_DIR)/%.o, $(SRVMNG_SRCS))

UTILS_SRCS = $(wildcard utils/*.c)
UTILS_OBJS = $(patsubst %.c, $(BUILD_DIR)/%.o, $(UTILS_SRCS))

_ := $(shell mkdir -p $(BUILD_DIR)/ServerMng $(BUILD_DIR)/utils)
_ := $(shell mkdir -p $(LOG_DIR))

MAIN_OBJ        = $(BUILD_DIR)/serverMain.o
CLIENT_MAIN_OBJ = $(BUILD_DIR)/clientMain.o

# Subdirectories that own their libraries and must be built before the
# top-level link step. ServerNet produces libtcpserver.a (used via -ltcpserver
# in LDFLAGS); ClientNet produces libclientcontroller.a; Client produces
# libclientapp.a. Without listing them here the linker would fail with
# "cannot find -lxxx" on a clean build.
SUBDIRS = ServerNet ClientNet Client

# .PHONY on the subdir names forces make to always enter each directory even
# though a directory with that name already exists on disk. Without .PHONY,
# make would treat the directory as an up-to-date target and silently skip the
# sub-make, so library sources would never be recompiled when they change.
.PHONY: all clean $(SUBDIRS)

# $(SUBDIRS) is listed before targets so libraries exist by link time.
# Without this ordering a clean build would fail because the archives have
# not been created yet when the link step runs.
all: $(SUBDIRS) $(TARGET) $(CLIENT_TARGET) $(CHAT_SENDER) $(CHAT_RECEIVER)

# $@ expands to the directory name, so one rule drives all subdirectories.
# Each sub-make checks its own sources and only rebuilds what changed.
$(SUBDIRS):
	$(MAKE) -C $@

$(TARGET): $(SRVMNG_OBJS) $(UTILS_OBJS) $(MAIN_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# The client links its own archive plus the ClientController archive, and
# pulls in network_utils.o directly (ClientController references it but the
# archive doesn't bundle it).
#
# The two archives are listed as prerequisites (built first by $(SUBDIRS)) so
# that editing a Client/ or ClientNet/ source relinks out.client. They are
# linked via the -l flags in CLIENT_LDFLAGS, so the recipe names the objects
# explicitly rather than using $^ (which would also pass the .a files).
CLIENT_ARCHIVES = Client/lib/libclientapp.a ClientNet/lib/libclientcontroller.a
$(CLIENT_TARGET): $(CLIENT_MAIN_OBJ) $(UTILS_OBJS) $(CLIENT_ARCHIVES)
	$(CC) $(CFLAGS) -o $@ $(CLIENT_MAIN_OBJ) $(UTILS_OBJS) $(CLIENT_LDFLAGS)

# Each chat-window program is a single .c plus the shared ChatIpc.c (the PID
# report). Built directly into executables; -I. finds ChatIpc.h / config.h.
$(CHAT_SENDER): ChatWindows/chat_sender.c ChatIpc.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ -lrt

$(CHAT_RECEIVER): ChatWindows/chat_receiver.c ChatIpc.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ -lrt

$(BUILD_DIR)/%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# Propagate clean into subdirectories so their build/ and lib/ artifacts are
# removed too. Without this, stale libraries in ServerNet/lib/ and
# ClientNet/lib/ would survive a clean and could mask build errors.
clean:
	rm -rf $(BUILD_DIR)
	for dir in $(SUBDIRS); do $(MAKE) -C $$dir clean; done
