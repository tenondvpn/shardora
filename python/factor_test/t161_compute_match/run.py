"""用例161 算力要素交易匹配(含时段占用)。独立运行: cd t161_compute_match && python run.py
前置(自动解决): 合约已部署、compute_N 算力已上链；占用时段所需的数据/模型要素(data_N/model_N)已上链"""
import sys, os; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from common import *
print("== 161 算力要素匹配 ==")
ensure_factors(data=True, model=True, compute=True)
print("  链上算力要素数:", counts()[2])
r = parse_creq("t161_req")
res = match_compute(r); man = manual_compute(r)
print("  匹配:", res, " 手工:", man)
check("与手工筛选一致", sorted(res) == sorted(man))

# 通过协同任务占用算力时段(若该时段已被占用过，则直接取已占用的算力)
od, om, oc = parse_dreq("t161_occupy_data"), parse_mreq("t161_occupy_model"), parse_creq("t161_occupy_req")
if match_compute(oc):
    tx(f"createTask({T_DREQ},{T_MREQ},{T_CREQ})", [T_DREQ, T_MREQ, T_CREQ], [od, om, oc])
    tid = counts()[3] - 1
    tx("matchTask(uint256)", ["uint256"], [tid])
    occ = call("getTask(uint256)", ["uint256"], [tid], [T_TASK])[0][6]
    tx("confirmTask(uint256)", ["uint256"], [tid])
else:
    print("  占用需求时段已被占用，沿用已占用的算力")
    occ = next((i for i in range(counts()[2]) if any(oc[3] < e and s < oc[4] for s, e, _ in occupied(i))), None)
    check("找到已占用算力", occ is not None)
print(f"  算力{occ}占用时段:", occupied(occ))

res2 = match_compute(r); print("  占用后匹配:", res2)
if oc[3] < r[4] and r[3] < oc[4]:
    check("已占用算力不参与匹配", occ not in res2)
check("占用后与手工一致", sorted(res2) == sorted(manual_compute(r)))
rf = parse_creq("t161_free_req")
check("不重叠时段与手工一致", sorted(match_compute(rf)) == sorted(manual_compute(rf)))
ro = parse_creq("t161_out_req")
check("超出可用窗口无匹配", match_compute(ro) == manual_compute(ro) == [])
finish()
