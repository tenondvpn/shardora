"""用例160 模型要素交易匹配。独立运行: cd t160_model_match && python run.py
前置(自动解决): 合约已部署、run.conf 中 model_N 描述的模型要素已上链"""
import sys, os; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from common import *
print("== 160 模型要素匹配 ==")
ensure_factors(model=True)
print("  链上模型要素数:", counts()[1])
Z = b"\0" * 32
r1 = parse_mreq("t160_req")
res = match_model(r1); man = manual_model(r1)
print("  匹配:", res, " 手工:", man)
check("与手工筛选一致", sorted(res) == sorted(man))
for i in res:   # 核对属性满足需求且与链上登记一致
    _, tt, fw, ver, acc, pm, p, act = get_model(i)
    check(f"模型{i}属性满足需求且与链上登记一致",
          (r1[0] == Z or tt == r1[0]) and (r1[1] == Z or fw == r1[1])
          and acc >= r1[2] and (r1[3] == 0 or pm <= r1[3]) and (r1[4] == 0 or p <= r1[4]) and act)
r2 = parse_mreq("t160_req2")
res2 = match_model(r2); print("  条件2匹配:", res2)
check("变更后与手工一致", sorted(res2) == sorted(manual_model(r2)))
check("结果随条件变化", res2 != res)
re_ = parse_mreq("t160_req_empty")
check("无匹配返回空且与手工一致", match_model(re_) == manual_model(re_) == [])
tx("submitModelMatch(%s)" % T_MREQ, [T_MREQ], [r1])
finish()
