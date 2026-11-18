# ascend_tao — Ascend 910B / 910C TAOHash Subnet 14 礦工（C++ + AIV）

**這個專案提供可操作的 Bitcoin SHA-256d / Stratum V1 礦池礦工，對接 Bittensor Subnet 14 (TAOHash)，另附依 Bittensor 原始白皮書公式寫成的 C++ 共識參考程式。** 礦工執行程序不依賴 Python，Ascend 裝置核心只用 AIV，不使用 Cube/AIC↔AIV 旗標同步。

> ⚠️ **白皮書不是 SHA-256d 挖礦規格**。Bittensor [原始白皮書](https://www.bittensor.com/whitepaper) 定義節點評分、共識、激勵與 Bonds；實際要挖什麼由個別 Subnet 決定。TAOHash 官方的 [Subnet 14](https://github.com/latent-to/taohash) 指定 BTC SHA-256d Share，所以 **不能**把裝置雜湊核心換成白皮書的 `R = WᵀS`，那會造成無效 Share。白皮書程式在 `src/whitepaper_incentive.cpp`，只是獨立的公式參考，不參與實際挖礦；對照說明請看 [`docs/ALGORITHM_MAPPING.md`](docs/ALGORITHM_MAPPING.md)。
>
> **驗證限制**：提供的是原始碼，不是保證有收益的現成挖礦服務。可在一般 Linux 建置並使用 CPU backend；目前無法在本環境實測 Ascend 910B/910C、CANN ASC kernel 實機吞吐、正式礦池接受率或收益。實際 BTC 挖礦與 ASIC 相比通常沒有競爭力。

## 1. 要挖的是什麼？

| 項目 | 本專案實際情況 |
|---|---|
| Bittensor Subnet | **14 / TAOHash**，非所有 Bittensor 子網 |
| 設備端演算法 | **Bitcoin SHA-256d**，80-byte block header + 32-bit nonce |
| 網路協定 | Bitcoin **Stratum V1**，`mining.subscribe`、`authorize`、`notify`、`submit` |
| Ascend 實作 | 純 AIV nonce 掃描，CPU 驗證再提交，不使用 Cube 與 inter-core flags |
| 收益管道 | BTC：TAOHash TIDES；Alpha：需額外完成 Bittensor Subnet 14 hotkey 註冊 |
| 白皮書對應 | 驗證者評分／Yuma 分配收益；白皮書公式用獨立的 `whitepaper_incentive` 展示 |
| 已排除 | 舊版 `pow_register`（保存在 `legacy/`）、BCH、任意子網挖礦、以 SHA-256d 直接挖出 TAO |

官方資料：[Bittensor 挖礦說明](https://www.bittensor.com/docs/guides/mining) · [TAOHash 連線參數](https://taohash.com/connect) · [TAOHash 礦工指南](https://github.com/latent-to/taohash/blob/main/docs/running_miner.md)

## 2. 建立 Bittensor 錢包（只有要領 Alpha 才必須）

這一步建議在**可信任的獨立 Linux 電腦**做。錢包管理工具 `btcli` 本身可能透過 Python 發佈，但 **C++ 礦工程序完全沒有 Python 依賴**；不需要把 coldkey、助記詞、私鑰複製到挖礦機。

```bash
# 請以 Bittensor 官方文件為準，先確認安裝來源及目前版本
python3 -m pip install --upgrade bittensor
btcli wallet create -w my_coldkey
# 若要使用另取名稱的 hotkey，額外建立（否則沿用預設 hotkey 即可）
btcli wallet new-hotkey -w my_coldkey -H my_hotkey
btcli wallet list
btcli subnets show 14
```

完成後確認 hotkey 的完整 **SS58 位址**（通常是 `5...`）。coldkey 負責資產，不放在礦機。這些是示範名稱；已經有錢包可直接沿用。

**只要 BTC／TIDES，不要 Alpha 的人**：根據 TAOHash 官方說明，可以不註冊 Bittensor，但仍須提供正確的收款帳號格式。BTC/Lightning 收款方式與使用的 worker 名稱請以 [TAOHash Connect](https://taohash.com/connect) 為準。

## 3. 若要領 Subnet 14 Alpha，先註冊 Hotkey

在錢包主機上檢查 Subnet 14 的註冊成本與自己的餘額，**確認價格再付款**。請先執行 `btcli config set network finney` 確認主網。註冊通常會消耗 TAO 或依當前鏈上註冊政策處理，不代表保證有收益。

```bash
btcli query burn --netuid 14 --json
btcli query subnet-hyperparameters --netuid 14 --json
btcli wallet registrations
# 下列以新版官方 CLI 命令為例；請先以 btcli tx burned-register --help 核對
btcli tx burned-register --netuid 14 --hotkey my_coldkey/my_hotkey --dry-run -w my_coldkey
btcli tx burned-register --netuid 14 --hotkey my_coldkey/my_hotkey -w my_coldkey
btcli wallet registrations
```

若 CLI 是舊版或 Subnet 官方另有指示，可能使用 `btcli subnet register --netuid 14 --wallet.name my_coldkey --wallet.hotkey my_hotkey --network finney`。**不可以在兩種命令都執行一次**；只需要完成一次註冊，並確認**你要用的那一把 hotkey** 已在 Subnet 14 上（上述 `--hotkey my_coldkey/my_hotkey` 是明確指定，避免誤註冊預設 hotkey）。指令格式會隨 Bittensor CLI 版本變動，請先看 CLI 的 `--help`。

## 4. 在 Linux 編譯 C++ 礦工

在 x86_64 或 ARM64 Linux 上安裝開發依賴：

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libssl-dev libjson-c-dev libboost-dev
bash tools/build.sh
./bin/taohash_pool --help
```

輸出：

- `bin/taohash_pool`：真正的 SHA-256d Stratum V1 C++ 礦工。
- `bin/whitepaper_incentive`：白皮書激勵公式計算器（**不挖礦**）。

**Ascend 910B／910C** 若裝有與機器匹配的 CANN 開發環境：

```bash
export CANN_HOME=/usr/local/Ascend/cann
source "$CANN_HOME/set_env.sh"
export WITH_ASCEND=1
export TAO_ASC_ARCH=dav-2201   # 依實機晶片及 CANN 文件確認架構值
bash tools/build.sh
ls -lh bin/taohash_pool bin/libtaohash_npu.so
```

`CANN_HOME` 依安裝路徑調整。**CANN 8.5.0 裝置不可假設能執行用 9.0.0 編出來的共享函式庫**，請在適用版本重新編譯；如果只拿到 GitHub Actions 產出的 binary，必須核對 CANN ABI、機器架構及裝置型號。原始 ASC 核心有針對 Bisheng `i64 bswap` 選指問題做 32-bit word path 調整，但 **未宣稱已在此實機編譯成功**。

## 5. 設定礦池帳號（不要填私鑰）

**TAOHash BTC 礦池**：

```text
Host:     btc.taohash.com
Port:     3331           # 一般難度，優先使用
Port:     3332           # 高難度，不建議低算力設備使用
Username: 完整BTC收款地址.熱錢包前4碼後4碼-1
Password: x
```

例如 SS58 hotkey 如果開頭 `5EX7`、末尾 `d4Eu`，worker 後綴就是 `5EX7d4Eu-1`。**這只是格式示例，不能當作真實熱錢包**。BTC 地址請使用自己的完整有效地址，不可以把 `bc1q...` 省略號放進去；Lightning 用戶要按官方工具的地址格式／編碼處理。

### 啟動 Ascend 實機挖礦

```bash
source "$CANN_HOME/set_env.sh"
./bin/taohash_pool \
  --pool btc.taohash.com --port 3331 \
  --user 'YOUR_FULL_BTC_ADDRESS.YOUR_HOTKEY_SLUG-1' \
  --pass x --backend ascend --device 0 --grid 40 --chunk 65536
```

**務必換掉 `YOUR_FULL_BTC_ADDRESS` 與 `YOUR_HOTKEY_SLUG`**。`--backend ascend` 若無法載入 NPU 插件會立即報錯，不會暗中退回 CPU。

### 沒有 Ascend，只使用 CPU（功能可通，算力通常極低）

```bash
./bin/taohash_pool \
  --pool btc.taohash.com --port 3331 \
  --user 'YOUR_FULL_BTC_ADDRESS.YOUR_HOTKEY_SLUG-1' \
  --pass x --backend cpu --chunk 65536
```

`--backend auto` 可以嘗試 NPU，失敗時明確顯示 CPU fallback；要確定實際用到 NPU，請指定 `--backend ascend`。停止時按 **Ctrl+C**。

## 6. 怎麼確認真的開始挖？

請按下列順序看 log，不能只看算力數字：

```text
TCP connected       → 已連線（不是找到 Share）
subscribed          → 已取得 Stratum 訂閱資訊
pool AUTHORIZED     → 礦池接受使用者帳號（不是賺到收益）
mining job=...      → 已接收到礦池工作
rate=... MH/s       → 本地雜湊掃描速度；只反映計算
SHARE SUBMITTED     → 發現並送出符合本地門檻的候選 Share
SHARE ACCEPTED      → 礦池確認接受，才是有效提交
SHARE REJECTED      → 礦池拒收，需檢查原因
accepted_diff_sum   → 已被接受 Share 的難度總和，僅為運算貢獻指標（非代幣）
```

到 [TAOHash Leaderboard](https://taohash.com/leaderboard) 檢查 hotkey、BTC 地址、last share、share value、哈希率及 Alpha/UID 訊息。**只有 `AUTHORIZED` 或 `rate > 0`，不能據此判定有 BTC/Alpha 收益**。本程式只支援明文 Stratum V1，網路不加密，不可把敏感憑證放進 `--pass`。

常見障礙：

1. **`pool rejected mining.authorize`**：地址／worker 名稱不符官方格式；先以官方網站重新生成。
2. **`requested --backend ascend is unavailable`**：檢查 `libtaohash_npu.so`、`CANN_HOME`、`LD_LIBRARY_PATH`、裝置權限與 CANN ABI。
3. **有 job、有 rate、長時間沒有 Share**：算力不足以達到池難度 3331 的 Share target，**這不是收益保證，也不一定是程式壞掉**；910B/910C 與專業 SHA-256 ASIC 算力不可相提並論。
4. **`SHARE REJECTED`**：可能是 job stale、時間/nonce、帳號、版本滾動需求或礦池協定差異；以礦池回應的 error 為準。
5. **有 accepted，沒有 Alpha**：確認 Subnet 14 hotkey 註冊、worker slug 與 hotkey 對應及礦池統計；BTC 與 Alpha 是兩個獨立獎勵管道。

## 7. 白皮書公式實作（不會挖出 Share）

原始白皮書定義：

- 排名 `R = Wᵀ S`（W：互評權重，S：stake 比例）。
- 信任 `T[i,j] = 1` iff `W[i,j] > 0`。
- 共識 `C = sigmoid(ρ (TᵀS − κ))`。
- 原始激勵 `I = R ⊙ C`；以 Bonds 分配部分價值給對貢獻者評分的節點。

程式用 `bonds[target][owner]` 作索引，歸一化後計算 `BᵀI` 與白皮書舉例的 `0.5 × dividend + 0.5 × miner incentive`。有些歸一化及資料索引慣例屬**明示的模擬選擇**，**不是現行 Yuma 鏈上實作**。詳見 [`docs/ALGORITHM_MAPPING.md`](docs/ALGORITHM_MAPPING.md)。

```bash
./bin/whitepaper_incentive docs/whitepaper_example.json
```

修改 `docs/whitepaper_example.json` 中的 `stake`、`weights`、`rho`、`kappa`、`tau` 可觀察不同節點的 `rank_W_transpose_S`、`consensus_sigmoid`、`normalized_incentive` 與 `stake_next`。該模擬器不連 Subtensor、沒有代幣簽章、不會提交 validator weights，也**無法預測現在的 Alpha 收益**。

## 8. GitHub Actions 編譯（ARM64）

把原始碼連同 `.github/workflows/main.yml` 上傳 GitHub repository，進入 **Actions → Ascend TAOHash Pool Miner → Run workflow**。現有 workflow 使用 CANN **9.0.0**、Ubuntu ARM64、`dav-2201`；成功才會產出 `ascend-taohash-aarch64-cann9.0.0.tar.gz`，內含 CPU 主程式、AIV 插件、白皮書計算器和說明檔。Actions runner **沒有 NPU**，無法代替實機驗證；CANN 安裝鏈結若變更也可能造成下載失敗。

## 專案檔案

```text
src/miner.cpp                      Stratum V1 主機／網路／提交 Share
src/sha256d.cpp                   CPU ↔ Ascend 資料準備與結果驗證
ascend/sha256d.asc                AIV-only SHA-256d nonce kernel / ACL
include/sha_core.h                SHA-256 壓縮
include/sha256d_core.h            固定 header 的雙 SHA-256
include/sha256d_aiv_words.h       ASC 32-bit words / Bisheng bswap 對策
src/whitepaper_incentive.cpp      白皮書公式示範（不接礦池）
docs/whitepaper_example.json      模擬輸入
docs/ALGORITHM_MAPPING.md         白皮書與 TAOHash 對應及公式界線
legacy/                           已停用的 pow_register 歷史程式
```

**English summary:** `taohash_pool` is a native C++17 BTC Stratum V1 miner for **TAOHash Subnet 14**, not a generic TAO mining algorithm. `whitepaper_incentive` separately demonstrates the original peer-ranking, consensus and bonds formulas; it is **not** used to hash or submit shares. No Python runtime is required for the miner; wallet setup uses the Bittensor CLI on a separate trusted machine. Ascend hardware success, share acceptance and profitability are **unverified**.
