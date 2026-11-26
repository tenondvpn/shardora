"""用例159 数据要素交易匹配。独立运行: cd t159_data_match && python run.py
前置(自动解决): 合约已部署、run.conf 中 data_N 描述的数据要素已上链"""
import sys, os; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from common import *
print("== 159 数据要素匹配 ==")
ensure_factors(data=True)
print("  链上数据要素数:", counts()[0])
r1 = parse_dreq("t159_req")
res = match_data(r1); man = manual_data(r1)
print("  条件1匹配:", res, " 手工:", man)
check("返回要素均满足全部条件", all(i in man for i in res))
check("与手工筛选一致(无遗漏无错配)", sorted(res) == sorted(man))
r2 = parse_dreq("t159_req2")
res2 = match_data(r2); man2 = manual_data(r2)
print("  条件2匹配:", res2, " 手工:", man2)
check("变更条件后与手工一致", sorted(res2) == sorted(man2))
check("结果随条件变化", res2 != res)
r3 = parse_dreq("t159_req3")
check("条件3与手工一致", sorted(match_data(r3)) == sorted(manual_data(r3)))
re_ = parse_dreq("t159_req_empty")
check("无满足条件时返回空且与手工一致", match_data(re_) == manual_data(re_) == [])
tx("submitDataMatch(%s)" % T_DREQ, [T_DREQ], [r1])   # 上链留痕
finish()
