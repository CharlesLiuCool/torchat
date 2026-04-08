# -----------------------------------------------------------------------
# TorChat – P2P chat with Raylib/raygui frontend
# Requires: raylib installed system-wide (e.g. brew install raylib / apt install libraylib-dev)
# -----------------------------------------------------------------------

CC      = gcc
TARGET  = torchat

SRCS    = main.c \
          backend.c \
          net.c \
          UI/main_ui.c

CFLAGS  = -Wall -Wextra -std=c11 -O2 \
          -I. -IUI

# Adjust LDFLAGS for your platform:
#   macOS:  -framework OpenGL -framework Cocoa -framework IOKit -framework CoreAudio -framework CoreVideo
#   Linux:  -lGL -lm -lpthread -ldl -lrt -lX11

UNAME := $(shell uname)

ifeq ($(UNAME), Darwin)
    LDFLAGS = -lraylib \
              -framework OpenGL -framework Cocoa \
              -framework IOKit -framework CoreAudio -framework CoreVideo
else
    LDFLAGS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
endif

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGET)