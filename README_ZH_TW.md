# Ascend TAOHash 礦池挖礦：繁體中文快速操作

本 ZIP 不是原本的 `pow_register` 註冊工作掃描器，而是 C++17 實作的 **Bitcoin Stratum V1／SHA-256d** 礦池程式。包含接收工作、處理礦池難度、AIV nonce 掃描、CPU 雙重 SHA-256 驗證、提交 Share、接收 accepted/rejected 結果及斷線重連。

## 已修正 GitHub Actions 設定及 CMake 依賴

- `json-c` 失敗的原因：workflow 遺漏 `libjson-c-dev`。已補上 `libjson-c-dev`、`libssl-dev`、`libboost-dev`。
- 原始專案的 GitHub workflow 要打包 `tao_native`，但 build.sh 實際編譯 `taohash_pool`；已統一輸出 `taohash_pool` 與 `libtaohash_npu.so`，並設定 `WITH_ASCEND=1`。
- CMake 改用 `find_path` / `find_library` 偵測 json-c，不需 `pkg-config`；但**仍需安裝 json-c 開發套件**，不能只安裝執行時期 `.so`。
- 舊版 `pow_register` 的 `native/` 與 `kernel/` 已移到 `legacy/`，供參考，**不參與礦池程式編譯**。


`-ltiling_api`、`-lregister`、`-lplatform` 不是這個 kernel 必須的依賴。`ascend/CMakeLists.txt` 已移除，改以 ASC linker 和實際發現的 CANN ACL runtime 函式庫連結。這是原錯誤的具體修正，不代表已在 NPU 上驗證成功。

## GitHub 編譯 ARM64 + Ascend

將此 ZIP 的原始碼放入 GitHub repository，執行 **Actions > Ascend TAOHash Pool Miner > Run workflow**。成功時下載 ARM64 `ascend-taohash-aarch64-cann9.0.0.tar.gz`。檔案包含 `bin/taohash_pool` 與 `bin/libtaohash_npu.so`。

本地一般 Linux 已可完成 `taohash_pool` C++ 編譯；GitHub runner 不配備 Ascend NPU，只能編譯，不能實際測量挖礦效能或驗證送出的 Share。若挖礦機使用 CANN 8.5.0，請勿直接把 CANN 9.0.0 二進位拿去執行；應使用與實機相同版本重新編譯。

## 實機啟動

```bash
source /usr/local/Ascend/cann/set_env.sh
./bin/taohash_pool \
  --pool btc.taohash.com --port 3331 \
  --user '完整BTC收款地址.熱錢包前4後4碼-1' \
  --pass x --backend ascend --device 0 --grid 40 --chunk 65536
```

`--backend ascend` 可避免在 NPU 無法載入時悄悄改成 CPU。`--backend cpu` 則可以只用原生 C++ 在一般 Linux 機器接礦池（SHA-256d 軟體運算的分享難度可能太高，不保證能提交成功）。

## 需要注意

- 正式 TAOHash BTC 礦池：`btc.taohash.com:3331`；官網帳號格式 `payout_address.hotkey_slug-worker_id`。
- 真正送出 Share 時會列印 `SHARE SUBMITTED`，礦池回覆才會出現 `SHARE ACCEPTED` 或 `SHARE REJECTED`。出現 `AUTHORIZED` 不表示已找到 Share。
- CPU C++ 程式本機能完成編譯，SHA-256d 與 OpenSSL 已核對。Ascend 910B／910C、CANN 編譯、實際礦池回報、收益都尚未在本環境實測。沒有提供虛假的算力或接受率數字。
- GitHub Actions **沒有 Python 步驟或測試工作**，原始碼只有 C++、Ascend C、Bash、CMake、YAML。

官方連接說明：https://taohash.com/connect

## 本機 CPU 編譯

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libjson-c-dev libssl-dev libboost-dev
bash tools/build.sh
./bin/taohash_pool --help
```

目前修正版本的 C++ host 端已在 Linux x86_64 以 CMake 編譯成功；昇騰 ASC kernel 還沒有在配備 CANN 及 NPU 的機器上編譯和執行驗證，不能把此結果當作 910B/910C 效能數字。

## Bisheng 15.0.5: `Cannot select ... i64 = bswap` 修正

前一版 `th_scan_kernel` 每次 nonce 都在裝置端建立 byte buffer、將 SHA digest 以 byte 拆裝，Bisheng 15.0.5/DAV-2201 在最佳化時可能產生不支援的 `i64 bswap` DAG。這一版：

- 將 AIV hot path 改成 **32-bit SHA state/message words**，在 CPU 事先準備 midstate、tail word 與 target word。
- 將 SHA256d 兩次壓縮直接以 `uint32_t[16]` 工作字處理，移除裝置端的大量 byte-array pack/unpack。
- 單一 nonce 的 32-bit 位元組順序轉換採用 4 個 `volatile` byte loads，避免被 Bisheng 合併成不支援的 wide `bswap`。
- 一般 CPU 端雜湊及礦池協定不變，找到 nonce 後由 CPU 完整重算 SHA256d 並核對 target。
- 保留 **AIV-only**，不使用 Cube、AIC/AIV flags，也不新增 Python runtime。

注意：本環境沒有 CANN/Bisheng 15.0.5，**不能宣稱 ASC 編譯已通過**；請在 GitHub Actions 重新執行。這些修改以規避所貼出的後端選指錯誤為目標，並非已量測過的 NPU 加速數據。
