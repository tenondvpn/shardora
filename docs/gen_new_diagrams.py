"""
Generate two new diagrams for the enhanced whitepaper:
  fig7_eth2_vs_shardora.jpg  — ETH 2.0 sharding collapse vs Shardora breakthrough
  fig8_progress.jpg          — Academic publications + engineering milestones timeline
"""
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch
import matplotlib.patheffects as pe
import numpy as np
import os

OUT   = os.path.dirname(os.path.abspath(__file__))
FONT  = 'Microsoft YaHei'
plt.rcParams['font.family'] = FONT
plt.rcParams['axes.unicode_minus'] = False
plt.rcParams['figure.dpi'] = 200

C_DARK   = '#1A1A2E'
C_NAVY   = '#16213E'
C_BLUE   = '#0F3460'
C_ACCENT = '#E94560'
C_GOLD   = '#F5A623'
C_TEAL   = '#00B4D8'
C_GREEN  = '#06D6A0'
C_PURPLE = '#7B2FBE'
C_LIGHT  = '#E0E0E0'
C_WHITE  = '#FFFFFF'
C_GRAY   = '#4A4A6A'
C_RED    = '#FF4444'
C_ORANGE = '#FF8C00'

def save(fig, name):
    path = os.path.join(OUT, name)
    fig.savefig(path, format='jpg', dpi=200, bbox_inches='tight',
                facecolor=fig.get_facecolor())
    plt.close(fig)
    print(f'Saved: {path}')

def rbox(ax, x, y, w, h, color, ec='none', lw=0, alpha=1.0, r=0.08, zorder=3, ls='-'):
    p = FancyBboxPatch((x - w/2, y - h/2), w, h,
                       boxstyle=f'round,pad=0,rounding_size={r}',
                       facecolor=color, edgecolor=ec, linewidth=lw,
                       alpha=alpha, zorder=zorder, linestyle=ls)
    ax.add_patch(p)
    return p

def txt(ax, x, y, s, size=10, color=C_WHITE, weight='normal',
        ha='center', va='center', zorder=6, style='normal'):
    return ax.text(x, y, s, fontsize=size, color=color, fontweight=weight,
                   ha=ha, va=va, zorder=zorder, style=style, fontfamily=FONT)

def arr(ax, x1, y1, x2, y2, color=C_TEAL, lw=2, style='->', rad=0, zorder=5):
    ax.annotate('', xy=(x2, y2), xytext=(x1, y1),
                arrowprops=dict(arrowstyle=style, color=color, lw=lw,
                                connectionstyle=f'arc3,rad={rad}'), zorder=zorder)


