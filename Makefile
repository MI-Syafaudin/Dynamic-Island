CXX ?= g++
CC ?= gcc
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -pthread
CFLAGS ?= -O2 -Wall

PKG_DEPS = wayland-client cairo pango pangocairo
PKG_CFLAGS = $(shell pkg-config --cflags $(PKG_DEPS))
PKG_LIBS = $(shell pkg-config --libs $(PKG_DEPS)) -lm

BUILD_DIR = build
BIN = $(BUILD_DIR)/dynamic-island

SRCS_CPP = \
	src/main.cpp \
	src/config.cpp \
	src/hyprland_ipc.cpp \
	src/ipc_server.cpp \
	src/wayland.cpp \
	src/renderer.cpp \
	src/app.cpp \
	src/modules/clock_module.cpp \
	src/modules/workspace_module.cpp \
	src/modules/window_module.cpp \
	src/modules/audio_module.cpp \
	src/modules/media_module.cpp \
	src/modules/battery_module.cpp \
	src/modules/network_module.cpp \
	src/modules/system_module.cpp \
	src/modules/screenshot_module.cpp \
	src/modules/notification_module.cpp \
	src/modules/quickaction_module.cpp

SRCS_C = \
	src/protocols/wlr-layer-shell-protocol.c \
	src/protocols/xdg-shell-protocol.c

OBJS_CPP = $(patsubst src/%.cpp, $(BUILD_DIR)/%.o, $(SRCS_CPP))
OBJS_C = $(patsubst src/%.c, $(BUILD_DIR)/%.o, $(SRCS_C))
OBJS = $(OBJS_CPP) $(OBJS_C)

all: $(BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR) $(BUILD_DIR)/protocols $(BUILD_DIR)/modules

$(BUILD_DIR)/protocols/%.o: src/protocols/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -c $< -o $@

$(BUILD_DIR)/modules/%.o: src/modules/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(PKG_CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: src/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(PKG_CFLAGS) -c $< -o $@

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@ $(PKG_LIBS)

clean:
	rm -rf $(BUILD_DIR)

install: $(BIN)
	install -d $(DESTDIR)/usr/local/bin
	install -m 755 $(BIN) $(DESTDIR)/usr/local/bin/dynamic-island
	install -d $(DESTDIR)/etc/xdg/dynamic-island
	install -m 644 config/config.json $(DESTDIR)/etc/xdg/dynamic-island/config.json

user-install: $(BIN)
	install -d $(HOME)/.local/bin
	install -m 755 $(BIN) $(HOME)/.local/bin/dynamic-island
	install -d $(HOME)/.config/dynamic-island
	install -m 644 config/config.json $(HOME)/.config/dynamic-island/config.json

.PHONY: all clean install user-install
