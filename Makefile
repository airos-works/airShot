# airShot: native Haiku build with the development tools bundled with Haiku.
#   make            builds build-haiku/airShot and the input_server filter
#   make package    builds artifacts/airshot-<version>-<arch>.hpkg
#   make check-host builds and runs the platform independent tests on Linux
.DEFAULT_GOAL := all
CXX ?= g++
BUILD ?= build-haiku
CPPFLAGS += -Isrc $(CROSS_CPPFLAGS)
CXXFLAGS ?= -O2 -g
# Resource tools; tools/build-cross.sh points these at host builds.
RC ?= rc
XRES ?= xres
MIMESET ?= mimeset
CXXFLAGS += -std=c++17 -Wall -Wextra -Wno-multichar -Wno-unused-parameter -Wno-sign-compare

# The application. Private headers: WindowInfo.h (window list), WindowPrivate.h
# (the overlay's window type), ToolBar.h (libshared).
APP_SRC = $(wildcard src/*.cpp) $(wildcard src/capture/*.cpp) $(wildcard src/editor/*.cpp) \
	$(wildcard src/ui/*.cpp)
APP_OBJ = $(APP_SRC:%.cpp=$(BUILD)/%.o)
APP_CPPFLAGS = -I/boot/system/develop/headers/private/interface \
	-I/boot/system/develop/headers/private/shared \
	-I/boot/system/develop/headers/private/app
APP_LIBS = -lbe -lgame -ltracker -ltranslation -lshared

# The input_server filter add-on; BInputServerFilter is resolved from
# input_server itself when the add-on is loaded.
FILTER_SRC = filter/airShotFilter.cpp src/HotKey.cpp src/Settings.cpp
FILTER_OBJ = $(FILTER_SRC:%.cpp=$(BUILD)/filter/%.o)
FILTER_CPPFLAGS = -I/boot/system/develop/headers/private/storage
FILTER_LIBS = -lbe

.PHONY: all app filter package clean icon tool-icons icon-dump capture-timing check check-host

all: $(BUILD)/airShot $(BUILD)/airShot_filter
app: $(BUILD)/airShot
filter: $(BUILD)/airShot_filter

$(BUILD)/airShot: $(APP_OBJ) resources/airShot.rdef resources/branding/airshot-icon.hvif
	$(CXX) $(APP_LDFLAGS) -o $@.new $(APP_OBJ) $(APP_LIBS) -Wl,--export-dynamic $(APP_LDEND)
	$(RC) -o $(BUILD)/airShot.rsrc resources/airShot.rdef
	$(XRES) -o $@.new $(BUILD)/airShot.rsrc
	$(MIMESET) -f $@.new
	mv $@.new $@

$(BUILD)/airShot_filter: $(FILTER_OBJ)
	$(CXX) $(FILTER_LDFLAGS) -shared -o $@ $(FILTER_OBJ) $(FILTER_LIBS) $(FILTER_LDEND)

$(BUILD)/filter/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(FILTER_CPPFLAGS) $(CXXFLAGS) -fPIC -MMD -MP -c $< -o $@

$(BUILD)/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(APP_CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

# Platform independent tests (HotKey) build anywhere.
TEST_SRC = tests/HotKeyTests.cpp src/HotKey.cpp
$(BUILD)/hotkey_tests: $(TEST_SRC)
	mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $(TEST_SRC)

check: $(BUILD)/hotkey_tests
	$(BUILD)/hotkey_tests

check-host: check

icon:
	python3 tools/make-icon.py resources/branding/airshot-icon.hvif resources/branding/airshot-icon-preview.png

# Optional artwork regeneration; needs Python fontTools on the host.
tool-icons:
	python3 tools/make-tool-icons.py

$(BUILD)/icon_dump: tests/IconDump.cpp $(BUILD)/src/editor/ToolIcons.o
	$(CXX) $(CPPFLAGS) $(APP_CPPFLAGS) $(CXXFLAGS) $(APP_LDFLAGS) -o $@ $^ -lbe -ltranslation $(APP_LDEND)

icon-dump: $(BUILD)/icon_dump

$(BUILD)/capture_timing: tests/CaptureTiming.cpp $(BUILD)/src/capture/ScreenCapture.o
	$(CXX) $(CPPFLAGS) $(APP_CPPFLAGS) $(CXXFLAGS) $(APP_LDFLAGS) -o $@ $^ -lbe -lgame $(APP_LDEND)

capture-timing: $(BUILD)/capture_timing

package: all
	bash tools/package-haiku.sh

clean:
	rm -rf $(BUILD)

-include $(APP_OBJ:.o=.d) $(FILTER_OBJ:.o=.d)

# Native UI regression checks require a running Haiku app_server.
.PHONY: check-ui
$(BUILD)/ui_tests: tests/UITests.cpp $(filter-out $(BUILD)/src/main.o $(BUILD)/src/App.o,$(APP_OBJ))
	$(CXX) $(CPPFLAGS) $(APP_CPPFLAGS) $(CXXFLAGS) $(APP_LDFLAGS) -o $@ $^ $(APP_LIBS) $(APP_LDEND)
check-ui: $(BUILD)/ui_tests
	$(BUILD)/ui_tests
