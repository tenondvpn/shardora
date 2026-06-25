#!/usr/bin/env python3
"""Parse BatchQueryAccounts detail lines; report prefund addrs with last nonce < target."""

import re
import sys
from collections import defaultdict

LINE_RE = re.compile(
    r"\[(?P<ip>[\d.]+)\]\s+(?P<ts>[\d\- :.]+)\s+.*?"
    r"batch_query_accounts detail: addr=(?P<addr>[0-9a-f]+)\s+"
    r"nonce=(?P<nonce>\d+)\s+balance=(?P<balance>\d+)\s+pool_index=(?P<pool>\d+)"
)

def parse(path: str, target_nonce: int = 20):
    # Per addr: keep last seen (by file order / timestamp)
    last = {}  # addr -> dict
    order = []  # first-seen order for stable output

    with open(path, "r", errors="replace") as f:
        for line in f:
            m = LINE_RE.search(line)
            if not m:
                continue
            d = m.groupdict()
            addr = d["addr"]
            if addr not in last:
                order.append(addr)
            last[addr] = {
                "ip": d["ip"],
                "ts": d["ts"].strip(),
                "nonce": int(d["nonce"]),
                "balance": int(d["balance"]),
                "pool": int(d["pool"]),
            }

    stuck = [a for a in order if last[a]["nonce"] < target_nonce]
    partial = [a for a in order if 0 < last[a]["nonce"] < target_nonce]
    zero = [a for a in order if last[a]["nonce"] == 0]

    print(f"Total unique addrs: {len(last)}")
    print(f"Last nonce < {target_nonce}: {len(stuck)}")
    print(f"  nonce=0: {len(zero)}")
    print(f"  0 < nonce < {target_nonce}: {len(partial)}")
    print()
    print(f"{'nonce':>5}  {'pool':>4}  {'balance':>12}  {'node':>15}  {'last_ts':<26}  addr")
    print("-" * 120)
    for addr in sorted(stuck, key=lambda a: (last[a]["nonce"], last[a]["pool"], a)):
        r = last[addr]
        print(
            f"{r['nonce']:>5}  {r['pool']:>4}  {r['balance']:>12}  {r['ip']:>15}  {r['ts']:<26}  {addr}"
        )

    return stuck, last


if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/batch_query_logs.txt"
    target = int(sys.argv[2]) if len(sys.argv) > 2 else 20
    parse(path, target)