# ═══════════════════════════════════════════════════════════════════
# DIAGRAM 7 — ETH 2.0 Sharding Collapse vs Shardora Breakthrough
# ═══════════════════════════════════════════════════════════════════
def diagram_eth2():
    fig, ax = plt.subplots(figsize=(20, 13))
    fig.patch.set_facecolor(C_DARK)
    ax.set_facecolor(C_DARK)
    ax.set_xlim(0, 20); ax.set_ylim(0, 13)
    ax.axis('off')

    # ── Title ──
    txt(ax, 10, 12.55, 'Ethereum 2.0 执行分片的困境与 Shardora 的颠覆性突破',
        size=21, weight='bold')
    txt(ax, 10, 12.1, '从 ETH2 分片计划的七年演变，看 Shardora 如何逐一攻破五大不可能',
        size=12, color=C_TEAL)

    # ════════════════════════════════════════
    # LEFT HALF: ETH 2.0 failure story
    # ════════════════════════════════════════
    # Background panel
    rbox(ax, 4.8, 6.5, 9.2, 11.5, C_NAVY, ec=C_RED, lw=2, r=0.15, zorder=1)
    txt(ax, 4.8, 11.8, '以太坊 2.0 执行分片的七年困局', size=14,
        color=C_RED, weight='bold')

    # Timeline items
    timeline = [
        (10.8, C_ORANGE, '2018', '以太坊提出分片路线图',
         '目标：1024 个执行分片，每分片独立运行 EVM\n"区块链可扩展性三难困境的完美解法"',
         '✗ 技术挑战远超预期'),
        (9.35, C_ORANGE, '2020', '削减至 64 个分片',
         '执行分片数从 1024 缩减至 64\n信标链 Beacon Chain 上线，作为分片协调器',
         '✗ 仍未解决跨分片合约'),
        (7.9, C_RED, '2021', 'Vitalik 宣布"以 Rollup 为中心"路线图',
         '"执行分片太复杂，将重心转向 L2 Rollup"\n本质是承认执行分片在工程上难以实现',
         '✗ 彻底放弃执行分片'),
        (6.45, C_RED, '2022–2023', 'EIP-4844 Proto-Danksharding',
         '仅引入"数据分片"（blob 存储）\n不包含任何执行分片逻辑，仅为 Rollup 降低 DA 成本',
         '✗ 永久放弃执行分片'),
        (5.0, '#888888', '2024–至今', 'Danksharding 持续延期',
         '完整 Danksharding 最早 2026 年，执行分片已从\n以太坊官方路线图彻底消失',
         '→ 七年未解决，转而依赖中心化 Sequencer'),
    ]
    for (ty, tc, year, title, desc, verdict) in timeline:
        # Year badge
        rbox(ax, 1.1, ty, 1.2, 0.45, tc, alpha=0.9, r=0.07)
        txt(ax, 1.1, ty, year, size=10, weight='bold', color=C_DARK if tc != '#888888' else C_LIGHT)
        # Line
        ax.plot([1.7, 2.1], [ty, ty], color=tc, lw=2, zorder=4)
        # Content box
        rbox(ax, 5.5, ty, 6.8, 1.1, C_BLUE, ec=tc, lw=1.5, r=0.08, alpha=0.85)
        txt(ax, 5.5, ty + 0.28, title, size=11, weight='bold', color=C_WHITE)
        txt(ax, 5.5, ty - 0.05, desc, size=8.5, color=C_LIGHT, style='normal')
        # Verdict
        txt(ax, 5.5, ty - 0.38, verdict, size=9, color=tc, weight='bold')

    # Vertical timeline line
    ax.plot([1.7, 1.7], [4.3, 11.3], color=C_GRAY, lw=2.5, zorder=2)
    ax.scatter([1.7]*5, [10.8, 9.35, 7.9, 6.45, 5.0],
               c=[C_ORANGE, C_ORANGE, C_RED, C_RED, '#888888'],
               s=80, zorder=5)

    # Five unsolved problems box
    rbox(ax, 4.8, 2.8, 9.0, 3.4, '#1a0a0a', ec=C_RED, lw=2.5, r=0.12,
         alpha=0.95, zorder=2)
    txt(ax, 4.8, 4.3, 'ETH 2.0 执行分片五大未解难题', size=13,
        color=C_RED, weight='bold')
    problems = [
        ('P1', '跨分片合约原子执行',  '需要多轮消息传递，无法在单轮共识内完成，放弃实现'),
        ('P2', '委员会换届停机',     'DKG 需要 200 秒，换届窗口 TPS 归零，无工程解法'),
        ('P3', '单维分片无法线性扩展', '每分片单线程执行，TPS = N 分片 × 1 池，扩展性差'),
        ('P4', '同步跨分片消息',     '基于收据的异步消息，多跳路由延迟 > 10 秒'),
        ('P5', '跨分片 DeFi 安全性', 'Harmony Bridge 被盗 $1 亿，分片私钥门限设计缺陷'),
    ]
    for i, (tag, title_p, desc_p) in enumerate(problems):
        py = 3.85 - i * 0.7
        rbox(ax, 1.55, py, 0.7, 0.42, C_RED, alpha=0.85, r=0.06)
        txt(ax, 1.55, py, tag, size=9, weight='bold', color=C_WHITE)
        txt(ax, 5.4, py + 0.1, title_p, size=10, color=C_ACCENT, weight='bold')
        txt(ax, 5.4, py - 0.14, desc_p, size=8.5, color=C_LIGHT)

    # ════════════════════════════════════════
    # RIGHT HALF: Shardora breakthrough
    # ════════════════════════════════════════
    rbox(ax, 15.2, 6.5, 9.2, 11.5, C_NAVY, ec=C_GREEN, lw=2.5, r=0.15, zorder=1)
    txt(ax, 15.2, 11.8, 'Shardora 逐一突破', size=14, color=C_GREEN, weight='bold')

    breakthroughs = [
        (10.6, C_GREEN, 'B1', '跨分片合约：阿贝尔群 + CrossShardBase.sol',
         'Abel 群可交换性 → 无锁无信任转账\nFeistel 双射防伪造，共识层强制断言\nEVM 原生跨分片合约，已工程实现',
         '4,500–5,500 TPS\n100% 跨分片负载'),
        (9.1, C_TEAL,  'B2', '等待分片双委员会：零停机换届',
         '下届候选节点在等待分片并行预运行 DKG\n换届时刻新密钥已就绪 → 服务无中断\nDKG 密钥复用优化，开销降低 90%',
         'TPS 无损失\n0ms 换届停机'),
        (7.6, C_GOLD,  'B3', '二维并行分片：1024 × 32 执行单元',
         '维度一：最多 1024 个独立分片并行\n维度二：每分片 32 个独立执行池\nTotal TPS = N × 32 × T_pool，线性扩展',
         '理论峰值\n~1000 万 TPS'),
        (6.1, C_PURPLE,'B4', '异步跨分片消息：GBP 6000× 压缩',
         '阿贝尔群保证消息乱序到达最终一致\nGBP 聚合：O(S²P) → O(S²)，零停等\n三层重放保护，因果序严格保证',
         '3–5s 跨分片\nAMM 总延迟'),
        (4.6, C_ACCENT,'B5', '分布式密钥 + BLS：抗桥接攻击',
         't=⌈2n/3⌉ BFT 阈值，攻击需控制 >1/3 委员会\nFeistel 地址派生：无法伪造合法跨分片身份\n96 字节聚合签名，O(1) 验证',
         '1024 节点委员会\n< 2s BFT 确认'),
    ]
    for (by, bc, tag, title_b, desc_b, metric) in breakthroughs:
        # Tag badge
        rbox(ax, 11.05, by, 0.8, 0.5, bc, alpha=0.9, r=0.08)
        txt(ax, 11.05, by, tag, size=10, weight='bold', color=C_DARK)
        # Content box
        rbox(ax, 15.5, by, 7.6, 1.25, C_BLUE, ec=bc, lw=1.8, r=0.1, alpha=0.88)
        txt(ax, 15.5, by + 0.38, title_b, size=11, weight='bold', color=bc)
        txt(ax, 15.5, by + 0.05, desc_b, size=8.5, color=C_LIGHT)
        # Metric
        rbox(ax, 18.7, by - 0.37, 2.0, 0.42, bc, alpha=0.2, r=0.07)
        txt(ax, 18.7, by - 0.37, metric, size=9, color=bc, weight='bold')

    # ── Center divider: "vs" arrows ──
    ax.axvline(x=10.0, ymin=0.05, ymax=0.9, color=C_GRAY, lw=1.5,
               linestyle='--', alpha=0.5, zorder=1)
    rbox(ax, 10.0, 8.5, 1.1, 0.55, C_ACCENT, r=0.1)
    txt(ax, 10.0, 8.5, 'VS', size=15, weight='bold', color=C_WHITE)

    for (pr, by) in [('P1','B1'), ('P2','B2'), ('P3','B3'), ('P4','B4'), ('P5','B5')]:
        idx = ['P1','P2','P3','P4','P5'].index(pr)
        y_prob = [3.85, 3.15, 2.45, 1.75, 1.05][idx]
        y_sol  = [10.6, 9.1, 7.6, 6.1, 4.6][idx]
        arr(ax, 9.35, y_prob, 10.65, y_sol, color=C_GREEN, lw=1.5,
            rad=-0.15 + idx * 0.05, zorder=4)

    # ── Bottom summary ──
    rbox(ax, 10.0, 1.15, 19.4, 1.5, '#0a1a0a', ec=C_GREEN, lw=2, r=0.12)
    txt(ax, 10.0, 1.5,
        'Shardora 是全球首个在工程层面完整实现执行分片、跨分片 EVM 合约、零停机换届的公链系统',
        size=12, color=C_GREEN, weight='bold')
    txt(ax, 10.0, 1.1,
        '以太坊用七年证明执行分片"理论上可行、工程上极难"；Shardora 用 25 万行 C++ 代码证明它已经是现实',
        size=10.5, color=C_LIGHT)

    save(fig, 'fig7_eth2_vs_shardora.jpg')


