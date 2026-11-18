# Native C++ + AIV-only HBM architecture (source-level revision)

## Core objective

TAO Subtensor `pow_register` uses SHA-256 and legacy Keccak-256 (domain byte 0x01), never PRL/BLAKE3 or Cube GEMM. One AIV worker holds the chain prefix, target, SHA state, Keccak state, and nonce locally. The only persistent device GM allocations per device are a 128-byte prepared challenge, a 32-byte target, and 64 bytes per AIV block.

## Data-flow map

```text
Trusted Bittensor / Rust chain client: fresh public challenge and difficulty
                        |
           C++ native process (persistent)
             - 256-bit target calculation (U256_MAX / difficulty)
             - fixed SHA block-0 midstate once per changed challenge
             - independent nonce range per device/thread
                        |
             pinned H2D buffer + ACL stream
                        |
 Device GM prepared[128] / target[32]           no AIC/Cube
                        |                       no global flags
             AIV block #0 ... AIV block #(G-1)  no inter-core GM queues
                SHA rolling schedule[16]
                SHA compress remaining 2 blocks/nonce
                Keccak-f[1600] 24 rounds
                unsigned LE U256 <= target
                        |
            per-block 64B result cacheline
                        |
               one D2H per kernel chunk
                        |
 C++ verifies rare hits, returns HIT or MISS; new challenge can be queued
```

## Explicit device memory budgets

For `G=40`, 128B prepared buffer + 32B target + 40×64B result = 2,720B allocated in GM. The AIV kernel reads 32B midstate + 59B fixed trailing prefix + 32B target per worker per launch = 123B logical, ~4,920B for 40 workers (original code used 155B each or ~6,200B). The prepared buffer is physically 128B; exact cacheline transactions and register spills may differ. Every block writes its own 64B result slot; D2H reads 2,560B per chunk, irrespective of whether any hit was found. There is **no GM write per unsuccessful hash**.

No D2H overlap is claimed: the host still synchronizes before examining the records. The primary performance target is CPU interpreter and redundant fixed SHA work elimination, not a hypothetical HBM bandwidth multiplier.

## Important remaining costs

1. Two SHA compressions remain per nonce after the C++ fixed-prefix prehash.
2. Keccak-f[1600] still rotates and xors 64-bit words for 24 rounds with scalar register arrays; its live-state pressure can dominate.
3. The ASC compiler might spill the 25-lane `a[]` and `b[]` Keccak states to UB/GM; an ISA-level profile is necessary before making spill claims.
4. Repeated `aclrtSynchronizeStream` and 2.5KiB D2H happen each native chunk. `--batch` changes the amortization versus freshness/latency trade-off.
5. One thread per NPU runs its own ACL context and stream, without device-to-device transfers. The launcher is a single persistent C++ process, not one process/command per nonce.
6. The 6-second default native job deadline is soft; it cannot preempt an outstanding device kernel.

## Feasible next optimizations (not implemented or validated)

- CANN architecture-specific Keccak 32-bit-limb representation to avoid expensive 64-bit rotates on a target ISA, **only if compiled instruction count and spills justify it**.
- Source-level SHA-256 rounds 0–13 partial precomputation for the fixed first 56 bytes of block 1, using a separately verified state transform.
- Multi-lane AIV SIMD, if bitwise/rotate operations and register allocation on the exact NPU/CANN combination are favorable. Do not equate GEMM TOPS with hash throughput.
- Event-driven or two-stream D2H overlap, conditional on real per-chunk latency; may increase complexity/GM traffic without benefit for tiny result records.

## Compatibility and correctness boundaries

- No 910B/910C build or hash-rate measurements are included; the archive was edited on a machine without the user-specific Ascend CANN SDK and device.
- No tests or startup test scripts are included at the user's request. This does not certify consensus correctness or production fitness. **Rare-hit CPU digest and target validation remain required and are part of the runtime**.
- The chain client—not the compute process—must manage work_block freshness, signing, and submission. This scanner does not perform TAO emission mining or alter consensus.
