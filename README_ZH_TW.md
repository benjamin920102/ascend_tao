# 繁體中文操作入口

完整挖礦設定、官方白皮書演算法對照、建立錢包、Subnet 14 註冊、CANN 8.5/9.0 注意事項、Ascend/CPU 啟動、確認 Share 接受與常見錯誤，**全部在根目錄 [`README.md`](README.md)**。

最快的 CPU 編譯（Linux）：

```bash
sudo apt-get install -y build-essential cmake libssl-dev libjson-c-dev libboost-dev
bash tools/build.sh
```

Ascend 實機：

```bash
export CANN_HOME=/usr/local/Ascend/cann
source "$CANN_HOME/set_env.sh"
WITH_ASCEND=1 TAO_ASC_ARCH=dav-2201 bash tools/build.sh
./bin/taohash_pool --pool btc.taohash.com --port 3331 \
  --user '完整BTC地址.熱錢包前4後4碼-1' --pass x \
  --backend ascend --device 0 --grid 40 --chunk 65536
```

上方的地址是佔位符，務必依 [TAOHash 官網](https://taohash.com/connect) 格式替換。獲得 Alpha 需先註冊 Bittensor Subnet 14；不要在挖礦命令中輸入私鑰。白皮書模型可執行：

```bash
./bin/whitepaper_incentive docs/whitepaper_example.json
```

此公式程式僅供研究，不是礦工核心。`AUTHORIZED` 不是收益，必須以 `SHARE ACCEPTED` 與礦池／鏈上記錄為準。無 Ascend/CANN 實機測試，無收益保證。
