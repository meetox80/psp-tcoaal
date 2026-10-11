PSPDEV ?= $(CURDIR)/_deps/pspdev
ZIG ?= $(CURDIR)/_deps/zig/zig
JOBS ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
OUT ?= out
GAME ?=
VOICES ?= voices

export PSPDEV
export PATH := $(PSPDEV)/bin:$(PATH)

BUILDER := build/builder-native/tcoaal-builder
ENGINE := build/psp/EBOOT.PBP

.PHONY: all help toolchain zig engine desktop builder test run play release web web-serve clean

all: builder

help:
	@echo "make                build the PSP engine and the builder for this machine"
	@echo "make toolchain      download the pspdev toolchain into _deps/pspdev"
	@echo "make engine         build the PSP engine ($(ENGINE))"
	@echo "make desktop        build the desktop test build (needs SDL2, libvorbis, zlib)"
	@echo "make builder        build the builder for this machine ($(BUILDER))"
	@echo "make run            run the builder; GAME=path/to/game, OUT=folder and VOICES=clip folder are optional"
	@echo "make play           run the built EBOOT in the desktop build (OUT=folder used by make run)"
	@echo "make test           run the builder unit tests"
	@echo "make release        cross-compile the builder for Linux, Windows and macOS into build/builder"
	@echo "make web            build the in-browser builder page into build/web"
	@echo "make web-serve      serve build/web on http://localhost:8080"
	@echo "make clean          remove build/"

toolchain: $(PSPDEV)/bin/psp-gcc

$(PSPDEV)/bin/psp-gcc:
	./scripts/setup-toolchain.sh

zig: $(ZIG)

$(ZIG):
	./scripts/setup-zig.sh

engine: toolchain
	cmake -S engine -B build/psp -DCMAKE_TOOLCHAIN_FILE=$(PSPDEV)/psp/share/pspdev.cmake -DCMAKE_BUILD_TYPE=Release
	cmake --build build/psp -j$(JOBS)

desktop:
	cmake -S engine -B build/desktop -DCMAKE_BUILD_TYPE=RelWithDebInfo
	cmake --build build/desktop -j$(JOBS)

builder: engine
	cmake -S tools/builder -B build/builder-native -DCMAKE_BUILD_TYPE=Release
	cmake --build build/builder-native -j$(JOBS)

test: builder
	ctest --test-dir build/builder-native --output-on-failure

run: builder
	$(BUILDER) $(if $(GAME),"$(GAME)" -o "$(OUT)") $(if $(VOICES),--voices "$(VOICES)")

play: desktop
	build/desktop/tcoaal --data "$(OUT)/PSP/GAME/TCOAAL/EBOOT.PBP" --saves "$(OUT)/saves"

release: engine zig
	ZIG=$(ZIG) ./scripts/build-builder.sh

web: engine zig
	ZIG=$(ZIG) VOICES=$(VOICES) ./scripts/build-web.sh

web-serve: web
	cd build/web && python3 -m http.server 8080 --bind 127.0.0.1

clean:
	rm -rf build
