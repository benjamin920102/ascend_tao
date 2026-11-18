# ascend_tao — Ascend AIV-only / C++ TAOHash BTC pool miner

**A real Stratum V1 client with SHA-256d hashing, share submission and reconnection.** This source ZIP replaces the earlier stand-alone **Bittensor `pow_register` Keccak scanner**. The new algorithm is SHA-256d for the TAOHash Bitcoin pool on Bittensor Subnet 14. The older registration scanner cannot generate valid Bitcoin pool shares.

> **Validation status:** the CPU-side C++ miner builds on Linux, and the SHA-256d implementation matches OpenSSL on a known Bitcoin header. The ASC kernel and live pool accepted-share path **have not been executed** on an Ascend NPU in this environment. No NPU hardware, pool connection or payout can be claimed from this ZIP alone. CANN 9.0.0 ARM64 GitHub Actions compilation requires an external CANN download, and is not a hardware runtime verification.

## Files

- `src/miner.cpp` — C++ Stratum V1 socket client, subscribe/authorize, difficulty, notify, merkle, share submit, reconnect, rate statistics.
- `src/sha256d.cpp`, `include/sha_core.h`, `include/sha256d_core.h` — SHA256d header hashing with precomputed SHA midstate and independent host verification.
- `ascend/sha256d.asc` — AIV-only SHA256d kernel, no AIC/Cube, consumer flags, inter-core messages or per-nonce GM intermediate buffers.
- `ascend/CMakeLists.txt` — ASC/BiSheng build linked with ACL runtime only. **No `-ltiling_api`, `-lregister`, `-lplatform`**.
- `tools/build.sh` — native CPU backend build and optional Ascend plugin. No Python runtime or code generation scripts in this project.
- `.github/workflows/main.yml` — GitHub Actions Ubuntu ARM64 + CANN 9.0.0 build and prerelease artifact.
- `legacy/` — archived `pow_register` sources; **not built** by this release.

## Pool connection details (Bitcoin)

- Pool: `btc.taohash.com`
- Normal difficulty: `3331`
- High difficulty: `3332` (not recommended for a slow software miner)
- Protocol: **Bitcoin Stratum V1**, plaintext TCP (no TLS in this implementation)
- Algorithm: SHA-256d, including 80-byte Bitcoin block header and 4-byte nonce
- Stratum user: **`payout_address.hotkey_slug-worker_id`**. `hotkey_slug` = first 4 + last 4 characters of your Bittensor hotkey, with optional `-1` etc. For BTC-only payouts, consult the pool's currently supported account syntax.
- Password: `anything123` or `x`. Optional difficulty hint: `x;md=1000;` (avoid imposing an unreachable difficulty on a slow device).

Authoritative pool setup: https://taohash.com/connect and https://taohash.com/btc/pool . Subnet 14 Alpha rewards additionally require registration on the subnet; accepting shares alone does not establish Bittensor eligibility.

## Build locally: native C++ Stratum client (CPU fallback)

`json-c` is **not bundled**. Installing `libjson-c-dev` provides both `json-c/json.h`
and the linker library; installing `libssl-dev` and `libboost-dev` supplies the
other compile-time dependencies. CMake detects json-c directly and does not
require `pkg-config` or a `json-c.pc` file.

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libssl-dev libjson-c-dev libboost-dev
bash tools/build.sh
```

## Build with Ascend 910B / 910C on CANN 9.0.0

```bash
export CANN_HOME=/usr/local/Ascend/cann
export WITH_ASCEND=1
export TAO_ASC_ARCH=dav-2201
bash tools/build.sh
```

This produces (host architecture matches the machine on which it was built):

```text
bin/taohash_pool
bin/libtaohash_npu.so   # only with WITH_ASCEND=1
```

The ASC module directories and runtime library must be present under `CANN_HOME`. Source `${CANN_HOME}/set_env.sh` before execution. Running on another CANN version can fail due to ABI differences; rebuild against the exact target runtime.

## Run with actual pool

```bash
source "$CANN_HOME/set_env.sh"    # required when running the Ascend plugin
./bin/taohash_pool \
  --pool btc.taohash.com --port 3331 \
  --user 'YOUR_FULL_BTC_ADDRESS.YOUR_8_CHARACTER_HOTKEY_SLUG-1' \
  --pass x --backend ascend --device 0 --grid 40 --chunk 65536
```

Use a real BTC payout address and a correctly derived hotkey slug. Never paste a mnemonic, coldkey or private key into the miner; **none are required** for Stratum authentication. With `--backend ascend`, failure to load or initialize the NPU library is **fatal** and cannot silently fall back to CPU. The library is discovered next to `bin/taohash_pool`, or you can set `--npu-lib /absolute/path/libtaohash_npu.so`.

Use `--backend cpu` if you only want C++ SHA-256d mining on a generic Linux system; it can connect and submit a share if one is found, but its production Bitcoin hashrate is extremely low. `--backend auto` first attempts Ascend and explicitly announces fallback to CPU.

## What happens on the wire

1. TCP connect -> `mining.subscribe` -> `mining.authorize`.
2. Process `mining.set_difficulty`, `mining.set_extranonce`, `mining.notify`.
3. Build coinbase from `coinb1 + extranonce1 + extranonce2 + coinb2`, double-SHA256, Merkle branches, 80-byte header, and Stratum prevhash word byte swaps.
4. Scan nonces in AIV chunks. Verify any candidate independently in C++ and OpenSSL before submitting.
5. Send `mining.submit` with `[username, job_id, extranonce2, ntime, nonce]` and report `SHARE ACCEPTED` or `SHARE REJECTED` from pool response.
6. Invalidate jobs on reconnect / new work, roll extranonce2 after nonce range exhaustion, and reauthorize.

An authorized connection **does not** prove that NPU hashing is fast enough to submit shares. On ordinary pool difficulties, software SHA-256d on a 910B/910C is unlikely to compete economically with SHA-256 ASIC miners. Hashrate, pool acceptance, payouts and earnings remain unverified.

## GitHub Actions ARM64 build

Upload all files **inside the ZIP** to a GitHub repository (keep `.github/workflows/main.yml`), select **Actions -> Ascend TAOHash Pool Miner -> Run workflow**, and download `ascend-taohash-aarch64-cann9.0.0.tar.gz` under Artifacts (also prerelease attachment). The job compiles C++17 and ASC with CANN 9.0.0. A standard GitHub-hosted runner has no Ascend NPU and cannot actually mine or runtime-verify ASC kernels. The workflow packages `bin/taohash_pool` and `bin/libtaohash_npu.so` (not the archived `tao_native` executable); it passes `WITH_ASCEND=1` explicitly. The CANN installer URL may need to be updated if the vendor changes hosting.

## Security / operational notes

- Pool TCP traffic is unencrypted. Do not send private credentials as a password.
- Do not use port `3332` unless your hashrate supports the higher share difficulty.
- Live Stratum V1 pools may require protocol extensions not implemented in this minimal client (e.g. mandatory `mining.configure` / version rolling). `mining.notify` / SHA-256d is the supported baseline.
- The scanner submits the first valid share it sees per AIV worker in each launch; no inter-core GM communication.
- Does not mine Bittensor's TAO emission directly. TAOHash may pay BTC and, when applicable, Subnet 14 Alpha according to pool and Bittensor rules.
