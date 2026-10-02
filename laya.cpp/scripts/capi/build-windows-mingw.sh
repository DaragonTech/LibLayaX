#!/usr/bin/env bash
# Reproducible cross-build of laya.dll (Windows x64) on Ubuntu 24.04 / Debian: build-win-<variant>/bin/laya.dll
# with no runtime dependencies beyond Windows system DLLs (MinGW runtime, libstdc++, winpthread and
# ICU are linked statically).
#
#   scripts/capi/build-windows-mingw.sh                    # avx2 + compat
#   VARIANTS="avx2 compat vulkan" VULKAN_HEADERS=... SPIRV_HEADERS=... GLSLC=... VULKAN_DEF=... scripts/capi/build-windows-mingw.sh
#   RUN_TESTS=1 LAYA_TEST_MODEL=/path/to/checkpoint scripts/capi/build-windows-mingw.sh
#
# Needs: apt install mingw-w64 cmake ninja-build nlohmann-json3-dev curl  (+ wine64 for tests)
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${WORK:-$ROOT/build-deps}
ICU_VERSION=${ICU_VERSION:-76-1}
ICU_TGZ=icu4c-${ICU_VERSION/-/_}-src.tgz
JOBS=${JOBS:-$(nproc)}
mkdir -p "$WORK"
cd "$WORK"

# 1. Static ICU for MinGW (cross builds need a native ICU build for the data tools).
if [ ! -f icu-win/lib/libsicudt.a ]; then
  [ -f "$ICU_TGZ" ] || curl -fsSL -o "$ICU_TGZ" "https://github.com/unicode-org/icu/releases/download/release-$ICU_VERSION/$ICU_TGZ"
  rm -rf icu icu-host icu-win-build && tar xzf "$ICU_TGZ" && mkdir -p icu-host icu-win-build
  (cd icu-host && ../icu/source/runConfigureICU Linux --disable-tests --disable-samples --disable-extras >/dev/null && make -j"$JOBS" >/dev/null)
  (cd icu-win-build && CC=x86_64-w64-mingw32-gcc-posix CXX=x86_64-w64-mingw32-g++-posix CFLAGS=-O2 CXXFLAGS="-O2 -std=c++17" \
     ../icu/source/configure --host=x86_64-w64-mingw32 --with-cross-build="$WORK/icu-host" \
       --enable-static --disable-shared --with-data-packaging=static --disable-tests --disable-samples \
       --disable-extras --disable-tools --disable-dyload --prefix="$WORK/icu-win" >/dev/null &&
   make -j"$JOBS" >/dev/null && (make install >/dev/null 2>&1 || true))
  # ICU's installer puts the static data archive in bin/ for MinGW targets; place it by hand.
  mkdir -p icu-win/lib && cp icu-win-build/lib/sicudt.a icu-win/lib/libsicudt.a
fi

# 2. Header-only nlohmann-json, isolated so the cross compiler never sees /usr/include.
if [ ! -d nl/include/nlohmann ]; then
  mkdir -p nl/share/cmake nl/include
  cp -r /usr/share/cmake/nlohmann_json nl/share/cmake/ && cp -r /usr/include/nlohmann nl/include/
fi

