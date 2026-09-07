# RVX prototype: native macOS arm64, Rack SDK 2.6.6, C++17.
CXX := clang++
RACK_DIR ?= dep/Rack-SDK
SYPHON_DIR ?= dep/Syphon
SYPHON_LIB ?= $(SYPHON_DIR)/libSyphon.a
CPPFLAGS += -Isrc -I"$(RACK_DIR)/include" -I"$(RACK_DIR)/dep/include" -I"$(SYPHON_DIR)/include"
CXXFLAGS += -std=c++17 -O2 -g -fPIC -Wall -Wextra -Wno-unused-parameter -arch arm64 -mmacosx-version-min=11.0
FRAMEWORKS := -framework Foundation -framework AppKit -framework OpenGL -framework IOSurface -framework CoreVideo
CPP_SOURCES := $(wildcard src/*.cpp src/core/*.cpp src/rack/*.cpp)
MM_SOURCES := $(wildcard src/io/*.mm)
OBJECTS := $(patsubst %.cpp,build/%.o,$(CPP_SOURCES)) $(patsubst %.mm,build/%.o,$(MM_SOURCES))

.PHONY: all deps check-sdk test test-syphon benchmark dist install clean
all: plugin.dylib

deps:
	python3 scripts/fetch-rack-sdk.py
	python3 scripts/fetch-syphon.py

check-sdk:
	@test -f "$(RACK_DIR)/include/rack.hpp" || (echo "Set RACK_DIR to Rack SDK 2.6.6 or run make deps"; exit 1)

build/%.o: %.cpp | check-sdk
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

build/%.o: %.mm | check-sdk
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fobjc-arc -fblocks -DGL_SILENCE_DEPRECATION -MMD -MP -c $< -o $@

plugin.dylib: $(OBJECTS) $(SYPHON_LIB)
	$(CXX) -dynamiclib -arch arm64 -mmacosx-version-min=11.0 -undefined dynamic_lookup -L"$(RACK_DIR)" -lRack $(OBJECTS) $(SYPHON_LIB) $(FRAMEWORKS) -o $@
	install_name_tool -change libRack.dylib /tmp/Rack2/libRack.dylib $@
	codesign --force --sign - $@

build/core-test: src/core/Video.cpp src/core/Video.hpp src/io/VideoBackend.hpp tests/core_test.cpp
	@mkdir -p build
	$(CXX) -std=c++17 -O2 -g -Wall -Wextra -pthread src/core/Video.cpp tests/core_test.cpp -o $@

test: build/core-test
	./build/core-test

build/benchmark: src/core/Video.cpp src/core/Video.hpp tests/benchmark.cpp
	@mkdir -p build
	$(CXX) -std=c++17 -O2 -g -pthread src/core/Video.cpp tests/benchmark.cpp -o $@

benchmark: build/benchmark
	./build/benchmark --seconds 600

# Syphon test is wired to the same backend and pinned library as the plugin.
build/syphon-test: src/io/SyphonBackend.mm tests/syphon_test.mm $(SYPHON_LIB)
	@mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fobjc-arc -fblocks -DGL_SILENCE_DEPRECATION src/io/SyphonBackend.mm tests/syphon_test.mm $(SYPHON_LIB) $(FRAMEWORKS) -o $@

test-syphon: build/syphon-test
	./build/syphon-test

dist: all
	python3 scripts/package-prototype.py

install: dist
	python3 scripts/install-prototype.py

clean:
	rm -rf build dist plugin.dylib

-include $(OBJECTS:.o=.d)
