# RVX prototype: native macOS arm64, Rack SDK 2.6.6, C++17.
CXX := clang++
RACK_DIR ?= dep/Rack-SDK
SYPHON_DIR ?= dep/Syphon
SYPHON_LIB ?= $(SYPHON_DIR)/libSyphon.a
CPPFLAGS += -I"$(RACK_DIR)/include" -I"$(RACK_DIR)/dep/include" -I"$(SYPHON_DIR)/include" -Isrc
CXXFLAGS += -std=c++17 -O2 -g -fPIC -Wall -Wextra -Wno-unused-parameter -arch arm64 -mmacosx-version-min=11.0
FRAMEWORKS := -framework Foundation -framework AppKit -framework OpenGL -framework IOSurface -framework CoreVideo
NATIVE_CXXFLAGS := -std=c++17 -O2 -g -Wall -Wextra -Wpedantic -arch arm64 -mmacosx-version-min=11.0
CPP_SOURCES := $(wildcard src/*.cpp src/core/*.cpp src/rack/*.cpp)
MM_SOURCES := $(wildcard src/io/*.mm)
OBJECTS := $(patsubst %.cpp,build/%.o,$(CPP_SOURCES)) $(patsubst %.mm,build/%.o,$(MM_SOURCES))

.PHONY: all deps check-sdk test test-lifecycle test-rack test-syphon benchmark \
	native-validation-tools test-native-coreaudio test-native-syphon-path \
	test-native-validation dist install clean
all: plugin.dylib

deps:
	@test -f "$(RACK_DIR)/include/rack.hpp" || python3 scripts/fetch-rack-sdk.py
	bash scripts/fetch-syphon.sh

check-sdk:
	@test -f "$(RACK_DIR)/include/rack.hpp" || (echo "Set RACK_DIR to Rack SDK 2.6.6 or run make deps"; exit 1)

build/%.o: %.cpp | check-sdk
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

build/%.o: %.mm | check-sdk
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fobjc-arc -fblocks -DGL_SILENCE_DEPRECATION -MMD -MP -c $< -o $@

plugin.dylib: $(OBJECTS) $(SYPHON_LIB)
	$(CXX) -dynamiclib -arch arm64 -mmacosx-version-min=11.0 -undefined dynamic_lookup -L"$(RACK_DIR)" -lRack $(OBJECTS) -Wl,-force_load,"$(SYPHON_LIB)" $(FRAMEWORKS) -o $@
	install_name_tool -change libRack.dylib /tmp/Rack2/libRack.dylib $@
	codesign --force --sign - $@

build/core-test: src/core/Video.cpp src/core/Video.hpp src/io/VideoBackend.hpp tests/core_test.cpp
	@mkdir -p build
	$(CXX) -std=c++17 -O2 -g -Wall -Wextra -pthread src/core/Video.cpp tests/core_test.cpp -o $@

test: build/core-test
	./build/core-test

build/lifecycle-test: src/core/Video.cpp src/core/Video.hpp src/io/VideoBackend.hpp tests/lifecycle_test.cpp
	@mkdir -p build
	$(CXX) -std=c++17 -O2 -g -Wall -Wextra -pthread src/core/Video.cpp tests/lifecycle_test.cpp -o $@

test-lifecycle: build/lifecycle-test
	./build/lifecycle-test

build/rack-adapter-test: src/rack/PublisherNames.hpp src/rack/AdapterDiagnostics.hpp tests/rack_adapter_test.cpp
	@mkdir -p build
	$(CXX) -std=c++17 -O2 -g -Wall -Wextra tests/rack_adapter_test.cpp -o $@

build/rack-host-test: src/core/Video.cpp src/core/Video.hpp src/rack/RackAdapter.cpp src/rack/RackAdapter.hpp src/rack/Modules.cpp src/rack/PublisherNames.hpp src/rack/AdapterDiagnostics.hpp tests/rack_host_test.cpp | check-sdk
	@mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -pthread src/core/Video.cpp src/rack/RackAdapter.cpp tests/rack_host_test.cpp -L"$(RACK_DIR)" -lRack -o $@

test-rack: build/rack-adapter-test build/rack-host-test
	./build/rack-adapter-test
	DYLD_LIBRARY_PATH="$(RACK_DIR)" ./build/rack-host-test

build/benchmark: src/core/Video.cpp src/core/Video.hpp tests/benchmark.cpp
	@mkdir -p build
	$(CXX) -std=c++17 -O2 -g -pthread src/core/Video.cpp tests/benchmark.cpp -o $@

benchmark: build/benchmark
	./build/benchmark --seconds 600

# Syphon test is wired to the same backend and pinned library as the plugin.
build/syphon-test: src/io/SyphonBackend.mm tests/syphon_test.mm $(SYPHON_LIB)
	@mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fobjc-arc -fblocks -DGL_SILENCE_DEPRECATION src/io/SyphonBackend.mm tests/syphon_test.mm -Wl,-force_load,"$(SYPHON_LIB)" $(FRAMEWORKS) -o $@

test-syphon: build/syphon-test
	./build/syphon-test

# These command-line probes are opt-in because they inspect live CoreAudio or
# publish a temporary Syphon server. They are not prerequisites of `make test`.
build/native-coreaudio-probe: tests/native_coreaudio_probe.cpp tests/NativeValidationJson.hpp
	@mkdir -p build
	$(CXX) $(NATIVE_CXXFLAGS) tests/native_coreaudio_probe.cpp \
		-framework CoreAudio -framework CoreFoundation -o $@

build/native-syphon-path-probe: src/io/SyphonBackend.mm src/io/VideoBackend.hpp \
		src/core/Video.hpp tests/native_syphon_path_probe.mm \
		tests/NativeValidationJson.hpp $(SYPHON_LIB)
	@mkdir -p build
	$(CXX) $(NATIVE_CXXFLAGS) -Wno-unused-parameter -fobjc-arc -fblocks \
		-DGL_SILENCE_DEPRECATION -I"$(SYPHON_DIR)/include" \
		src/io/SyphonBackend.mm tests/native_syphon_path_probe.mm \
		-Wl,-force_load,"$(SYPHON_LIB)" $(FRAMEWORKS) -o $@

native-validation-tools: build/native-coreaudio-probe build/native-syphon-path-probe

test-native-coreaudio: build/native-coreaudio-probe
	./build/native-coreaudio-probe --observe-default-output --duration 2 >/dev/null
	@! ./build/native-coreaudio-probe --duration 2 >/dev/null 2>&1
	@! ./build/native-coreaudio-probe --observe-default-output --duration 0 >/dev/null 2>&1
	@! ./build/native-coreaudio-probe --observe-default-output --duration 3600.1 >/dev/null 2>&1

test-native-syphon-path: build/native-syphon-path-probe
	./build/native-syphon-path-probe --loopback --duration 2 --progress 0 >/dev/null
	@! ./build/native-syphon-path-probe --loopback --duration 0 >/dev/null 2>&1
	@! ./build/native-syphon-path-probe --loopback --duration 3600.1 >/dev/null 2>&1
	@! ./build/native-syphon-path-probe --duration 2 >/dev/null 2>&1

test-native-validation: test-native-coreaudio test-native-syphon-path

dist: all
	python3 scripts/package-prototype.py

install: dist
	python3 scripts/install-prototype.py

clean:
	rm -rf build dist plugin.dylib

-include $(OBJECTS:.o=.d)
