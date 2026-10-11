#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
zig="${ZIG:-$root/_deps/zig/zig}"
engine="${ENGINE_PBP:-$root/build/psp/EBOOT.PBP}"
voices="${VOICES:-}"
src="$root/tools/builder"
out="$root/build/web"
gen="$root/build/web-gen"
[ -f "$engine" ] || { echo "engine not found: $engine (run make engine first)"; exit 1; }
mkdir -p "$out" "$gen"
find "$out" -mindepth 1 -delete
cc -O2 -o "$gen/embed" "$src/tools/embed.c"
"$gen/embed" "$engine" "$gen/engine_blob.c" embedded_engine
"$gen/embed" "$src/art/psp.png" "$gen/art_blob.c" embedded_psp_art
"$zig" cc -target wasm32-wasi -std=c99 -O2 -D_DEFAULT_SOURCE -DMINIZ_NO_STDIO -D_WASI_EMULATED_PROCESS_CLOCKS \
  -I"$src/src" -I"$src/third_party" "$src"/src/*.c "$src/third_party/miniz.c" "$gen/engine_blob.c" "$gen/art_blob.c" \
  -lwasi-emulated-process-clocks -Wl,--max-memory=0x80000000 -Wl,-z,stack-size=8388608 -s -o "$out/tcoaal-builder.wasm"
cp -r "$root"/web/. "$out/"
rm -f "$out/voices.zip"
if [ -n "$voices" ] && [ -d "$voices" ] && [ -n "$(find "$voices" -name '*.ogg' -print -quit)" ]; then
  (cd "$voices" && cmake -E tar cf "$out/voices.zip" --format=zip -- $(find . -type f \( -name '*.ogg' -o -name 'credits.txt' \) | sed 's|^\./||' | sort))
fi
ls -la "$out"
