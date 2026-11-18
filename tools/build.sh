#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JOBS="${JOBS:-4}"

cmake -S "$ROOT" -B "$ROOT/build/native" -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT/build/native" --parallel "$JOBS"
mkdir -p "$ROOT/bin"
cp "$ROOT/build/native/taohash_pool" "$ROOT/bin/taohash_pool"
cp "$ROOT/build/native/whitepaper_incentive" "$ROOT/bin/whitepaper_incentive"

if [[ "${WITH_ASCEND:-0}" == 1 ]]; then
  CANN_HOME="${CANN_HOME:-/usr/local/Ascend/cann}"
  if [[ ! -f "$CANN_HOME/set_env.sh" ]]; then
    echo "Ascend CANN not found: $CANN_HOME/set_env.sh" >&2
    exit 1
  fi
  # Source toolkit environment after validating the selected installation.
  # shellcheck disable=SC1090
  source "$CANN_HOME/set_env.sh"

  ASC_MODULES=""
  for candidate in \
    "$CANN_HOME/compiler/tikcpp/ascendc_kernel_cmake/asc_modules" \
    "$CANN_HOME/compiler/tikcpp/ascendc_kernel_cmake" \
    "$CANN_HOME/aarch64-linux/tikcpp/ascendc_kernel_cmake/asc_modules" \
    "$CANN_HOME/x86_64-linux/tikcpp/ascendc_kernel_cmake/asc_modules"; do
    if [[ -f "$candidate/FindASC.cmake" ]]; then
      ASC_MODULES="$candidate"
      break
    fi
    if [[ -f "$candidate/asc_modules/FindASC.cmake" ]]; then
      ASC_MODULES="$candidate/asc_modules"
      break
    fi
  done
  if [[ -z "$ASC_MODULES" ]]; then
    # Toolkit layouts vary by CANN release; locate the module only if the
    # well-known paths were not present.
    asc_module_file="$(find "$CANN_HOME" -name FindASC.cmake -print -quit 2>/dev/null)"
    if [[ -n "$asc_module_file" ]]; then
      ASC_MODULES="$(dirname "$asc_module_file")"
    else
      echo "Ascend FindASC.cmake not found below $CANN_HOME" >&2
      exit 1
    fi
  fi

  cmake -S "$ROOT/ascend" -B "$ROOT/build/ascend" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_ASC_RUN_MODE=npu \
    -DCMAKE_ASC_ARCHITECTURES="${TAO_ASC_ARCH:-dav-2201}" \
    -DCMAKE_PREFIX_PATH="$ASC_MODULES" \
    -DCMAKE_MODULE_PATH="$ASC_MODULES"
  cmake --build "$ROOT/build/ascend" --parallel "$JOBS"
  cp "$ROOT/build/ascend/libtaohash_npu.so" "$ROOT/bin/libtaohash_npu.so"
fi

echo "Built $ROOT/bin/taohash_pool"
echo "Built $ROOT/bin/whitepaper_incentive (reference only)"
if [[ "${WITH_ASCEND:-0}" == 1 ]]; then
  echo "Built $ROOT/bin/libtaohash_npu.so"
fi
