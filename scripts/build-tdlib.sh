#!/usr/bin/env bash
# Builds TDLib (Telegram's official client library) at the pinned commit and
# installs it into a prefix. Needs git, cmake, a C++17 compiler, gperf and the
# OpenSSL and zlib development files; takes 20-60 minutes and a few GB of RAM.
#   scripts/build-tdlib.sh <install-prefix> [jobs]   (CMake runs it for third_party/tdlib)
# Normally not called by hand: configuring the plugin builds TDLib when needed.
set -euo pipefail
prefix="$(mkdir -p "$1" && cd "$1" && pwd)"
jobs="${2:-$(nproc)}"
root="$(cd "$(dirname "$0")/.." && pwd)"
commit="$(tr -d '[:space:]' < "$root/TDLIB_COMMIT")"

if [[ -f "$prefix/.tdlib-commit" && "$(cat "$prefix/.tdlib-commit")" == "$commit" ]]; then
    echo "TDLib $commit already installed in $prefix"
    exit 0
fi

src="$(mktemp -d)"
trap 'rm -rf "$src"' EXIT
git -C "$src" init -q
git -C "$src" remote add origin https://github.com/tdlib/td.git
git -C "$src" fetch -q --depth 1 origin "$commit"
git -C "$src" checkout -q FETCH_HEAD

generator=()
command -v ninja >/dev/null && generator=(-G Ninja)
# Static OpenSSL and zlib: TDLib records the libraries it was built with in
# its CMake package, and the plugin is linked fully static.
static=()
for lib in ssl crypto z; do
    a="$(${CC:-gcc} -print-file-name="lib$lib.a")"
    [[ -f "$a" ]] || { echo "lib$lib.a not found (install the OpenSSL and zlib dev packages)" >&2; exit 1; }
    case $lib in
        ssl) static+=(-DOPENSSL_SSL_LIBRARY="$a") ;;
        crypto) static+=(-DOPENSSL_CRYPTO_LIBRARY="$a") ;;
        z) static+=(-DZLIB_LIBRARY="$a" -DZLIB_LIBRARY_RELEASE="$a") ;;
    esac
done
cmake -S "$src" -B "$src/build" "${generator[@]}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DTD_ENABLE_LTO=OFF -DTD_ENABLE_JNI=OFF -DTD_ENABLE_DOTNET=OFF \
    -DOPENSSL_USE_STATIC_LIBS=ON -DZLIB_USE_STATIC_LIBS=ON "${static[@]}"
cmake --build "$src/build" --target install -j "$jobs"
echo "$commit" > "$prefix/.tdlib-commit"
echo "TDLib $commit installed in $prefix"
