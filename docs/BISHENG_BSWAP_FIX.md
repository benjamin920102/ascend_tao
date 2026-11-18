# Bisheng 15.0.5 / CANN 9.0.0 / DAV-2201: bswap backend failure

## Symptom

```
fatal error: error in backend: Cannot select: ... i64 = bswap ...
In function: _Z14th_scan_kernelPhS_Pjjjj
bisheng: error: clang frontend command failed with exit code 70
```

The device-side SHA256d implementation previously converted SHA words to/from
byte arrays in a hot loop, plus loaded and reassembled byte-array header data.
The compiler's AIV instruction selector could not lower an optimized 64-bit
byte swap. This is a Bisheng backend code-generation error, not a json-c
runtime or linker problem.

## Source-level changes

- `include/sha_core.h`: 32-bit SHA compression on SHA-standard big-endian
  message words, while keeping the original byte-oriented CPU interface.
- `include/sha256d_aiv_words.h`: device-side SHA256d entirely with 32-bit
  state/message words. Nonce endian transform uses volatile 8-bit loads to
  avoid LLVM's wide bswap combine; compare the high-significance digest bytes
  against target bytes directly, without materializing digest bytes.
- `ascend/sha256d.asc`: the worker reads 32-bit host-prepared midstate, tail
  and target words. Result words remain packed until the host obtains a hit.
- `src/sha256d.cpp`: prepare device words in native C++ and reconstruct a hit's
  digest as SHA-standard big-endian digest bytes before host verification.

No AIC/Cube execution, AIC-AIV flag communication, Python or per-nonce GM
round-trip was added.

## Validation scope

Native CPU project builds using CMake. A disposable local standalone checker
compared **20,000 SHA256d outputs and 256-bit target decisions** (including
all-zeros, all-ones, random and exact-digest targets) against OpenSSL and the
original byte path; all passed. Local Clang 17 `-O3` LLVM IR contained zero
`llvm.bswap.i64` / `llvm.bswap.i32` occurrences in the checked code. The
checker is not included in the source archive.

**Not verified:** CANN/Bisheng 15.0.5 device compile, physical 910B/910C
runtime, kernel throughput, Stratum pool acceptance. The GitHub runner can
compile the ASC kernel but normally does not have an Ascend accelerator.

## Rebuild in GitHub Actions

Push the source files from this archive to your repository, then open
**Actions > Ascend TAOHash Pool Miner > Run workflow**.
If you still get `Cannot select`, capture the first 30 lines around the new
DAG node, plus the full Bisheng command (`cmake --build build/ascend --verbose`).
Do not claim the NPU path is fixed without a successful CANN kernel build.