# ═══════════════════════════════════════════════════════════════════
# DIAGRAM 8 — Academic Publications + Engineering Progress
# ═══════════════════════════════════════════════════════════════════
def diagram_progress():
    fig, ax = plt.subplots(figsize=(20, 12))
    fig.patch.set_facecolor(C_DARK)
    ax.set_facecolor(C_DARK)
    ax.set_xlim(0, 20); ax.set_ylim(0, 12)
    ax.axis('off')

    txt(ax, 10, 11.55, 'Shardora 当前进展：顶刊发表 · 严格评审 · 工程实现', size=21, weight='bold')
    txt(ax, 10, 11.1, '四篇顶级期刊/会议论文 · 多轮高水平同行评审 · 25 万行经过验证的生产级代码',
        size=12, color=C_TEAL)

    # ════════════════════════════════════════
    # TOP: Four publications as cards
    # ════════════════════════════════════════
    papers = [
        {
            'x': 2.6, 'color': C_GOLD,
            'venue': 'IEEE TNSE 2026',
            'rank': 'CCF-B · SCI Q1',
            'title': 'Shardora: Scaling Blockchain\nSharding via 2D Parallelism',
            'contrib': '二维并行分片 · Fast-HotStuff · BLS DKG · FTS 选举',
            'status': '已接收',
            'doi': 'DOI: 10.1109/TNSE.2026.3684813',
            'badge': 'ACCEPTED',
        },
        {
            'x': 7.4, 'color': C_TEAL,
            'venue': 'IEEE TDSC',
            'rank': 'CCF-A · SCI Q1',
            'title': 'Boosting Sharded Blockchain via\nMulti-Leader Parallel Pipelines',
            'contrib': '多 Leader 并行管道 · tVRF · GBP 协议 · EVS',
            'status': '二轮修改 (R2)',
            'doi': 'TDSC-2026-03-0984',
            'badge': 'UNDER REVIEW',
        },
        {
            'x': 12.4, 'color': C_PURPLE,
            'venue': 'SOSP 2026',
            'rank': 'CCF-A · 系统顶会',
            'title': 'A Runtime System for Service-Oriented\nSmart Contracts in Sharded Blockchains',
            'contrib': 'SCoRE 运行时 · 跨分片服务合约 · AMM 原子性',
            'status': '在审',
            'doi': 'SOSP 2026 Submission',
            'badge': 'SUBMITTED',
        },
        {
            'x': 17.4, 'color': C_GREEN,
            'venue': 'IEEE TIFS 2025',
            'rank': 'CCF-A · SCI Q1',
            'title': 'NMFT: NFT+AI Merkle Feature Tree\nfor Copyright Trading',
            'contrib': 'NFT 版权协议 · AI 特征树 · 区块链存证',
            'status': '已接收',
            'doi': 'IEEE TIFS 2025',
            'badge': 'ACCEPTED',
        },
    ]

    for p in papers:
        x, col = p['x'], p['color']
        # Main card
        rbox(ax, x, 8.75, 4.6, 4.0, C_NAVY, ec=col, lw=2.5, r=0.15)
        # Venue banner
        rbox(ax, x, 10.55, 4.6, 0.65, col, r=0.12, alpha=0.9)
        txt(ax, x, 10.72, p['venue'], size=13, weight='bold', color=C_DARK)
        txt(ax, x, 10.44, p['rank'], size=9, color=C_DARK, weight='bold')
        # Title
        txt(ax, x, 9.85, p['title'], size=10.5, weight='bold', color=C_WHITE)
        # Contrib
        txt(ax, x, 9.3, p['contrib'], size=8.5, color=C_LIGHT)
        # Status badge
        badge_col = C_GREEN if p['badge'] == 'ACCEPTED' else \
                    C_GOLD if p['badge'] == 'UNDER REVIEW' else C_TEAL
        rbox(ax, x, 8.7, 2.0, 0.38, badge_col, alpha=0.3, r=0.06)
        rbox(ax, x, 8.7, 2.0, 0.38, 'none', ec=badge_col, lw=1.5, r=0.06)
        txt(ax, x, 8.7, p['badge'], size=9, color=badge_col, weight='bold')
        # DOI
        txt(ax, x, 8.38, p['doi'], size=8, color=C_GRAY, style='italic')

    # ════════════════════════════════════════
    # MIDDLE: Peer Review Quality Evidence
    # ════════════════════════════════════════
    txt(ax, 10, 7.7, '同行评审深度：顶级审稿人的高水平质疑与代码级回应',
        size=14, color=C_GOLD, weight='bold')

    reviews = [
        ('TDSC R3-Q1', '跨池排序一致性', '审稿人要求形式化证明全局交易顺序\n→ 提供池内全序 + 跨池因果序严格证明\n→ 映射到 to_txs_pools.cc 高度单调性约束',
         C_TEAL, 3.2),
        ('TDSC R3-Q2', 'AMM 跨分片原子性', '审稿人质疑跨分片 AMM 是否需要补偿逻辑\n→ 提供三种部署场景（共置/并行/跨分片）\n→ 合约共置方案与以太坊原子性等价',
         C_PURPLE, 7.9),
        ('TDSC R3-Q3', 'GBP 瓶颈分析', '审稿人要求定量证明 GBP 非瓶颈\n→ 理论分析 O(S²P)→O(S²) 压缩\n→ tx_cli.cc 压力测试 4,500+ TPS 验证',
         C_GOLD, 12.6),
        ('TNSE 评审', 'DKG 安全性证明', 'DKG 正确性/保密性/可验证性形式化证明\n→ 提供 Feldman VSS 三条性质的严格数学证明\n→ 80% 节点参与门限的鲁棒性分析',
         C_GREEN, 17.0),
    ]
    for (tag, title_r, detail, col, rx) in reviews:
        rbox(ax, rx, 6.6, 4.5, 2.0, C_NAVY, ec=col, lw=1.8, r=0.1)
        rbox(ax, rx, 7.43, 4.5, 0.55, col, alpha=0.85, r=0.08)
        txt(ax, rx, 7.43, tag, size=10, weight='bold', color=C_DARK)
        txt(ax, rx, 7.05, title_r, size=11, weight='bold', color=col)
        txt(ax, rx, 6.56, detail, size=8.5, color=C_LIGHT)

    # ════════════════════════════════════════
    # BOTTOM: Engineering milestones
    # ════════════════════════════════════════
    txt(ax, 10, 5.35, '工程实现里程碑：已上线生产就绪的完整系统', size=14,
        color=C_ACCENT, weight='bold')

    milestones = [
        ('25 万行', 'C++ 生产代码', C_GOLD),
        ('702 个', '核心代码文件', C_TEAL),
        ('40,000', 'TPS（4分片实测）', C_GREEN),
        ('5,000+', '节点规模', C_PURPLE),
        ('4+1', '分片（当前在线）', C_ACCENT),
        ('<2s', 'BFT 确认延迟', C_TEAL),
    ]
    for i, (val, desc, col) in enumerate(milestones):
        mx = 1.8 + i * 3.1
        rbox(ax, mx, 4.25, 2.8, 1.55, C_NAVY, ec=col, lw=2, r=0.12)
        txt(ax, mx, 4.65, val, size=20, color=col, weight='bold')
        txt(ax, mx, 4.1, desc, size=10, color=C_LIGHT)

    eng_items = [
        ('✓ evmone EVM 全兼容（142文件）', C_GREEN),
        ('✓ BLS 门限签名 + 三阶段 DKG', C_GREEN),
        ('✓ CrossShardBase.sol 跨分片合约框架', C_GREEN),
        ('✓ EIP-1559 / EIP-155 交易格式', C_GREEN),
        ('✓ 后量子签名 ML-DSA-44（OQS）', C_TEAL),
        ('✓ 内嵌区块链浏览器（SQLite WAL）', C_TEAL),
        ('✓ 国密 SM2 签名算法支持', C_TEAL),
        ('✓ ClickHouse 链上数据分析集成', C_TEAL),
        ('✓ Feistel 双射地址派生（防伪造）', C_GOLD),
        ('✓ HostJournalStack 无损回滚 Gas', C_GOLD),
    ]
    for i, (item, col) in enumerate(eng_items):
        col_idx = i % 2
        ex = 1.8 + col_idx * 9.5 + (i // 2 % 1) * 0
        ey = 2.8 - (i // 2) * 0.48
        txt(ax, ex, ey, item, size=9.5, color=col, ha='left', weight='normal')

    # Engineering stats bar
    rbox(ax, 10, 0.65, 19.5, 0.9, '#0a0f1a', ec=C_GRAY, lw=1, r=0.08)
    txt(ax, 10, 0.85,
        '代码覆盖率：共识层单元测试 ✓  |  集成测试：tx_cli.cc + amm.py + shardora3.py  |  持续集成：多平台构建验证',
        size=9.5, color=C_LIGHT)
    txt(ax, 10, 0.5,
        'IEEE TNSE（CCF-B）+ IEEE TIFS（CCF-A）已接收  ·  IEEE TDSC（CCF-A）二轮修改  ·  SOSP 2026（CCF-A 系统顶会）在审',
        size=9.5, color=C_GOLD)

    save(fig, 'fig8_progress.jpg')


if __name__ == '__main__':
    print('Generating new diagrams...')
    diagram_eth2()
    diagram_progress()
    print('Done!')