# 3. One build per variant: build-win-<variant>/bin/laya.dll
#   avx2    cpu backend, AVX2/FMA/F16C/BMI2 baseline (Intel 2013+, AMD Zen+)
#   compat  cpu backend, SSE4.2 baseline (any x86-64 since ~2009; Windows on ARM x64 emulation)
#   vulkan  cpu (avx2) + vulkan backends. vulkan-1.dll is delay-loaded, so the DLL also loads on
#           PCs without Vulkan. Needs VULKAN_HEADERS (Vulkan-Headers/include), SPIRV_HEADERS
#           (SPIRV-Headers/include), GLSLC (shaderc >= 2025.3) and VULKAN_DEF (a .def listing the
#           exports of vulkan-1.dll, e.g. generated from Wine's or the SDK's vulkan-1.dll).
I=$WORK/icu-win
VERSION=${LAYA_C_VERSION:-1.0.14}
for variant in ${VARIANTS:-avx2 compat}; do
  cxxflags="-DU_STATIC_IMPLEMENTATION"
  case $variant in
    avx2)   isa=(-DGGML_AVX=ON -DGGML_AVX2=ON -DGGML_FMA=ON -DGGML_F16C=ON -DGGML_BMI2=ON) ;;
    compat) isa=(-DGGML_SSE42=ON -DGGML_AVX=OFF -DGGML_AVX2=OFF -DGGML_FMA=OFF -DGGML_F16C=OFF -DGGML_BMI2=OFF) ;;
    vulkan) mkdir -p "$WORK/vk-mingw"
            x86_64-w64-mingw32-dlltool -d "${VULKAN_DEF:?set VULKAN_DEF}" -y "$WORK/vk-mingw/libvulkan-1-delay.a" -D vulkan-1.dll
            cxxflags="$cxxflags -I${SPIRV_HEADERS:?set SPIRV_HEADERS}"
            isa=(-DLAYA_VULKAN=ON -DVulkan_INCLUDE_DIR="${VULKAN_HEADERS:?set VULKAN_HEADERS}"
                 -DVulkan_LIBRARY="$WORK/vk-mingw/libvulkan-1-delay.a" -DVulkan_GLSLC_EXECUTABLE="${GLSLC:-glslc}"
                 -DGGML_AVX=ON -DGGML_AVX2=ON -DGGML_FMA=ON -DGGML_F16C=ON -DGGML_BMI2=ON) ;;
    *) echo "unknown variant $variant" >&2; exit 1 ;;
  esac
  B=$ROOT/build-win-$variant
  cmake -S "$ROOT" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchains/mingw-w64-x86_64.cmake" \
    -DLAYA_CUDA=OFF -DLAYA_C_API=ON -DBUILD_SHARED_LIBS=OFF -DLAYA_C_VERSION="$VERSION" \
    -DGGML_NATIVE=OFF -DGGML_OPENMP=OFF -DGGML_CCACHE=OFF "${isa[@]}" \
    -DICU_ROOT="$I" -DICU_INCLUDE_DIR="$I/include" -DICU_UC_LIBRARY_RELEASE="$I/lib/libsicuuc.a" \
    -DICU_I18N_LIBRARY_RELEASE="$I/lib/libsicuin.a" -DICU_DATA_LIBRARY_RELEASE="$I/lib/libsicudt.a" \
    "-DCMAKE_CXX_FLAGS=$cxxflags" -Dnlohmann_json_DIR="$WORK/nl/share/cmake/nlohmann_json"
  cmake --build "$B" --parallel "$JOBS" --target laya_c test-capi laya-bench laya-diag laya-probe laya-cli
  x86_64-w64-mingw32-strip --strip-unneeded "$B/bin/laya.dll"
  # Import library for Visual C++ users (MinGW can link the DLL directly). llvm-dlltool writes
  # the short import format link.exe expects; GNU dlltool's output also works with it.
  "$(command -v llvm-dlltool || command -v llvm-dlltool-18 || echo x86_64-w64-mingw32-dlltool)" \
    -m i386:x86-64 -d "$ROOT/capi/laya.def" -D laya.dll -l "$B/bin/laya.lib"
  echo "== $variant: $B/bin/laya.dll"
  x86_64-w64-mingw32-objdump -p "$B/bin/laya.dll" | grep "DLL Name"

  # Optional: run the C test suite under Wine (cpu backend).
  if [ "${RUN_TESTS:-0}" = 1 ]; then
    model=""
    [ -n "${LAYA_TEST_MODEL:-}" ] && model="Z:${LAYA_TEST_MODEL//\//\\}"
    (cd "$B/bin" && WINEDEBUG=-all LAYA_TEST_MODEL="$model" wine test-capi.exe) || [ $? = 77 ]
  fi
done
