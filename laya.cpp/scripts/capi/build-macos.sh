#!/usr/bin/env bash
# Cross-build liblaya.dylib for macOS 11+ from Linux with Zig (no Apple SDK needed):
#   build-mac-arm64/bin/liblaya.dylib          Apple Silicon (M1 baseline: NEON, fp16, dotprod)
#   build-mac-x86_64-avx2/bin/liblaya.dylib    Intel Macs with AVX2 (2013+ except Mac Pro 2013)
#   build-mac-x86_64-compat/bin/liblaya.dylib  any Intel Mac that runs macOS 11 (SSE4.2), and Rosetta
# CPU backend only: Metal/Accelerate/CoreML need Apple frameworks, which a cross build lacks.
#
#   pip install ziglang    (or put zig >= 0.13 on PATH; set ZIG=/path/to/zig)
#   apt install lld        (ld64.lld, for the final link with an exact export list)
#   scripts/capi/build-macos.sh [arm64] [x86_64-avx2] [x86_64-compat]
#
# On a real Mac you would instead use Xcode's clang: cmake -DLAYA_C_API=ON ... (see capi/README.md).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${WORK:-$ROOT/build-deps}
JOBS=${JOBS:-$(nproc)}
VERSION=${LAYA_C_VERSION:-1.0.14}
ICU_VERSION=${ICU_VERSION:-76-1}
ICU_TGZ=icu4c-${ICU_VERSION/-/_}-src.tgz
if [ -z "${ZIG:-}" ]; then
  ZIG=$(command -v zig || python3 -c "import ziglang,os;print(os.path.join(os.path.dirname(ziglang.__file__),'zig'))")
fi
export ZIG
LD64=${LD64_LLD:-$(command -v ld64.lld || command -v ld64.lld-18 || true)}
W=$ROOT/cmake/toolchains/zig
VARIANTS=${*:-arm64 x86_64-avx2 x86_64-compat}
mkdir -p "$WORK" && cd "$WORK"

# 1. Static ICU per architecture (cross builds reuse a native ICU build for the data tools).
icu_for() {  # $1 = aarch64|x86_64
  local a=$1 p=$WORK/icu-mac-$1
  [ -f "$p/lib/libicuuc.a" ] && return
  [ -f "$ICU_TGZ" ] || curl -fsSL -o "$ICU_TGZ" "https://github.com/unicode-org/icu/releases/download/release-$ICU_VERSION/$ICU_TGZ"
  [ -d icu ] || tar xzf "$ICU_TGZ"
  if [ ! -d icu-host/lib ]; then
    mkdir -p icu-host && (cd icu-host && ../icu/source/runConfigureICU Linux --disable-tests --disable-samples --disable-extras >/dev/null && make -j"$JOBS" >/dev/null)
  fi
  rm -rf "icu-mac-$a-build" && mkdir "icu-mac-$a-build"
  (cd "icu-mac-$a-build" && ZIG_TARGET=$a-macos.11.0 CC=$W/zigcc CXX=$W/zigcxx AR=$W/zigar RANLIB=$W/zigranlib \
     CFLAGS="-O2 -I$W/macos-stubs" CXXFLAGS="-O2 -std=c++17 -I$W/macos-stubs" \
     ../icu/source/configure --host=$a-apple-darwin --with-cross-build="$WORK/icu-host" \
       --enable-static --disable-shared --with-data-packaging=static --disable-tests --disable-samples \
       --disable-extras --disable-tools --disable-dyload --prefix="$p" >/dev/null &&
   ZIG_TARGET=$a-macos.11.0 make -j"$JOBS" >/dev/null && (ZIG_TARGET=$a-macos.11.0 make install >/dev/null 2>&1 || true))
  [ -f "$p/lib/libicudata.a" ] || cp "icu-mac-$a-build/lib/libicudata.a" "$p/lib/"
}

# Header-only nlohmann-json, isolated from the build machine's /usr/include.
if [ ! -d nl/include/nlohmann ]; then
  mkdir -p nl/share/cmake nl/include
  cp -r /usr/share/cmake/nlohmann_json nl/share/cmake/ && cp -r /usr/include/nlohmann nl/include/
fi

