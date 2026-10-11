#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
zig="${ZIG:-$root/_deps/zig/zig}"
engine="${ENGINE_PBP:-$root/build/psp/EBOOT.PBP}"
src="$root/tools/builder"
out="$root/build/builder"
gen="$out/gen"
mkdir -p "$gen"
[ -f "$engine" ] || { echo "engine not found: $engine (run scripts/build.sh first)"; exit 1; }
cc -O2 -o "$gen/embed" "$src/tools/embed.c"
"$gen/embed" "$engine" "$gen/engine_blob.c" embedded_engine
"$gen/embed" "$src/art/psp.png" "$gen/art_blob.c" embedded_psp_art
sources=("$src"/src/*.c "$src/third_party/miniz.c" "$gen/engine_blob.c" "$gen/art_blob.c")
targets=("${@:-x86_64-linux-musl aarch64-linux-musl x86_64-windows-gnu x86_64-macos aarch64-macos}")
for t in ${targets[@]}; do
  name="tcoaal-builder-$t"
  libs="-lm"
  case "$t" in
    *windows*) name="$name.exe"; libs="-lole32 -lshell32 -luser32 -lgdi32 -ladvapi32 -luuid" ;;
    *linux*) libs="-lm -lpthread -static" ;;
  esac
  "$zig" cc -target "$t" -std=c99 -O2 -D_DEFAULT_SOURCE -D_FILE_OFFSET_BITS=64 -DMINIZ_NO_STDIO -I"$src/src" -I"$src/third_party" \
    -s "${sources[@]}" -o "$out/$name" $libs
  rm -f "$out/${name%.exe}.pdb"
  echo "built $out/$name"
done
