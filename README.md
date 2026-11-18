# ascend_sn14 — CANN 極簡版

**定位：Ascend CANN 上的 CacheOn SN14 推論算子研發起點，不是已獲獎勵資格的 SN14 礦工。**

## 1. 只需三條指令（Ascend 910B/910C、CANN 8.5.0）

```bash
source /usr/local/Ascend/ascend-toolkit/latest/set_env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4
./build/ascend_sn14 0 32 4096
```

如安裝位置不同，將第一行改成你機器上的 `set_env.sh`；若標頭未找到，加入 `-DASCEND_HOME_PATH=/your/CANN/root`。

- `0`：NPU device ID；`32`：rows；`4096`：RMSNorm hidden size。
- 程式以 **C++17 + AscendCL + aclnnRmsNorm** 執行，無 Python、無 torch_npu、無 SHA-256d、無 Stratum。
- 實際路徑：Host float32 → `aclrtMalloc`/`aclrtMemcpy` → `aclnnRmsNormGetWorkspaceSize` → `aclnnRmsNorm` → D2H → CPU 參考誤差。
- 成功輸出包含 `CANN aclnnRmsNorm OK`、`max_abs_error`；目前只執行一次，不把傳輸時間誤稱成算子算力。
- CANN API / `libopapi_nn.so` 必須由已安裝套件提供；不同硬體與 CANN 版本的算子支援請先核對。

## 2. 這與 CacheOn SN14 的關係

CacheOn SN14 競賽評估的是在**官方指定 Arena** 中，對 SGLang 模組或 prefix cache 的可稽核、可重現推論加速；不是 SHA-256d 挖礦，也不是本機 NPU benchmark。

**限制：截至 2026-11-18，官方公開評測規範使用 NVIDIA/CUDA/SGLang，未公布可提交 Ascend CANN 原生算子的相容 Arena。** 因此本專案的 CANN `.cpp` **不能直接交給官方 `chain-package` 作為有效 Bundle**，也沒有可保證收益或驗證者 PASS 的宣稱。特意不提供會誤導使用者付費提交的假 Bundle。

要正式參賽：
1. 查看 https://cacheon.ai/docs/miner-guide/overview 並確認當前 Arena、目標與允許的 kernel ABI。
2. 在官方 CUDA/SGLang 開發映像檔，依規範實作真正更快且等價的 `forward_pass` 或 `prefix_cache` Bundle；官方專案 https://github.com/latent-to/cacheon 。
3. 執行 `python -m cacheon.cli scan <bundle>`、`verify <bundle>`、官方 Arena `check`，確認正確性與速度，再依 https://cacheon.ai/docs/miner-guide/submitting 使用 `chain-register`、`chain-package`、`chain-submit --dry-run`，最後才考慮正式提交。**這些 CLI 指令是官方協定所需，不在本 CANN 研發程式的熱路徑。**
4. 若 CacheOn 日後公布 Ascend Arena，仍需依官方核准的 CANN ABI/原始碼政策，整合對應 Bundle，才能由驗證者決定是否接受。

## 3. 專案結構

```text
ascend_sn14/
├── src/main.cpp              # 唯一 CANN C++ 算子程式
├── CMakeLists.txt             # 唯一 build 設定
├── README.md                  # 本說明
└── .github/workflows/main.yml # 可選：自行架設 Ascend ARM64 runner 進行建置
```

不包含舊版 TAOHash、CPU 模擬礦工、Python 執行器、預編譯二進位或私鑰。

## 官方技術來源

- CacheOn SN14：https://cacheon.ai/docs
- 提交流程：https://cacheon.ai/docs/miner-guide/submitting
- CANN 8.5 RMSNorm：https://www.hiascend.com/document/detail/en/canncommercial/850/API/aolapi/context/ops-nn/aclnnRmsNorm.md
- CANN NN 算子編譯連結：https://www.hiascend.com/document/detail/en/CANNCommunityEdition/850/API/aolapi/context/common/compilation_running_sample_nn.md