for variant in $VARIANTS; do
  case $variant in
    arm64)         arch=arm64;  za=aarch64; isa=(-DCMAKE_C_FLAGS=-mcpu=apple_m1 "-DCMAKE_CXX_FLAGS=-mcpu=apple_m1 -DU_STATIC_IMPLEMENTATION") ;;
    x86_64-avx2)   arch=x86_64; za=x86_64;  isa=(-DGGML_AVX=ON -DGGML_AVX2=ON -DGGML_FMA=ON -DGGML_F16C=ON -DGGML_BMI2=ON "-DCMAKE_CXX_FLAGS=-DU_STATIC_IMPLEMENTATION") ;;
    x86_64-compat) arch=x86_64; za=x86_64;  isa=(-DGGML_SSE42=ON -DGGML_AVX=OFF -DGGML_AVX2=OFF -DGGML_FMA=OFF -DGGML_F16C=OFF -DGGML_BMI2=OFF "-DCMAKE_CXX_FLAGS=-DU_STATIC_IMPLEMENTATION") ;;
    # GPU via Vulkan on Metal (MoltenVK, weak-linked: without libMoltenVK.dylib next to liblaya.dylib
    # the cpu backend still works). Needs MOLTENVK=/path/to/libMoltenVK.dylib (official release),
    # VULKAN_HEADERS=/path/to/Vulkan-Headers/include, GLSLC=glslc (shaderc >= 2025.3),
    # SPIRV_HEADERS=/path/to/SPIRV-Headers/include.
    arm64-vulkan)  arch=arm64;  za=aarch64; isa=(-DCMAKE_C_FLAGS=-mcpu=apple_m1
                     "-DCMAKE_CXX_FLAGS=-mcpu=apple_m1 -DU_STATIC_IMPLEMENTATION -I${SPIRV_HEADERS:?set SPIRV_HEADERS}"
                     -DLAYA_VULKAN=ON -DVulkan_INCLUDE_DIR="${VULKAN_HEADERS:?set VULKAN_HEADERS}"
                     -DVulkan_LIBRARY="${MOLTENVK:?set MOLTENVK}" -DVulkan_GLSLC_EXECUTABLE="${GLSLC:-glslc}") ;;
    *) echo "unknown variant $variant" >&2; exit 1 ;;
  esac
  icu_for $za
  I=$WORK/icu-mac-$za
  B=$ROOT/build-mac-$variant
  cmake -S "$ROOT" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchains/zig-macos.cmake" -DLAYA_MACOS_ARCH=$arch \
    -DLAYA_CUDA=OFF -DLAYA_C_API=ON -DBUILD_SHARED_LIBS=OFF -DLAYA_C_VERSION="$VERSION" \
    -DGGML_NATIVE=OFF -DGGML_METAL=OFF -DGGML_BLAS=OFF -DGGML_ACCELERATE=OFF -DGGML_OPENMP=OFF -DGGML_CCACHE=OFF \
    "${isa[@]}" \
    -DICU_ROOT="$I" -DICU_INCLUDE_DIR="$I/include" -DICU_UC_LIBRARY_RELEASE="$I/lib/libicuuc.a" \
    -DICU_I18N_LIBRARY_RELEASE="$I/lib/libicui18n.a" -DICU_DATA_LIBRARY_RELEASE="$I/lib/libicudata.a" \
    -Dnlohmann_json_DIR="$WORK/nl/share/cmake/nlohmann_json"
  cmake --build "$B" --parallel "$JOBS" --target laya_c test-capi laya-bench laya-diag laya-trace laya-probe

  # zig's Mach-O linker ignores -exported_symbols_list, so relink the dylib with LLVM's ld64.lld
  # against the same libc++/compiler-rt that zig uses (found by letting zig link a probe).
  if [ -n "$LD64" ]; then
    probe=$B/zig-probe; mkdir -p "$probe"
    printf '#include <string>\nextern "C" int f(){ return (int)std::string("x").size(); }\n' > "$probe/p.cpp"
    rt=$(cd "$probe" && ZIG_VERBOSE_LINK=1 "$W/zigcxx-$za-macos" -O3 -dynamiclib -o p.dylib p.cpp 2>&1 |
         tr ' ' '\n' | grep -E '/lib(c\+\+abi|c\+\+|compiler_rt)\.a$' | tr '\n' ' ')
    libdir=$("$ZIG" env | sed -n 's/.*\.lib_dir = "\(.*\)".*/\1/p')
    ldarch=$([ $za = aarch64 ] && echo arm64 || echo x86_64)
    inputs=$(sed -n 's/^  LINK_LIBRARIES = //p' <(sed -n '/^build bin\/liblaya.dylib/,/^$/p' "$B/build.ninja") | sed 's/ -lm//')
    extra=""
    if [ -n "${MOLTENVK:-}" ] && [[ $variant == *vulkan* ]]; then
      # Weak link: dyld then loads liblaya.dylib even without MoltenVK (the cpu backend keeps working);
      # @loader_path finds libMoltenVK.dylib when it is shipped next to liblaya.dylib.
      inputs=$(printf '%s\n' $inputs | grep -v -F "$MOLTENVK" | tr '\n' ' ')
      extra="-weak_library $MOLTENVK -rpath @loader_path"
    fi
    (cd "$B" && "$LD64" -arch $ldarch -platform_version macos 11.0 11.0 -dylib -o bin/liblaya.dylib $extra \
       -install_name @rpath/liblaya.dylib -compatibility_version 1.0.0 -current_version 1.0.0 \
       -exported_symbols_list "$ROOT/capi/macos-exports.txt" -dead_strip -x -adhoc_codesign \
       capi/CMakeFiles/laya_c.dir/laya_c.cpp.o $inputs $rt -L"$libdir/libc/darwin" -lSystem)
    # Relink the test program the same way, so it is ad-hoc signed on Intel too (zig signs arm64 only).
    crt=$(printf '%s\n' $rt | grep compiler_rt || true)
    (cd "$B" && "$LD64" -arch $ldarch -platform_version macos 11.0 11.0 -execute -o bin/test-capi \
       -rpath @executable_path -dead_strip -adhoc_codesign \
       capi/CMakeFiles/test-capi.dir/__/tests/capi/test_capi.c.o bin/liblaya.dylib $crt -L"$libdir/libc/darwin" -lSystem)
    for tool in laya-bench:bench_capi laya-diag:diag_capi laya-trace:trace_capi; do
      (cd "$B" && "$LD64" -arch $ldarch -platform_version macos 11.0 11.0 -execute -o bin/${tool%%:*} \
         -rpath @executable_path -dead_strip -adhoc_codesign \
         capi/CMakeFiles/${tool%%:*}.dir/__/tests/capi/${tool#*:}.c.o bin/liblaya.dylib $crt -L"$libdir/libc/darwin" -lSystem)
    done
  else
    echo "warning: ld64.lld not found; $variant dylib exports internal symbols too" >&2
  fi
  echo "== $variant: $B/bin/liblaya.dylib"
done
