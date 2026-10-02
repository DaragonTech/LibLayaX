#!/usr/bin/env bash
# Build portable liblaya.so variants (Linux x86-64, CPU backend):
#   build-linux-avx2/bin/liblaya.so    AVX2/FMA/F16C/BMI2 baseline (fast, ~2013+ CPUs)
#   build-linux-compat/bin/liblaya.so  SSE4.2 baseline (any x86-64 since ~2009, and emulators)
#   build-linux-vulkan/bin/liblaya.so  GPU via Vulkan (VARIANTS=vulkan; also links libvulkan.so.1)
# ICU, libstdc++ and libgcc are linked in statically, so the library needs only glibc
# (libc, libm, libpthread) from the system and works across distributions of the same or newer
# glibc than the build machine.
#
#   scripts/capi/build-linux.sh            # build both
#   VARIANTS=compat scripts/capi/build-linux.sh
#   VARIANTS=vulkan VULKAN_HEADERS=/path/to/Vulkan-Headers/include \
#     EXTRA_CMAKE_ARGS="-DVulkan_GLSLC_EXECUTABLE=/path/to/glslc -DVulkan_INCLUDE_DIR=..." ...
#     (Ubuntu 24.04's glslc 2023.8 crashes on laya's shaders; use shaderc >= 2025.3 / Vulkan SDK 1.4)
#
# Needs: cmake ninja-build g++ nlohmann-json3-dev curl
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${WORK:-$ROOT/build-deps}
ICU_VERSION=${ICU_VERSION:-76-1}
ICU_TGZ=icu4c-${ICU_VERSION/-/_}-src.tgz
JOBS=${JOBS:-$(nproc)}
VARIANTS=${VARIANTS:-"avx2 compat"}
VERSION=${LAYA_C_VERSION:-1.0.14}
mkdir -p "$WORK" && cd "$WORK"

# 1. Static, position-independent ICU (the system one is a shared, version-specific library).
if [ ! -f icu-linux-static/lib/libicuuc.a ]; then
  [ -f "$ICU_TGZ" ] || curl -fsSL -o "$ICU_TGZ" "https://github.com/unicode-org/icu/releases/download/release-$ICU_VERSION/$ICU_TGZ"
  [ -d icu ] || tar xzf "$ICU_TGZ"
  rm -rf icu-linux-static-build && mkdir icu-linux-static-build
  (cd icu-linux-static-build && CFLAGS="-O2 -fPIC" CXXFLAGS="-O2 -fPIC -std=c++17" \
     ../icu/source/runConfigureICU Linux --enable-static --disable-shared --with-data-packaging=static \
       --disable-tests --disable-samples --disable-extras --disable-tools --disable-dyload \
       --prefix="$WORK/icu-linux-static" >/dev/null && make -j"$JOBS" >/dev/null && (make install >/dev/null 2>&1 || true))
  # Like the MinGW cross build, the static data archive may not be installed; place it by hand.
  [ -f icu-linux-static/lib/libicudata.a ] || cp icu-linux-static-build/lib/libsicudt.a icu-linux-static/lib/libicudata.a 2>/dev/null \
    || cp icu-linux-static-build/lib/libicudata.a icu-linux-static/lib/libicudata.a
fi
I=$WORK/icu-linux-static

# 2. One build per CPU baseline.
for variant in $VARIANTS; do
  case $variant in
    avx2)   isa=(-DGGML_AVX=ON -DGGML_AVX2=ON -DGGML_FMA=ON -DGGML_F16C=ON -DGGML_BMI2=ON) ;;
    compat) isa=(-DGGML_SSE42=ON -DGGML_AVX=OFF -DGGML_AVX2=OFF -DGGML_FMA=OFF -DGGML_F16C=OFF -DGGML_BMI2=OFF) ;;
    # GPU through Vulkan (needs libvulkan-dev glslc spirv-headers to build, a Vulkan driver to run);
    # its CPU fallback backend uses the avx2 baseline.
    vulkan) # libvulkan.so.1 is opened on first use through a small shim, not linked: the library
            # then loads (and the cpu backend works) on machines without Vulkan.
            mkdir -p "$WORK/vulkan-lazy"
            ${CC:-cc} -O2 -fPIC -I"${VULKAN_HEADERS:-/usr/include}" -c "$ROOT/capi/vulkan_lazy.c" -o "$WORK/vulkan-lazy/vulkan_lazy.o"
            rm -f "$WORK/vulkan-lazy/libvulkan_lazy.a" && ar rcs "$WORK/vulkan-lazy/libvulkan_lazy.a" "$WORK/vulkan-lazy/vulkan_lazy.o"
            isa=(-DLAYA_VULKAN=ON -DLAYA_VULKAN_LAZY=ON -DVulkan_LIBRARY="$WORK/vulkan-lazy/libvulkan_lazy.a"
                 -DGGML_AVX=ON -DGGML_AVX2=ON -DGGML_FMA=ON -DGGML_F16C=ON -DGGML_BMI2=ON) ;;
    *) echo "unknown variant $variant" >&2; exit 1 ;;
  esac
  cmake -S "$ROOT" -B "$ROOT/build-linux-$variant" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DLAYA_CUDA=OFF -DLAYA_C_API=ON -DBUILD_SHARED_LIBS=OFF -DLAYA_C_VERSION="$VERSION" \
    -DGGML_NATIVE=OFF -DGGML_OPENMP=OFF -DGGML_CCACHE=OFF "${isa[@]}" \
    -DICU_ROOT="$I" -DICU_INCLUDE_DIR="$I/include" -DICU_UC_LIBRARY_RELEASE="$I/lib/libicuuc.a" \
    -DICU_I18N_LIBRARY_RELEASE="$I/lib/libicui18n.a" -DICU_DATA_LIBRARY_RELEASE="$I/lib/libicudata.a" \
    -DCMAKE_CXX_FLAGS="-DU_STATIC_IMPLEMENTATION" \
    -DCMAKE_SHARED_LINKER_FLAGS="-static-libstdc++ -static-libgcc" \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc" ${EXTRA_CMAKE_ARGS:-}
  cmake --build "$ROOT/build-linux-$variant" --parallel "$JOBS" --target laya_c test-capi laya-cli laya-bench laya-diag laya-trace laya-probe
  strip --strip-unneeded "$ROOT/build-linux-$variant/bin/liblaya.so"
  echo "== $variant: $ROOT/build-linux-$variant/bin/liblaya.so"
  readelf -d "$ROOT/build-linux-$variant/bin/liblaya.so" | grep NEEDED
done
