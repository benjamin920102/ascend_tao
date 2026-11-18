# Whitepaper alignment and the actual mining algorithm

## Source of truth

1. Original paper (Yuma Rao), **Bittensor: A Peer-to-Peer Intelligence Market**:
   https://www.bittensor.com/whitepaper
2. Current Bittensor mining docs (a miner implements a **subnet-defined commodity**):
   https://www.bittensor.com/docs/guides/mining
3. Current Bittensor Yuma Consensus docs (validator weights, bonds, rewards):
   https://www.bittensor.com/docs/internals/consensus
4. TAOHash official Subnet 14 repository and miner docs:
   https://github.com/latent-to/taohash
   https://github.com/latent-to/taohash/blob/main/docs/running_miner.md
5. Official pool connection details: https://taohash.com/connect

## The distinction that determines correctness

There is **no global SHA-256 / Keccak / SHA-256d miner described by the
Bittensor original whitepaper**. The paper proposes a peer-to-peer intelligence
market with stake-weighted evaluations and bonds. Its equations do not specify
any pool-share hash, nonce search, or general-purpose Bittensor mining endpoint.
The current chain rewards **subnet-defined digital commodities** and runs the
modern *on-chain* Yuma Consensus over validator-set weights.

**Subnet 14 TAOHash** happens to use Bitcoin **double-SHA-256 (SHA-256d)**
shares as its digital commodity. Its validators assign rewards based on the
value of shares submitted for eligible hotkeys; that is the bridge between
hashing and Bittensor incentives. Replacing SHA-256d with the whitepaper's
stake/weight formula will make a Stratum V1 share **invalid**.

## Executable reference for original paper: `whitepaper_incentive`

This **separate** native C++17 executable maps directly to the original paper:

| Original paper concept | This reference implementation |
|---|---|
| Stake S | `normalized_stake`: `S_i >= 0`, `sum(S)=1` |
| Peer weights W | `normalized_weights_owner_by_target`: `W[i][j]` means owner `i` assigns weight to target `j`; rows normalized |
| Ranking R | `rank_W_transpose_S[j] = sum_i W[i][j] * S[i]` |
| Trust T | `T[i][j]=1` if `W[i][j]>0`, else 0 |
| Consensus C (paper Eq. 4) | `C[j]=sigmoid(rho * (sum_i T[i][j]*S[i] - kappa))` |
| Incentive I (paper Eq. 5) | `raw_incentive[j]=R[j]*C[j]`; normalized to shares for allocation |
| Bonds (paper Sec. 3) | `bonds[target][owner] += W[owner][target]*S[owner]`; normalize **per target** so it represents ownership fractions |
| Dividend (paper Eq. 8) | `dividend[owner]=sum_target bonds[target][owner]*I[target]`, i.e., `B^T I` under the documented indexing |
| Minted stake example (paper Eq. 8) | `deltaS=0.5*dividend+0.5*incentive`; `S_next=S+tau*deltaS` |

Notes on interpretation: the paper sketches bond accrual and uses the term
`B^T I`, but does not define a full modern chain runtime normalization and
quantization implementation. Normalizing stake, W rows, I, and bond ownership
makes the illustrative formula meaningful as shares. This is **not** a claim of
bit-for-bit or payout equivalence with 2026 Yuma Consensus. Use the actual
Bittensor on-chain implementation to obtain real emissions.

```bash
bash tools/build.sh
./bin/whitepaper_incentive docs/whitepaper_example.json
```

The JSON input is an example with three hypothetical peers; edit the stakes,
weights, `rho`, `kappa`, `tau`, optionally `bonds`. This executable **does not
connect** to Subtensor, submit weights, mine TAO, generate Alpha, or predict a
payout. The SHA-256d pool miner does **not** call it.

## Actual work / payout mapping for Subnet 14

1. Pool sends `mining.notify` jobs and `mining.set_difficulty` by Stratum V1.
2. C++ builds an 80-byte Bitcoin candidate block header, hashes it twice via
   SHA-256, compares the 256-bit value against the pool share target.
3. AIV-only Ascend kernel (`ascend/sha256d.asc`) scans independent nonces. No
   Cube and no AIC/AIV handshake; the CPU verifies candidates with OpenSSL.
4. C++ submits `mining.submit`. The miner logs `accepted_diff_sum = sum(pool-assigned difficulty of accepted shares)` as a **local work proxy, not tokens**. Only **pool-accepted** shares count as confirmed
   protocol submissions. Local hashrate or `AUTHORIZED` is NOT a reward.
5. TAOHash's validators evaluate contribution / share value and submit
   subnet weights. Bittensor on-chain Yuma Consensus determines emissions for
   **registered** hotkeys. Pool BTC payouts use the separate TIDES protocol.

**Scope**: current code supports **BTC SHA-256d** TAOHash Stratum V1 only; it
is not a BCH backend, generic Bittensor subnet miner, validator, or Bitcoin
full-node/block producer. CANN/AIV device runtime, pool accepted shares and
real-world profit have NOT been independently verified for this archive.
