CXX ?= g++
CXXFLAGS ?= -std=c++23 -O2 -Wall -Wextra -Wpedantic -Werror -fvisibility=hidden -fvisibility-inlines-hidden -fPIC
CPPFLAGS += -I../../../include
LDFLAGS ?= -shared

BUILD_DIR := build
TARGET := $(BUILD_DIR)/libdomain_mesh_probe.so
PLUGIN_INSTALL_DIR ?= $(HOME)/.config/gridlock/plugins

.PHONY: all install clean

all: $(TARGET)

$(TARGET): domain_mesh_probe.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $< -o $@

install: $(TARGET)
	@mkdir -p "$(PLUGIN_INSTALL_DIR)"
	cp "$(TARGET)" "$(PLUGIN_INSTALL_DIR)/"

clean:
	rm -rf $(BUILD_DIR)
