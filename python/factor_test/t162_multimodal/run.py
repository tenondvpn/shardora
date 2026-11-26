"""用例162 多模态要素协同计算。独立运行: cd t162_multimodal && python run.py
前置(自动解决): 合约已部署、data_N/model_N/compute_N 三类要素已上链；所需算力时段已被占用时自动补登记算力"""
import sys, os; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from common import *
print("== 162 多模态协同计算 ==")
ensure_factors(data=True, model=True, compute=True)
d, m, c = parse_dreq("t162_data_req"), parse_mreq("t162_model_req"), parse_creq("t162_compute_req")
S, E = c[3], c[4]
ensure_compute_for(c)

tx(f"createTask({T_DREQ},{T_MREQ},{T_CREQ})", [T_DREQ, T_MREQ, T_CREQ], [d, m, c])
tid = counts()[3] - 1; print("  task id:", tid)

# 三类同时匹配，输出组合方案
check("三类要素均有候选", bool(match_data(d) and match_model(m) and match_compute(c)))
tx("matchTask(uint256)", ["uint256"], [tid])
t = call("getTask(uint256)", ["uint256"], [tid], [T_TASK])[0]
did, mid, cid, cost, status = t[4], t[5], t[6], t[7], t[9]
print(f"  方案: data={did} model={mid} compute={cid} cost={cost} status={status}")
exp_cost = get_data(did)[6] + get_model(mid)[6] + get_compute(cid)[6] * ((E - S + 3599) // 3600)
check("方案各要素满足需求", did in manual_data(d) and mid in manual_model(m) and cid in manual_compute(c))
check("费用=数据+模型+算力*小时数", cost == exp_cost)
check("状态为Matched", status == 1)

# 确认并执行(链下执行，结果哈希上链)
tx("confirmTask(uint256)", ["uint256"], [tid])
local_result = hashlib.sha256(f"data{did}|model{mid}|compute{cid}".encode()).digest()  # 本地同条件计算结果
tx("completeTask(uint256,bytes32)", ["uint256", "bytes32"], [tid, local_result])
tx("settleTask(uint256)", ["uint256"], [tid])

# 结果比对
t = call("getTask(uint256)", ["uint256"], [tid], [T_TASK])[0]
check("链上结果哈希与本地计算一致", t[8] == local_result)
check("状态为Settled", t[9] == 4)

# 链上记录关联
recs = call("getRecords(uint256)", ["uint256"], [tid], [f"{T_REC}[]"])[0]
for r in recs: print("  record(kind,type,factorId,party,amount):", r[0], r[1], r[2], r[3], r[4])
check("共9条记录(授权/调用/结算 x 三类)", len(recs) == 9)
for kind, name in ((0, "调用"), (1, "授权"), (2, "结算")):
    types = sorted(r[1] for r in recs if r[0] == kind)
    check(f"{name}记录覆盖数据/模型/算力", types == [0, 1, 2])
check("记录要素与方案一致", {(r[1], r[2]) for r in recs} == {(0, did), (1, mid), (2, cid)})
check("结算金额合计等于总费用", sum(r[4] for r in recs if r[0] == 2) == cost)
finish()
