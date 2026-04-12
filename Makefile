# -----------------------------------------------------------------------
# TorChat – P2P chat with Raylib/raygui frontend
# Raylib is built from the local submodule (raylib/src) — no system install needed.
# SQLite3 must be installed system-wide:
#   Linux:  sudo apt install libsqlite3-dev libgl1-mesa-dev libx11-dev
#   macOS:  brew install sqlite3
# -----------------------------------------------------------------------

CC      = gcc
TARGET  = torchat

RAYLIB_A = raylib/src/libraylib.a

SRCS    = main.c \
          backend.c \
          net.c \
          peer.c \
          event_loop.c \
          UI/main_ui.c \
          storage.c

CFLAGS  = -Wall -Wextra -std=c11 -O2 \
          -I. -IUI -IUI/include -Iraylib/src

UNAME := $(shell uname)

ifeq ($(UNAME), Darwin)
    LDFLAGS = -framework OpenGL -framework Cocoa \
              -framework IOKit -framework CoreAudio -framework CoreVideo \
              -lsqlite3
else
    LDFLAGS = -lGL -lm -lpthread -ldl -lrt -lX11 -lsqlite3
endif

.PHONY: all clean raylib

all: $(TARGET)

# Build raylib static library from the submodule if not already built
$(RAYLIB_A):
	$(MAKE) -C raylib/src PLATFORM=PLATFORM_DESKTOP

# Link raylib archive directly instead of -lraylib
$(TARGET): $(RAYLIB_A) $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $@ $(RAYLIB_A) $(LDFLAGS)

clean:
	rm -f $(TARGET)

# Also expose a target to rebuild raylib from scratch
raylib:
	$(MAKE) -C raylib/src clean
	$(MAKE) -C raylib/src PLATFORM=PLATFORM_DESKTOP
