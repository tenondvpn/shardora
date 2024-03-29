# Shardora
      A Dynamic and Public Blockchain Sharding Platform with Seamless Shards Reconfiguration

# Quick Start
## Requirements
      centos7
      g++8.3.0
      python3.10+

## Run local shardora network
      git clone git@github.com:tenondvpn/shardora.git
      sh local.sh

## Transaction test
      cd ./cbuild_Debug && make txcli
      ./txcli

## Throughput / TPS（Two Shards, 20 servers and 8cores 50nodes/shard)

2024-03-27 12:33:31,077 [INFO] [bft_manager.cc][HandleLocalCommitBlock][2981] tps: 5924.17
2024-03-27 12:33:34,155 [INFO] [bft_manager.cc][HandleLocalCommitBlock][2981] tps: 5353.85
2024-03-27 12:33:37,361 [INFO] [bft_manager.cc][HandleLocalCommitBlock][2981] tps: 5415.45
2024-03-27 12:33:40,366 [INFO] [bft_manager.cc][HandleLocalCommitBlock][2981] tps: 5822.74
2024-03-27 12:33:43,505 [INFO] [bft_manager.cc][HandleLocalCommitBlock][2981] tps: 5643.69
2024-03-27 12:33:46,613 [INFO] [bft_manager.cc][HandleLocalCommitBlock][2981] tps: 6074.38
2024-03-27 12:33:49,642 [INFO] [bft_manager.cc][HandleLocalCommitBlock][2981] tps: 6321.85
2024-03-27 12:33:52,750 [INFO] [bft_manager.cc][HandleLocalCommitBlock][2981] tps: 5553.02
2024-03-27 12:33:55,947 [INFO] [bft_manager.cc][HandleLocalCommitBlock][2981] tps: 7758.51
2024-03-27 12:33:59,108 [INFO] [bft_manager.cc][HandleLocalCommitBlock][2981] tps: 4143.93
2024-03-27 12:34:02,130 [INFO] [bft_manager.cc][HandleLocalCommitBlock][2981] tps: 6733.75
2024-03-27 12:34:05,134 [INFO] [bft_manager.cc][HandleLocalCommitBlock][2981] tps: 4807.78
2024-03-27 12:34:08,280 [INFO] [bft_manager.cc][HandleLocalCommitBlock][2981] tps: 7019.39
2024-03-27 12:34:11,292 [INFO] [bft_manager.cc][HandleLocalCommitBlock][2981] tps: 5673.25
2024-03-27 12:34:14,452 [INFO] [bft_manager.cc][HandleLocalCommitBlock][2981] tps: 5437.10
2024-03-27 12:34:17,475 [INFO] [bft_manager.cc][HandleLocalCommitBlock][2981] tps: 5181.79
2024-03-27 12:34:20,810 [INFO] [bft_manager.cc][HandleLocalCommitBlock][2981] tps: 5390.77
2024-03-27 12:34:23,810 [INFO] [bft_manager.cc][HandleLocalCommitBlock][2981] tps: 6568.68
2024-03-27 12:34:27,081 [INFO] [bft_manager.cc][HandleLocalCommitBlock][2981] tps: 6291.68
2024-03-27 12:34:30,146 [INFO] [bft_manager.cc][HandleLocalCommitBlock][2981] tps: 6873.93
2024-03-27 12:34:33,377 [INFO] [bft_manager.cc][HandleLocalCommitBlock][2981] tps: 4760.56
2024-03-27 12:34:36,407 [INFO] [bft_manager.cc][HandleLocalCommitBlock][2981] tps: 6138.49
2024-03-27 12:34:39,461 [INFO] [bft_manager.cc][HandleLocalCommitBlock][2981] tps: 5250.05
2024-03-27 12:34:42,515 [INFO] [bft_manager.cc][HandleLocalCommitBlock][2981] tps: 5252.67
2024-03-27 12:34:45,587 [INFO] [bft_manager.cc][HandleLocalCommitBlock][2981] tps: 5867.65
2024-03-27 12:34:48,798 [INFO] [bft_manager.cc][HandleLocalCommitBlock][2981] tps: 4098.32

## Latency (US)（Two Shards, 20 servers and 8cores 50nodes/shard)
2024-03-27 13:05:14,848 [INFO] [tx_pool.cc][TxOver][323] tx latency p50: 1407273
2024-03-27 13:05:14,851 [INFO] [tx_pool.cc][TxOver][323] tx latency p50: 1999590
2024-03-27 13:05:14,852 [INFO] [tx_pool.cc][TxOver][323] tx latency p50: 1410339
2024-03-27 13:05:14,864 [INFO] [tx_pool.cc][TxOver][323] tx latency p50: 1179470
2024-03-27 13:05:14,852 [INFO] [tx_pool.cc][TxOver][323] tx latency p50: 1402222
2024-03-27 13:05:14,853 [INFO] [tx_pool.cc][TxOver][323] tx latency p50: 2031765
2024-03-27 13:05:14,854 [INFO] [tx_pool.cc][TxOver][323] tx latency p50: 2143952
2024-03-27 13:05:14,854 [INFO] [tx_pool.cc][TxOver][323] tx latency p50: 2274495
2024-03-27 13:05:14,855 [INFO] [tx_pool.cc][TxOver][323] tx latency p50: 1404248
2024-03-27 13:05:14,855 [INFO] [tx_pool.cc][TxOver][323] tx latency p50: 1404931
2024-03-27 13:05:14,856 [INFO] [tx_pool.cc][TxOver][323] tx latency p50: 2115872
2024-03-27 13:05:15,774 [INFO] [tx_pool.cc][TxOver][323] tx latency p50: 2248965
2024-03-27 13:05:15,775 [INFO] [tx_pool.cc][TxOver][323] tx latency p50: 1482399
2024-03-27 13:05:15,775 [INFO] [tx_pool.cc][TxOver][323] tx latency p50: 1482768
2024-03-27 13:05:15,776 [INFO] [tx_pool.cc][TxOver][323] tx latency p50: 1483402
2024-03-27 13:05:15,777 [INFO] [tx_pool.cc][TxOver][323] tx latency p50: 1484152
2024-03-27 13:05:15,777 [INFO] [tx_pool.cc][TxOver][323] tx latency p50: 1484550
2024-03-27 13:05:15,778 [INFO] [tx_pool.cc][TxOver][323] tx latency p50: 1485404
2024-03-27 13:05:15,780 [INFO] [tx_pool.cc][TxOver][323] tx latency p50: 1467470
2024-03-27 13:05:15,782 [INFO] [tx_pool.cc][TxOver][323] tx latency p50: 1469468
2024-03-27 13:05:15,783 [INFO] [tx_pool.cc][TxOver][323] tx latency p50: 1471160
