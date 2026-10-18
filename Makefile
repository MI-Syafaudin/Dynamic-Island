CXX ?= g++
CC ?= gcc
CXXFLAGS ?= -std=c++20 -O3 -flto -Wall -Wextra -pthread
CFLAGS ?= -O3 -flto -Wall

PKG_DEPS = wayland-client cairo pango pangocairo luajit
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
	install -m 755 scripts/sync_dark_mode.lua $(DESTDIR)/usr/local/bin/dynamic-island-sync-dark
	install -m 755 scripts/power_menu.lua $(DESTDIR)/usr/local/bin/dynamic-island-powermenu
	install -d $(DESTDIR)/etc/xdg/dynamic-island
	install -m 644 config/config.lua $(DESTDIR)/etc/xdg/dynamic-island/config.lua

user-install: $(BIN)
	install -d $(HOME)/.local/bin
	install -m 755 $(BIN) $(HOME)/.local/bin/dynamic-island
	install -m 755 scripts/power_menu.lua $(HOME)/.local/bin/dynamic-island-powermenu
	install -m 755 scripts/sync_dark_mode.lua $(HOME)/.local/bin/dynamic-island-sync-dark
	install -d $(HOME)/.config/dynamic-island
	install -d $(HOME)/.config/dynamic-island/scripts
	install -m 755 scripts/sync_dark_mode.lua $(HOME)/.config/dynamic-island/scripts/sync_dark_mode.lua
	install -m 755 scripts/power_menu.lua $(HOME)/.config/dynamic-island/scripts/power_menu.lua
	install -d $(HOME)/.config/dynamic-island/shaders
	install -m 644 config/config.lua $(HOME)/.config/dynamic-island/config.lua
	install -m 644 shaders/nightmode.frag $(HOME)/.config/dynamic-island/shaders/nightmode.frag

.PHONY: all clean install user-install
