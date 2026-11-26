#!/bin/sh
# 各用例相互独立，也可单独执行: cd tXXX_xxx && python run.py
# 依赖: pip install requests eth_abi eth_utils pycryptodome ecdsa py-solc-x
cd "$(dirname "$0")"
for d in t159_data_match t160_model_match t161_compute_match t162_multimodal; do
  (cd "$d" && python run.py) || echo "!! $d 未通过"
done
