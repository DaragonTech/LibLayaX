#!/usr/bin/env bash
# Cross-build the ARM64 libraries from Linux:
#   build-linux-arm64/bin/liblaya.so   Linux aarch64, glibc >= 2.27, ARMv8.0 + NEON baseline.
#                                      Built with Zig (pip install ziglang).
#   build-win-arm64/bin/laya.dll       native Windows ARM64 (for ARM64 processes; an x64 process
#                                      running under emulation needs the x64 compat DLL instead).
#                                      Built with llvm-mingw (https://github.com/mstorsjo/llvm-mingw;
#                                      set LLVM_MINGW to the unpacked directory). Do not build this
#                                      one with Zig 0.16: the result hangs on real Windows on ARM
#                                      the first time the library throws a C++ exception.
# CPU backend. libc++ and ICU are linked in.
#
#   scripts/capi/build-arm64.sh [linux] [windows]
#   RUN_TESTS=1 LAYA_TEST_MODEL=/path/to/checkpoint scripts/capi/build-arm64.sh linux
#     (runs test-capi through qemu-aarch64-static; needs qemu-user-static libc6-arm64-cross; slow)
#
# Needs: cmake ninja-build nlohmann-json3-dev curl, a host C++ compiler (for ICU's data tools).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${WORK:-$ROOT/build-deps}
JOBS=${JOBS:-$(nproc)}
VERSION=${LAYA_C_VERSION:-1.0.14}
ICU_VERSION=${ICU_VERSION:-76-1}
ICU_TGZ=icu4c-${ICU_VERSION/-/_}-src.tgz
W=$ROOT/cmake/toolchains/zig
TARGETS=${*:-linux windows}
mkdir -p "$WORK" && cd "$WORK"

[ -f "$ICU_TGZ" ] || [ -d icu ] || curl -fsSL -o "$ICU_TGZ" "https://github.com/unicode-org/icu/releases/download/release-$ICU_VERSION/$ICU_TGZ"
[ -d icu ] || tar xzf "$ICU_TGZ"
if [ ! -d icu-host/lib ]; then  # native ICU build: cross builds reuse its data tools
  mkdir -p icu-host && (cd icu-host && ../icu/source/runConfigureICU Linux --disable-tests --disable-samples --disable-extras >/dev/null && make -j"$JOBS" >/dev/null)
fi
if [ ! -d nl/include/nlohmann ]; then
  mkdir -p nl/share/cmake nl/include
  cp -r /usr/share/cmake/nlohmann_json nl/share/cmake/ && cp -r /usr/include/nlohmann nl/include/
fi

for target in $TARGETS; do
  case $target in
    linux)   host=aarch64-linux-gnu;   prefix=$WORK/icu-linux-arm64; flags="-O2 -fPIC"
             cc=$W/zigcc-aarch64-linux; cxx=$W/zigcxx-aarch64-linux; ar=$W/zigar; ranlib=$W/zigranlib
             B=$ROOT/build-linux-arm64; toolchain=zig-linux-aarch64.cmake; tools="laya-trace"
             uc=libicuuc.a; in=libicui18n.a; dt=libicudata.a ;;
    windows) host=aarch64-w64-mingw32; prefix=$WORK/icu-win-arm64-llvm; flags="-O2"
             M=${LLVM_MINGW:?set LLVM_MINGW to the llvm-mingw directory}/bin; export LLVM_MINGW
             cc=$M/aarch64-w64-mingw32-clang; cxx=$M/aarch64-w64-mingw32-clang++
             ar=$M/aarch64-w64-mingw32-ar; ranlib=$M/aarch64-w64-mingw32-ranlib
             B=$ROOT/build-win-arm64;   toolchain=llvm-mingw-aarch64.cmake; tools=""
             uc=libsicuuc.a; in=libsicuin.a; dt=libsicudt.a ;;
    *) echo "unknown target $target" >&2; exit 1 ;;
  esac
  if [ ! -f "$prefix/lib/$uc" ]; then
    rm -rf "$prefix-build" && mkdir "$prefix-build"
    (cd "$prefix-build" && CC=$cc CXX=$cxx AR=$ar RANLIB=$ranlib CFLAGS="$flags" CXXFLAGS="$flags -std=c++17" \
       ../icu/source/configure --host=$host --with-cross-build="$WORK/icu-host" --enable-static --disable-shared \
         --with-data-packaging=static --disable-tests --disable-samples --disable-extras --disable-tools --disable-dyload \
         --prefix="$prefix" >/dev/null && make -j"$JOBS" >/dev/null && (make install >/dev/null 2>&1 || true))
    # ICU installs only a stub data archive for MinGW targets; the real one stays in the build tree.
    if [ $target = windows ]; then cp "$prefix-build/lib/sicudt.a" "$prefix/lib/$dt"
    elif [ ! -f "$prefix/lib/$dt" ]; then cp "$prefix-build/lib/$dt" "$prefix/lib/"; fi
  fi
  cmake -S "$ROOT" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchains/$toolchain" \
    -DLAYA_CUDA=OFF -DLAYA_C_API=ON -DBUILD_SHARED_LIBS=OFF -DLAYA_C_VERSION="$VERSION" \
    -DGGML_NATIVE=OFF -DGGML_OPENMP=OFF -DGGML_CCACHE=OFF \
    -DICU_ROOT="$prefix" -DICU_INCLUDE_DIR="$prefix/include" -DICU_UC_LIBRARY_RELEASE="$prefix/lib/$uc" \
    -DICU_I18N_LIBRARY_RELEASE="$prefix/lib/$in" -DICU_DATA_LIBRARY_RELEASE="$prefix/lib/$dt" \
    -DCMAKE_CXX_FLAGS=-DU_STATIC_IMPLEMENTATION -Dnlohmann_json_DIR="$WORK/nl/share/cmake/nlohmann_json"
  cmake --build "$B" --parallel "$JOBS" --target laya_c test-capi laya-cli laya-bench laya-diag laya-probe $tools
  if [ $target = windows ]; then  # import library for Visual C++ users
    "$M/llvm-dlltool" -m arm64 -d "$ROOT/capi/laya.def" -D laya.dll -l "$B/bin/laya.lib"
  fi
  echo "== $target arm64: $B/bin"
  if [ $target = linux ] && [ "${RUN_TESTS:-0}" = 1 ]; then
    (cd "$B/bin" && LAYA_TEST_QUICK=1 qemu-aarch64-static -L /usr/aarch64-linux-gnu -E LD_LIBRARY_PATH=. ./test-capi) || [ $? = 77 ]
  fi
done
