"""
Shardora Whitepaper Diagram Generator
Generates all JPG diagrams for the Chinese whitepaper.
"""
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import matplotlib.patches as patches
from matplotlib.patches import FancyArrowPatch, FancyBboxPatch
from matplotlib.lines import Line2D
import matplotlib.patheffects as pe
import numpy as np
import os

OUT = os.path.dirname(os.path.abspath(__file__))
FONT = 'Microsoft YaHei'

plt.rcParams['font.family'] = FONT
plt.rcParams['axes.unicode_minus'] = False
plt.rcParams['figure.dpi'] = 200

# ─────────────────────────────────────────────
# Colour palette
# ─────────────────────────────────────────────
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

def save(fig, name):
    path = os.path.join(OUT, name)
    fig.savefig(path, format='jpg', dpi=200, bbox_inches='tight',
                facecolor=fig.get_facecolor())
    plt.close(fig)
    print(f'Saved: {path}')
    return path

def rounded_box(ax, x, y, w, h, color, alpha=1.0, radius=0.04, zorder=3):
    fancy = FancyBboxPatch((x - w/2, y - h/2), w, h,
                           boxstyle=f'round,pad=0,rounding_size={radius}',
                           facecolor=color, edgecolor='none',
                           alpha=alpha, zorder=zorder)
    ax.add_patch(fancy)
    return fancy

def arrow(ax, x1, y1, x2, y2, color=C_TEAL, lw=2, style='->', zorder=4):
    ax.annotate('', xy=(x2, y2), xytext=(x1, y1),
                arrowprops=dict(arrowstyle=style, color=color,
                                lw=lw, connectionstyle='arc3,rad=0'),
                zorder=zorder)

def label(ax, x, y, text, size=11, color=C_WHITE, weight='normal',
          ha='center', va='center', zorder=5):
    ax.text(x, y, text, fontsize=size, color=color, fontweight=weight,
            ha=ha, va=va, zorder=zorder,
            fontfamily=FONT)

# ═══════════════════════════════════════════════════════════════════
# DIAGRAM 1 – System Architecture Overview
# ═══════════════════════════════════════════════════════════════════
def diagram_architecture():
    fig, ax = plt.subplots(figsize=(18, 13))
    fig.patch.set_facecolor(C_DARK)
    ax.set_facecolor(C_DARK)
    ax.set_xlim(0, 18); ax.set_ylim(0, 13)
    ax.axis('off')

    # Title
    ax.text(9, 12.4, 'Shardora 系统架构总览', fontsize=22, color=C_WHITE,
            fontweight='bold', ha='center', va='center', fontfamily=FONT)
    ax.text(9, 12.0, '三层网络 · 二维并行分片 · 零停机换届', fontsize=13,
            color=C_TEAL, ha='center', va='center', fontfamily=FONT)

    # ── Layer 1: Universal Network ──
    fancy = FancyBboxPatch((0.3, 10.2), 17.4, 1.4,
                           boxstyle='round,pad=0,rounding_size=0.15',
                           facecolor=C_BLUE, edgecolor=C_TEAL, linewidth=2,
                           alpha=0.85, zorder=2)
    ax.add_patch(fancy)
    ax.text(2.0, 10.9, '通用网络  Universal Network', fontsize=14,
            color=C_WHITE, fontweight='bold', va='center', fontfamily=FONT)
    ax.text(2.0, 10.55, 'Kademlia DHT 覆盖网络 · 全节点发现与路由 · TCP/UDP 混合传输',
            fontsize=10, color=C_LIGHT, va='center', fontfamily=FONT)
    # icon dots
    for xi in [13.5, 14.5, 15.5, 16.5]:
        circle = plt.Circle((xi, 10.9), 0.22, color=C_TEAL, zorder=4)
        ax.add_patch(circle)
    ax.annotate('', xy=(15.0, 10.9), xytext=(14.0, 10.9),
                arrowprops=dict(arrowstyle='<->', color=C_GOLD, lw=1.5), zorder=5)
    ax.annotate('', xy=(16.0, 10.9), xytext=(15.5, 10.9),
                arrowprops=dict(arrowstyle='<->', color=C_GOLD, lw=1.5), zorder=5)

    # Arrow down
    ax.annotate('', xy=(9, 9.9), xytext=(9, 10.2),
                arrowprops=dict(arrowstyle='->', color=C_TEAL, lw=2.5), zorder=5)

    # ── Layer 2: Root Congress ──
    fancy2 = FancyBboxPatch((3.5, 8.6), 11, 1.2,
                            boxstyle='round,pad=0,rounding_size=0.15',
                            facecolor=C_PURPLE, edgecolor=C_GOLD, linewidth=2.5,
                            alpha=0.9, zorder=2)
    ax.add_patch(fancy2)
    ax.text(9, 9.35, '根国会  Root Congress  (网络 ID = 2)', fontsize=14,
            color=C_WHITE, fontweight='bold', ha='center', va='center', fontfamily=FONT)
    # sub items
    items_rc = ['全局委员会选举 (FTS)', '分片创建 / 销毁', '时间块 Epoch 同步', 'VSS 随机数生成']
    for i, txt in enumerate(items_rc):
        xi = 4.8 + i * 2.7
        rounded_box(ax, xi, 8.85, 2.4, 0.36, C_NAVY, radius=0.06)
        ax.text(xi, 8.85, txt, fontsize=9, color=C_TEAL,
                ha='center', va='center', fontfamily=FONT)

    # Arrows down to shards
    for xi in [3.0, 7.0, 11.0, 15.0]:
        ax.annotate('', xy=(xi, 7.9), xytext=(xi, 8.6),
                    arrowprops=dict(arrowstyle='->', color=C_GOLD, lw=1.8), zorder=5)

    # ── Layer 3: Consensus Shards ──
    shard_colors = [C_BLUE, '#1a4a6e', '#0d3b5c', '#1e4d7a']
    shard_titles = ['共识分片 #3', '共识分片 #4', '···', '共识分片 #1026']
    xs = [3.0, 7.0, 11.0, 15.0]
    for i, (xi, col, title) in enumerate(zip(xs, shard_colors, shard_titles)):
        # Main shard box
        fancy_s = FancyBboxPatch((xi - 2.2, 5.2), 4.4, 2.6,
                                 boxstyle='round,pad=0,rounding_size=0.12',
                                 facecolor=col, edgecolor=C_TEAL, linewidth=1.5,
                                 alpha=0.9, zorder=2)
        ax.add_patch(fancy_s)
        ax.text(xi, 7.6, title, fontsize=12, color=C_WHITE, fontweight='bold',
                ha='center', va='center', fontfamily=FONT)
        # 32 pools grid (4x2 mini boxes)
        ax.text(xi, 7.2, '32 个并行执行池', fontsize=9, color=C_TEAL,
                ha='center', va='center', fontfamily=FONT)
        cols_p = ['#00B4D8', '#06D6A0', '#F5A623', '#E94560',
                  '#7B2FBE', '#00B4D8', '#06D6A0', '#F5A623']
        if title != '···':
            for j in range(8):
                px = xi - 1.7 + (j % 4) * 0.9
                py = 5.85 - (j // 4) * 0.52
                rect = FancyBboxPatch((px - 0.38, py - 0.18), 0.76, 0.36,
                                      boxstyle='round,pad=0,rounding_size=0.04',
                                      facecolor=cols_p[j], alpha=0.7, zorder=4)
                ax.add_patch(rect)
                ax.text(px, py, f'P{j+1}', fontsize=7, color=C_WHITE,
                        ha='center', va='center', fontfamily=FONT)
        else:
            ax.text(xi, 6.2, '···', fontsize=28, color=C_LIGHT,
                    ha='center', va='center', fontfamily=FONT)

        # Waiting shard below
        fancy_w = FancyBboxPatch((xi - 2.0, 4.3), 4.0, 0.75,
                                 boxstyle='round,pad=0,rounding_size=0.08',
                                 facecolor=C_GRAY, edgecolor=C_GOLD,
                                 linestyle='--', linewidth=1.5,
                                 alpha=0.85, zorder=2)
        ax.add_patch(fancy_w)
        ax.text(xi, 4.67, '等待分片 (DKG 预执行)', fontsize=9,
                color=C_GOLD, ha='center', va='center', fontfamily=FONT)
        ax.annotate('', xy=(xi, 5.2), xytext=(xi, 5.05),
                    arrowprops=dict(arrowstyle='<->', color=C_GOLD, lw=1.5,
                                   linestyle='dashed'), zorder=5)

    # Cross-shard GBP arrows
    ax.annotate('', xy=(6.85, 6.4), xytext=(5.2, 6.4),
                arrowprops=dict(arrowstyle='<->', color=C_ACCENT, lw=2,
                                connectionstyle='arc3,rad=0.25'), zorder=6)
    ax.text(6.0, 6.9, 'GBP\n跨分片', fontsize=8, color=C_ACCENT,
            ha='center', va='center', fontfamily=FONT)

    # ── Bottom legend ──
    legend_items = [
        (C_TEAL, '执行池 Pool'),
        (C_GOLD, '跨 Epoch 换届'),
        (C_ACCENT, 'GBP 跨分片消息'),
        (C_PURPLE, '根国会'),
    ]
    for i, (color, txt) in enumerate(legend_items):
        lx = 1.5 + i * 4.0
        rect = FancyBboxPatch((lx - 0.2, 3.45), 0.4, 0.25,
                              boxstyle='round,pad=0,rounding_size=0.04',
                              facecolor=color, zorder=4)
        ax.add_patch(rect)
        ax.text(lx + 0.4, 3.57, txt, fontsize=10, color=C_LIGHT,
                va='center', fontfamily=FONT)

    # Stats bar
    stats = [('1024', '最大分片数'), ('32', '每分片并行池'), ('1024', '委员会规模/分片'),
             ('1000万', '理论峰值 TPS'), ('< 2s', 'BFT 确认延迟')]
    for i, (val, desc) in enumerate(stats):
        sx = 1.6 + i * 3.4
        rounded_box(ax, sx, 2.3, 3.0, 0.9, C_NAVY, radius=0.1)
        ax.text(sx, 2.6, val, fontsize=18, color=C_GOLD, fontweight='bold',
                ha='center', va='center', fontfamily=FONT)
        ax.text(sx, 2.1, desc, fontsize=9, color=C_LIGHT,
                ha='center', va='center', fontfamily=FONT)

    ax.text(9, 1.5, '总吞吐量 = 分片数(N) × 32池 × 每池吞吐  →  随分片数线性扩展，无固有上限',
            fontsize=11, color=C_TEAL, ha='center', va='center',
            style='italic', fontfamily=FONT)

    save(fig, 'fig1_architecture.jpg')


# ═══════════════════════════════════════════════════════════════════
# DIAGRAM 2 – Key Technology Breakthrough Flow
# ═══════════════════════════════════════════════════════════════════
def diagram_tech_flow():
    fig, ax = plt.subplots(figsize=(20, 11))
    fig.patch.set_facecolor(C_DARK)
    ax.set_facecolor(C_DARK)
    ax.set_xlim(0, 20); ax.set_ylim(0, 11)
    ax.axis('off')

    ax.text(10, 10.5, 'Shardora 关键技术突破路径', fontsize=22, color=C_WHITE,
            fontweight='bold', ha='center', fontfamily=FONT)
    ax.text(10, 10.0, '五层技术协同突破区块链不可能三角', fontsize=13,
            color=C_TEAL, ha='center', fontfamily=FONT)

    # Five technology layers as horizontal rows
    layers = [
        # (y, color, layer_name, title, subtitle, metrics)
        (8.8, C_ACCENT,  '密码学层',
         'BLS 门限签名  O(n²) → O(1) 聚合验证',
         '• alt_bn128 曲线  • 96字节聚合签名  • 1024节点委员会单次配对验证',
         '96 bytes / 验证'),
        (7.2, C_PURPLE,  '共识层',
         'Fast-HotStuff + EVS  千人委员会 < 2s 确认',
         '• 两阶段流水线提交  • 增强视图同步  • Nonce 连续性防双花',
         '< 2s / 1024节点'),
        (5.6, C_TEAL,    '工程层',
         '等待分片双委员会  换届零停机',
         '• 并行 DKG（200s）  • 密钥复用优化  • 换届同步开销降低 90%',
         '0ms 停机'),
        (4.0, C_GREEN,   '数学层',
         '阿贝尔群 + Feistel 双射  无锁无信任跨分片',
         '• 可交换余额更新  • 160-bit 置换网络  • 共识层强制地址断言',
         '6000× 消息压缩'),
        (2.4, C_GOLD,    '经济层',
         'FTS 四维权重 + 地理分散激励',
         '• PoS × 信用 × 区域 × 任期反向  • VSS 随机数防预测  • 分代分片差异激励',
         '32 领导者/分片'),
    ]

    for y, color, lname, title, sub, metric in layers:
        # Layer tag
        rounded_box(ax, 1.2, y, 1.8, 0.85, color, radius=0.1)
        ax.text(1.2, y, lname, fontsize=12, color=C_WHITE, fontweight='bold',
                ha='center', va='center', fontfamily=FONT)

        # Main content box
        fancy = FancyBboxPatch((2.2, y - 0.5), 14.5, 1.0,
                               boxstyle='round,pad=0,rounding_size=0.1',
                               facecolor=C_NAVY, edgecolor=color, linewidth=2,
                               alpha=0.9, zorder=2)
        ax.add_patch(fancy)
        ax.text(2.5, y + 0.15, title, fontsize=12.5, color=C_WHITE,
                fontweight='bold', va='center', fontfamily=FONT)
        ax.text(2.5, y - 0.2, sub, fontsize=9.5, color=C_LIGHT,
                va='center', fontfamily=FONT)

        # Metric badge
        rounded_box(ax, 18.0, y, 2.8, 0.55, color, alpha=0.3, radius=0.1)
        ax.text(18.0, y, metric, fontsize=10, color=color, fontweight='bold',
                ha='center', va='center', fontfamily=FONT)

        # Vertical connector arrow (except last)
        if y > 2.4:
            ax.annotate('', xy=(1.2, y - 0.43), xytext=(1.2, y - 0.9 + 0.43),
                        arrowprops=dict(arrowstyle='->', color=color, lw=2.5), zorder=5)

    # Problem → Solution flow on right side
    ax.text(19.0, 10.0, '解决\n的矛盾', fontsize=10, color=C_LIGHT,
            ha='center', va='center', fontfamily=FONT)

    problems = [
        '大委员会\nvs\n高性能',
        '节点规模\nvs\n确认延迟',
        '安全换届\nvs\n持续可用',
        '跨分片\n安全 vs\n吞吐量',
        '充分激励\nvs\n去中心化',
    ]
    ys = [8.8, 7.2, 5.6, 4.0, 2.4]
    for y, prob in zip(ys, problems):
        rounded_box(ax, 19.3, y, 1.2, 0.85, C_GRAY, radius=0.08)
        ax.text(19.3, y, prob, fontsize=8, color=C_LIGHT,
                ha='center', va='center', fontfamily=FONT)

    # Bottom: trilemma resolution
    trilemma = [
        ('去中心化', '9.0/10', C_GREEN),
        ('安全性', '9.5/10', C_TEAL),
        ('可扩展性', '10/10', C_GOLD),
    ]
    ax.text(10, 1.35, '不可能三角综合得分', fontsize=12, color=C_LIGHT,
            ha='center', va='center', fontfamily=FONT)
    for i, (dim, score, col) in enumerate(trilemma):
        bx = 7.0 + i * 3.0
        rounded_box(ax, bx, 0.7, 2.6, 0.65, col, alpha=0.25, radius=0.1)
        ax.text(bx - 0.3, 0.7, dim, fontsize=12, color=col, fontweight='bold',
                ha='center', va='center', fontfamily=FONT)
        ax.text(bx + 0.75, 0.7, score, fontsize=14, color=col, fontweight='bold',
                ha='center', va='center', fontfamily=FONT)

    save(fig, 'fig2_tech_flow.jpg')


# ═══════════════════════════════════════════════════════════════════
# DIAGRAM 3 – Cross-Shard Transfer Protocol
# ═══════════════════════════════════════════════════════════════════
def diagram_cross_shard():
    fig, ax = plt.subplots(figsize=(18, 12))
    fig.patch.set_facecolor(C_DARK)
    ax.set_facecolor(C_DARK)
    ax.set_xlim(0, 18); ax.set_ylim(0, 12)
    ax.axis('off')

    ax.text(9, 11.5, 'Shardora 跨分片资产转移协议', fontsize=22,
            color=C_WHITE, fontweight='bold', ha='center', fontfamily=FONT)
    ax.text(9, 11.0, '阿贝尔群无锁 · Feistel 双射防伪造 · Gas 确定性预扣', fontsize=13,
            color=C_TEAL, ha='center', fontfamily=FONT)

    # ── Four-layer architecture ──
    layer_data = [
        (9.2, 1.4, C_ACCENT,  '价值层  Value Plane',
         '_crossTransfer',
         '阿贝尔群可交换性    T_Δ1(T_Δ2(B)) = T_Δ2(T_Δ1(B)) = B + Δ1 + Δ2',
         '无锁 · 强最终一致性 SEC · 彻底消除 2PC 死锁'),
        (9.2, 0.9, C_PURPLE,  '控制层  Control Plane',
         '_crossSetStorage',
         '幂等全量覆盖    LWW 版本栅栏    防陈旧覆盖',
         '写操作幂等 · 版本号单调递增 · 重放安全'),
        (9.2, 0.9, C_TEAL,    '计算层  Compute Plane',
         'AMM / 订单簿',
         '100% 封闭在单分片池内    非可交换算子不跨越分片边界',
         '共置策略 → 天然原子 · 单轮共识完成'),
        (9.2, 0.9, C_GREEN,   '宿主执行层  Host Execution',
         'C++ evmone Host',
         'Gas 编译期常量定价    HostJournalStack 无损回滚',
         '100% 确定性零 Revert 终局 · 子调用REVERT全额退还'),
    ]
    y_start = 9.6
    for (cx, h, color, name, api, desc1, desc2) in layer_data:
        fancy = FancyBboxPatch((0.4, y_start - h), 17.2, h - 0.06,
                               boxstyle='round,pad=0,rounding_size=0.1',
                               facecolor=C_NAVY, edgecolor=color, linewidth=2.5,
                               alpha=0.9, zorder=2)
        ax.add_patch(fancy)
        # tag
        rounded_box(ax, 2.1, y_start - h/2, 2.6, h * 0.65, color,
                    alpha=0.85, radius=0.08)
        ax.text(2.1, y_start - h/2 + 0.08, name.split('  ')[0],
                fontsize=10, color=C_WHITE, fontweight='bold',
                ha='center', va='center', fontfamily=FONT)
        ax.text(2.1, y_start - h/2 - 0.13, name.split('  ')[1],
                fontsize=8, color=C_LIGHT,
                ha='center', va='center', fontfamily=FONT)
        # api badge
        rounded_box(ax, 5.5, y_start - h/2, 2.2, 0.32, color, alpha=0.25, radius=0.06)
        ax.text(5.5, y_start - h/2, api, fontsize=10, color=color,
                fontweight='bold', ha='center', va='center', fontfamily=FONT)
        ax.text(3.7, y_start - h/2 + 0.15, desc1, fontsize=9, color=C_LIGHT,
                va='center', fontfamily=FONT)
        ax.text(3.7, y_start - h/2 - 0.15, desc2, fontsize=8.5, color=C_TEAL,
                va='center', fontfamily=FONT)
        y_start -= h

    # ── Feistel Network diagram ──
    ax.text(2.2, 5.25, 'Feistel 双射地址派生', fontsize=14, color=C_GOLD,
            fontweight='bold', ha='center', fontfamily=FONT)

    # Input
    rounded_box(ax, 2.2, 4.7, 3.0, 0.5, C_BLUE, radius=0.08)
    ax.text(2.2, 4.7, 'Base Address (160-bit)', fontsize=10, color=C_WHITE,
            ha='center', va='center', fontfamily=FONT)
    ax.text(0.8, 4.7, 'L₀', fontsize=14, color=C_TEAL, ha='center', fontfamily=FONT)
    ax.text(3.6, 4.7, 'R₀', fontsize=14, color=C_GOLD, ha='center', fontfamily=FONT)

    # 4 rounds
    round_ys = [4.05, 3.4, 2.75, 2.1]
    for i, ry in enumerate(round_ys):
        rounded_box(ax, 2.2, ry, 3.6, 0.45, C_GRAY, radius=0.06)
        ax.text(2.2, ry + 0.08, f'第 {i+1} 轮  Li = R(i-1)', fontsize=9,
                color=C_LIGHT, ha='center', va='center', fontfamily=FONT)
        ax.text(2.2, ry - 0.1, f'Ri = L(i-1) XOR F(R(i-1), K{i+1})', fontsize=8.5,
                color=C_TEAL, ha='center', va='center', fontfamily=FONT)
        ax.annotate('', xy=(2.2, ry + 0.23), xytext=(2.2, ry - 0.23 + 0.45),
                    arrowprops=dict(arrowstyle='->', color=C_GOLD, lw=1.5), zorder=5)

    # Output
    rounded_box(ax, 2.2, 1.5, 3.2, 0.5, C_ACCENT, radius=0.08)
    ax.text(2.2, 1.58, 'Shadow Address (160-bit)', fontsize=10, color=C_WHITE,
            fontweight='bold', ha='center', va='center', fontfamily=FONT)
    ax.text(2.2, 1.35, '(shard=0,pool=0) → 恒等映射 = 以太坊地址',
            fontsize=8.5, color=C_GOLD, ha='center', va='center', fontfamily=FONT)

    ax.annotate('', xy=(2.2, 1.77), xytext=(2.2, 1.85),
                arrowprops=dict(arrowstyle='->', color=C_ACCENT, lw=2), zorder=5)

    # Round key description
    ax.text(4.7, 3.7, '轮密钥:', fontsize=9, color=C_LIGHT, fontfamily=FONT)
    ax.text(4.7, 3.4, 'Kᵢ = keccak256(\n  "AKAVERSE_FEISTEL_V1"\n  || shard || pool || i)',
            fontsize=8.5, color=C_TEAL, fontfamily=FONT)

    # ── GBP message aggregation diagram ──
    ax.text(10.5, 5.25, 'GBP 消息聚合 (6000× 压缩)', fontsize=14,
            color=C_GOLD, fontweight='bold', ha='center', fontfamily=FONT)

    # Source shard
    rounded_box(ax, 8.0, 4.4, 2.6, 1.2, C_BLUE, radius=0.1)
    ax.text(8.0, 4.7, '源分片 A', fontsize=12, color=C_WHITE,
            fontweight='bold', ha='center', fontfamily=FONT)
    ax.text(8.0, 4.35, 'kNormalFrom × N', fontsize=9.5, color=C_TEAL,
            ha='center', fontfamily=FONT)
    ax.text(8.0, 4.05, 'N 条独立跨分片消息', fontsize=9, color=C_LIGHT,
            ha='center', fontfamily=FONT)

    # GBP aggregator
    rounded_box(ax, 10.5, 4.4, 2.4, 1.1, C_ACCENT, radius=0.1)
    ax.text(10.5, 4.65, 'GBP 聚合器', fontsize=12, color=C_WHITE,
            fontweight='bold', ha='center', fontfamily=FONT)
    ax.text(10.5, 4.35, '↓  唯一哈希锚定', fontsize=9.5, color=C_LIGHT,
            ha='center', fontfamily=FONT)
    ax.text(10.5, 4.1, '↓  高度单调性约束', fontsize=9, color=C_LIGHT,
            ha='center', fontfamily=FONT)

    # Target shard
    rounded_box(ax, 13.0, 4.4, 2.6, 1.2, C_GREEN, radius=0.1)
    ax.text(13.0, 4.7, '目标分片 B', fontsize=12, color=C_WHITE,
            fontweight='bold', ha='center', fontfamily=FONT)
    ax.text(13.0, 4.35, 'kNormalTo × 1', fontsize=9.5, color=C_DARK,
            fontweight='bold', ha='center', fontfamily=FONT)
    ax.text(13.0, 4.05, '1 条聚合消息', fontsize=9, color=C_DARK,
            ha='center', fontfamily=FONT)

    ax.annotate('', xy=(9.3, 4.4), xytext=(9.2, 4.4),
                arrowprops=dict(arrowstyle='->', color=C_TEAL, lw=2.5), zorder=5)
    ax.annotate('', xy=(11.73, 4.4), xytext=(11.7, 4.4),
                arrowprops=dict(arrowstyle='->', color=C_GREEN, lw=2.5), zorder=5)

    ax.text(10.5, 3.4, '压缩比  O(S²·P) → O(S²)  ≈ 6,000×', fontsize=11,
            color=C_GOLD, ha='center', fontfamily=FONT)
    ax.text(10.5, 3.1, '三层重放保护：唯一哈希 · 数据库存在性检查 · 高度单调性', fontsize=10,
            color=C_TEAL, ha='center', fontfamily=FONT)

    # ── CrossShardBase security ──
    ax.text(10.5, 2.6, 'CrossShardBase.sol  零铸造分身合约安全模型', fontsize=12,
            color=C_GREEN, fontweight='bold', ha='center', fontfamily=FONT)
    rounded_box(ax, 10.5, 1.85, 9.0, 1.2, C_NAVY, radius=0.12)
    ax.text(10.5, 2.25,
            'if (shard,pool) ≠ (0,0) → totalSupply = 0', fontsize=11,
            color=C_ACCENT, fontweight='bold', ha='center', fontfamily=FONT)
    ax.text(10.5, 1.95,
            '无外部 mint()  ·  唯一资金来源: systemExecuteCrossTransfer (仅共识层调用)',
            fontsize=10, color=C_TEAL, ha='center', fontfamily=FONT)
    ax.text(10.5, 1.65,
            '攻击者即使控制私钥  →  无法在分身合约上凭空增发  →  数学层消灭攻击面',
            fontsize=10, color=C_LIGHT, ha='center', fontfamily=FONT)

    # Bottom gas info
    rounded_box(ax, 4.5, 0.85, 3.8, 0.65, C_GRAY, radius=0.08)
    ax.text(4.5, 0.95, '跨分片转账  固定 30,000 Gas', fontsize=10,
            color=C_GOLD, ha='center', va='center', fontfamily=FONT)
    ax.text(4.5, 0.7, 'REVERT → 快照栈无损回滚，Gas 完整退还', fontsize=9,
            color=C_LIGHT, ha='center', va='center', fontfamily=FONT)
    rounded_box(ax, 9.5, 0.85, 4.2, 0.65, C_GRAY, radius=0.08)
    ax.text(9.5, 0.95, '跨分片存储  25,000 + ⌈L/32⌉×20,000 Gas', fontsize=10,
            color=C_TEAL, ha='center', va='center', fontfamily=FONT)
    ax.text(9.5, 0.7, '编译期常量定价，零不确定性', fontsize=9,
            color=C_LIGHT, ha='center', va='center', fontfamily=FONT)

    save(fig, 'fig3_cross_shard.jpg')


# ═══════════════════════════════════════════════════════════════════
# DIAGRAM 4 – Economic Model
# ═══════════════════════════════════════════════════════════════════
def diagram_economic():
    fig = plt.figure(figsize=(18, 11))
    fig.patch.set_facecolor(C_DARK)

    # ── Left: halving curve ──
    ax1 = fig.add_axes([0.04, 0.18, 0.42, 0.70])
    ax1.set_facecolor(C_NAVY)
    for spine in ax1.spines.values():
        spine.set_color(C_GRAY)
    ax1.tick_params(colors=C_LIGHT, labelsize=9)
    ax1.yaxis.label.set_color(C_LIGHT)
    ax1.xaxis.label.set_color(C_LIGHT)

    epochs = np.linspace(0, 210240 * 6, 2000)
    rewards = 10000 / (2 ** (epochs / 210240))
    rewards = np.maximum(rewards, 1)
    ax1.fill_between(epochs / 210240, rewards, alpha=0.25, color=C_GOLD)
    ax1.plot(epochs / 210240, rewards, color=C_GOLD, lw=2.5, label='基础 Epoch 奖励')
    early_bonus = np.where(epochs / 210240 < 1, rewards * 1.1, rewards)
    ax1.plot(epochs / 210240, early_bonus, color=C_ACCENT, lw=1.8,
             linestyle='--', label='早期加成 (+10%)', alpha=0.8)

    for h, label_text in [(1, '第1次减半\n≈4年'), (2, '第2次减半\n≈8年'),
                          (3, '第3次减半\n≈12年')]:
        ax1.axvline(x=h, color=C_TEAL, lw=1.2, linestyle=':', alpha=0.7)
        ax1.text(h + 0.05, 8000, label_text, fontsize=8, color=C_TEAL,
                 va='top', fontfamily=FONT)

    ax1.set_xlim(0, 6); ax1.set_ylim(0, 11500)
    ax1.set_xlabel('减半周期数（1周期≈4年）', fontsize=10, fontfamily=FONT)
    ax1.set_ylabel('Epoch 奖励（SHARDORA）', fontsize=10, fontfamily=FONT)
    ax1.set_title('代币发行曲线（比特币式减半）', fontsize=13, color=C_WHITE,
                  pad=10, fontfamily=FONT)
    ax1.legend(fontsize=9, facecolor=C_DARK, labelcolor=C_LIGHT,
               prop={'family': FONT})
    ax1.grid(axis='y', color=C_GRAY, alpha=0.3, lw=0.8)

    # annotations
    ax1.annotate('初始奖励\n10,000 SHD/Epoch', xy=(0, 10000), xytext=(0.6, 9500),
                 fontsize=9, color=C_GOLD, fontfamily=FONT,
                 arrowprops=dict(arrowstyle='->', color=C_GOLD, lw=1.2))
    ax1.annotate('最低奖励\n1 SHD', xy=(4.5, 1), xytext=(3.5, 1200),
                 fontsize=9, color=C_LIGHT, fontfamily=FONT,
                 arrowprops=dict(arrowstyle='->', color=C_LIGHT, lw=1.2))

    # ── Right top: shard weight bar chart ──
    ax2 = fig.add_axes([0.52, 0.55, 0.44, 0.35])
    ax2.set_facecolor(C_NAVY)
    for spine in ax2.spines.values():
        spine.set_color(C_GRAY)
    ax2.tick_params(colors=C_LIGHT, labelsize=9)

    gens = ['Gen0\n(3-5)', 'Gen1\n(6-10)', 'Gen2\n(11-18)',
            'Gen3\n(19-34)', 'Gen4', 'Gen5', 'Gen6', 'Gen7\n(≥515)']
    weights = [1.000, 0.900, 0.810, 0.729, 0.656, 0.590, 0.531, 0.430]
    colors_bar = [C_GOLD, C_ACCENT, C_PURPLE, C_TEAL, C_GREEN,
                  '#4895ef', '#4361ee', C_GRAY]
    bars = ax2.bar(gens, weights, color=colors_bar, edgecolor='none', width=0.65)
    for bar, w in zip(bars, weights):
        ax2.text(bar.get_x() + bar.get_width()/2, w + 0.02,
                 f'{w:.3f}', fontsize=8, color=C_LIGHT,
                 ha='center', va='bottom', fontfamily=FONT)
    ax2.set_ylim(0, 1.15)
    ax2.set_ylabel('奖励权重系数', fontsize=10, fontfamily=FONT,
                   color=C_LIGHT)
    ax2.set_title('分代分片差异激励权重', fontsize=12, color=C_WHITE,
                  pad=8, fontfamily=FONT)
    ax2.grid(axis='y', color=C_GRAY, alpha=0.3, lw=0.8)

    # ── Right bottom: FTS weight radar ──
    ax3 = fig.add_axes([0.52, 0.05, 0.44, 0.44], polar=True)
    ax3.set_facecolor(C_NAVY)
    categories = ['PoS 权重\n(质押量)', '信用权重\n(历史表现)',
                  '区域权重\n(地理分散)', '任期权重\n(流动性)']
    N = 4
    angles = [n / float(N) * 2 * np.pi for n in range(N)]
    angles += angles[:1]

    high_node = [9, 8, 7, 6]
    low_node  = [4, 3, 8, 9]
    high_node += high_node[:1]
    low_node  += low_node[:1]

    ax3.plot(angles, high_node, color=C_TEAL, lw=2.5, label='高FTS节点')
    ax3.fill(angles, high_node, color=C_TEAL, alpha=0.2)
    ax3.plot(angles, low_node, color=C_ACCENT, lw=2, label='低FTS节点', linestyle='--')
    ax3.fill(angles, low_node, color=C_ACCENT, alpha=0.1)

    ax3.set_xticks(angles[:-1])
    ax3.set_xticklabels(categories, fontsize=9, color=C_LIGHT, fontfamily=FONT)
    ax3.set_ylim(0, 10)
    ax3.set_yticks([2, 4, 6, 8, 10])
    ax3.set_yticklabels(['2','4','6','8','10'], fontsize=7, color=C_GRAY)
    ax3.grid(color=C_GRAY, alpha=0.4, lw=0.8)
    ax3.set_facecolor(C_NAVY)
    ax3.spines['polar'].set_color(C_GRAY)
    ax3.set_title('FTS 四维权重雷达图', fontsize=12, color=C_WHITE,
                  pad=15, fontfamily=FONT)
    ax3.legend(loc='upper right', bbox_to_anchor=(1.35, 1.1), fontsize=9,
               facecolor=C_DARK, labelcolor=C_LIGHT,
               prop={'family': FONT})

    # Big title
    fig.text(0.5, 0.96, 'Shardora 经济模型', fontsize=20, color=C_WHITE,
             fontweight='bold', ha='center', fontfamily=FONT)
    fig.text(0.5, 0.925, '比特币式减半 · 分代分片权重 · FTS 四维选举 · 50% Gas 销毁',
             fontsize=12, color=C_TEAL, ha='center', fontfamily=FONT)

    # Key params box
    params = [
        ('总供应上限', '~42 亿 SHD'),
        ('Epoch 周期', '600 秒'),
        ('减半周期', '≈ 4 年'),
        ('Gas 销毁', '50%'),
        ('最低质押', '8 SHD'),
    ]
    for i, (k, v) in enumerate(params):
        px = 0.06 + i * 0.082
        fig.add_axes([px, 0.07, 0.075, 0.08]).set_visible(False)
        rounded_box_fig = FancyBboxPatch((px, 0.06), 0.072, 0.09,
                                         boxstyle='round,pad=0,rounding_size=0.01',
                                         facecolor=C_NAVY, edgecolor=C_TEAL,
                                         linewidth=1.5,
                                         transform=fig.transFigure, zorder=3)
        fig.add_artist(rounded_box_fig)
        fig.text(px + 0.036, 0.115, v, fontsize=12, color=C_GOLD,
                 ha='center', va='center', fontweight='bold', fontfamily=FONT)
        fig.text(px + 0.036, 0.082, k, fontsize=8.5, color=C_LIGHT,
                 ha='center', va='center', fontfamily=FONT)

    save(fig, 'fig4_economic.jpg')


# ═══════════════════════════════════════════════════════════════════
# DIAGRAM 5 – Blockchain Trilemma Comparison
# ═══════════════════════════════════════════════════════════════════
def diagram_trilemma():
    fig, axes = plt.subplots(1, 2, figsize=(18, 10))
    fig.patch.set_facecolor(C_DARK)

    # ── Left: radar comparison ──
    ax = fig.add_subplot(121, polar=True)
    ax.set_facecolor(C_NAVY)
    fig.patch.set_facecolor(C_DARK)

    projects = {
        'Shardora':        ([9, 9.5, 10], C_GOLD,   2.5),
        'Ethereum 2.0':    ([7, 9, 6],    C_TEAL,   1.8),
        'Polkadot':        ([6, 8, 8],    C_PURPLE, 1.8),
        'Solana':          ([4, 6, 10],   C_ACCENT, 1.8),
        'Bitcoin':         ([8, 10, 2],   C_GREEN,  1.8),
        'Avalanche':       ([7, 8, 7],    '#4895ef', 1.5),
    }
    categories = ['去中心化', '安全性', '可扩展性']
    N = 3
    angles = [n / float(N) * 2 * np.pi for n in range(N)]
    angles += angles[:1]

    for name, (scores, color, lw) in projects.items():
        vals = scores + scores[:1]
        ax.plot(angles, vals, color=color, lw=lw,
                label=name, alpha=0.9 if name == 'Shardora' else 0.75)
        ax.fill(angles, vals, color=color,
                alpha=0.35 if name == 'Shardora' else 0.05)

    ax.set_xticks(angles[:-1])
    ax.set_xticklabels(categories, fontsize=14, color=C_WHITE,
                       fontfamily=FONT, fontweight='bold')
    ax.set_ylim(0, 10)
    ax.set_yticks([2, 4, 6, 8, 10])
    ax.set_yticklabels(['2','4','6','8','10'], fontsize=9, color=C_GRAY)
    ax.grid(color=C_GRAY, alpha=0.4, lw=0.8)
    ax.spines['polar'].set_color(C_GRAY)
    ax.set_title('区块链不可能三角对比', fontsize=15, color=C_WHITE,
                 pad=20, fontfamily=FONT)
    ax.legend(loc='upper right', bbox_to_anchor=(1.6, 1.15), fontsize=10,
              facecolor=C_DARK, labelcolor=C_LIGHT,
              prop={'family': FONT})

    # ── Right: bar chart comparison ──
    ax2 = axes[1]
    ax2.set_facecolor(C_NAVY)
    for spine in ax2.spines.values():
        spine.set_color(C_GRAY)
    ax2.tick_params(colors=C_LIGHT, labelsize=10)
    fig.delaxes(axes[0])

    projs    = ['Shardora', 'Ethereum 2.0', 'Polkadot', 'Avalanche', 'NEAR', 'Solana', 'Bitcoin']
    decentr  = [9.0, 7.0, 6.0, 7.0, 6.0, 4.0, 8.0]
    security = [9.5, 9.0, 8.0, 8.0, 7.0, 6.0, 10.0]
    scalab   = [10.0, 6.0, 8.0, 7.0, 8.0, 10.0, 2.0]

    x = np.arange(len(projs))
    w = 0.24
    b1 = ax2.bar(x - w, decentr,  w, label='去中心化', color=C_GREEN,   alpha=0.85)
    b2 = ax2.bar(x,     security, w, label='安全性',   color=C_TEAL,    alpha=0.85)
    b3 = ax2.bar(x + w, scalab,   w, label='可扩展性', color=C_ACCENT,  alpha=0.85)

    # Shardora highlight
    for bar in [b1[0], b2[0], b3[0]]:
        bar.set_edgecolor(C_GOLD)
        bar.set_linewidth(2.5)
        bar.set_alpha(1.0)

    for bars in [b1, b2, b3]:
        for bar in bars:
            h = bar.get_height()
            ax2.text(bar.get_x() + bar.get_width()/2, h + 0.1,
                     f'{h:.0f}', fontsize=8, color=C_LIGHT,
                     ha='center', va='bottom', fontfamily=FONT)

    ax2.set_ylim(0, 12.5)
    ax2.set_xticks(x)
    ax2.set_xticklabels(projs, fontsize=11, fontfamily=FONT)
    ax2.set_ylabel('评分（满分 10）', fontsize=12, color=C_LIGHT, fontfamily=FONT)
    ax2.set_title('各公链不可能三角定量评分', fontsize=15, color=C_WHITE,
                  pad=10, fontfamily=FONT)
    ax2.legend(fontsize=11, facecolor=C_DARK, labelcolor=C_LIGHT,
               prop={'family': FONT})
    ax2.grid(axis='y', color=C_GRAY, alpha=0.3, lw=0.8)

    # Area score annotation
    areas = {
        'Shardora': 42.9,
        'Ethereum 2.0': 27.5,
        'Polkadot': 27.7,
        'Avalanche': 26.5,
        'NEAR': 24.5,
        'Solana': 23.3,
        'Bitcoin': 19.6,
    }
    for i, (proj, area) in enumerate(areas.items()):
        color = C_GOLD if proj == 'Shardora' else C_GRAY
        ax2.text(i, 11.8, f'面积\n{area}', fontsize=8, color=color,
                 ha='center', va='center', fontfamily=FONT,
                 fontweight='bold' if proj == 'Shardora' else 'normal')

    fig.suptitle('Shardora 区块链不可能三角定量分析', fontsize=20,
                 color=C_WHITE, fontweight='bold', y=0.97, fontfamily=FONT)
    fig.text(0.5, 0.93, '三角面积 = (去中心化 × 安全性 × 可扩展性 × 2) / (3√3)  |  Shardora 综合得分领先业界',
             fontsize=11, color=C_TEAL, ha='center', fontfamily=FONT)

    save(fig, 'fig5_trilemma.jpg')


# ═══════════════════════════════════════════════════════════════════
# DIAGRAM 6 – Consensus & DKG Flow
# ═══════════════════════════════════════════════════════════════════
def diagram_consensus_dkg():
    fig, ax = plt.subplots(figsize=(18, 11))
    fig.patch.set_facecolor(C_DARK)
    ax.set_facecolor(C_DARK)
    ax.set_xlim(0, 18); ax.set_ylim(0, 11)
    ax.axis('off')

    ax.text(9, 10.55, 'Fast-HotStuff BFT 共识流程 + DKG 换届协议', fontsize=20,
            color=C_WHITE, fontweight='bold', ha='center', fontfamily=FONT)
    ax.text(9, 10.1, '两阶段流水线提交 · EVS 增强视图同步 · Feldman VSS 三阶段 DKG · 零停机换届',
            fontsize=11, color=C_TEAL, ha='center', fontfamily=FONT)

    # ── HotStuff pipeline ──
    ax.text(0.5, 9.5, 'Fast-HotStuff 流水线', fontsize=13, color=C_GOLD,
            fontweight='bold', fontfamily=FONT)

    block_ys = 8.8
    blocks = [
        ('B(h-1)\nQC(h-2)', C_BLUE, 1.8),
        ('B(h)\nQC(h-1)', C_PURPLE, 4.5),
        ('B(h+1)\nQC(h)', C_ACCENT, 7.2),
        ('B(h+2)\nQC(h+1)', C_TEAL, 9.9),
        ('B(h+3)\n···', C_GRAY, 12.6),
    ]
    for (label_text, col, bx) in blocks:
        rounded_box(ax, bx, block_ys, 2.2, 0.9, col, radius=0.1)
        ax.text(bx, block_ys + 0.12, label_text.split('\n')[0], fontsize=11,
                color=C_WHITE, fontweight='bold', ha='center', fontfamily=FONT)
        ax.text(bx, block_ys - 0.2, label_text.split('\n')[1], fontsize=9,
                color=C_LIGHT, ha='center', fontfamily=FONT)

    # commit arrows
    commit_pairs = [(1.8, 4.5, 'B(h-1) 提交'), (4.5, 7.2, 'B(h) 提交')]
    for x1, x2, ctxt in commit_pairs:
        ax.annotate('', xy=(x2 - 1.1, block_ys - 0.85), xytext=(x1, block_ys - 0.45),
                    arrowprops=dict(arrowstyle='->', color=C_GREEN, lw=2,
                                   connectionstyle='arc3,rad=-0.3'), zorder=5)
    ax.text(3.2, 7.65, 'B(h+1)到达 → B(h)提交', fontsize=9, color=C_GREEN,
            ha='center', fontfamily=FONT)
    ax.text(5.9, 7.65, 'B(h+2)到达 → B(h+1)提交', fontsize=9, color=C_GREEN,
            ha='center', fontfamily=FONT)

    # forward arrows
    for i in range(len(blocks) - 1):
        x1 = blocks[i][2] + 1.1
        x2 = blocks[i+1][2] - 1.1
        ax.annotate('', xy=(x2, block_ys), xytext=(x1, block_ys),
                    arrowprops=dict(arrowstyle='->', color=C_TEAL, lw=2), zorder=5)

    # EVS box
    rounded_box(ax, 14.5, block_ys, 2.8, 1.2, C_NAVY, radius=0.12)
    ax.text(14.5, block_ys + 0.3, 'EVS 视图同步', fontsize=12, color=C_GOLD,
            fontweight='bold', ha='center', fontfamily=FONT)
    ax.text(14.5, block_ys - 0.05, '仅接受携带有效', fontsize=9, color=C_LIGHT,
            ha='center', fontfamily=FONT)
    ax.text(14.5, block_ys - 0.28, 'QC 的消息推进视图', fontsize=9, color=C_LIGHT,
            ha='center', fontfamily=FONT)
    ax.text(14.5, block_ys - 0.52, '1024节点 < 500ms 切换', fontsize=9.5,
            color=C_TEAL, ha='center', fontfamily=FONT)

    # QC structure
    rounded_box(ax, 16.9, block_ys, 2.0, 1.2, C_GRAY, radius=0.1)
    ax.text(16.9, block_ys + 0.3, 'QC 结构', fontsize=10, color=C_LIGHT,
            fontweight='bold', ha='center', fontfamily=FONT)
    for i, field in enumerate(['view', 'block_hash', 'elect_height', 'sign_x/y']):
        ax.text(16.9, block_ys + 0.05 - i * 0.22, field, fontsize=8,
                color=C_TEAL, ha='center', fontfamily=FONT)

    # ── BLS voting flow ──
    ax.text(0.5, 7.15, 'BLS 门限签名聚合', fontsize=13, color=C_GOLD,
            fontweight='bold', fontfamily=FONT)

    nodes_y = 6.5
    for i in range(8):
        nx = 1.5 + i * 1.7
        circle = plt.Circle((nx, nodes_y), 0.4,
                             color=C_BLUE if i < 6 else C_ACCENT,
                             zorder=3)
        ax.add_patch(circle)
        ax.text(nx, nodes_y, f'n{i+1}' if i < 6 else 'L',
                fontsize=9, color=C_WHITE, ha='center', va='center',
                fontfamily=FONT)
        if i < 6:
            ax.annotate('', xy=(13.8, 6.25), xytext=(nx + 0.4, nodes_y - 0.1),
                        arrowprops=dict(arrowstyle='->', color=C_TEAL, lw=1.2,
                                        alpha=0.7), zorder=4)

    # Leader aggregation
    rounded_box(ax, 14.5, 6.2, 2.8, 1.0, C_PURPLE, radius=0.1)
    ax.text(14.5, 6.48, '领导者聚合', fontsize=11, color=C_WHITE,
            fontweight='bold', ha='center', fontfamily=FONT)
    ax.text(14.5, 6.2, '收集 ⌈2n/3⌉ 个部分签名', fontsize=9, color=C_LIGHT,
            ha='center', fontfamily=FONT)
    ax.text(14.5, 5.95, 'Lagrange 插值 → σ_agg', fontsize=9, color=C_TEAL,
            ha='center', fontfamily=FONT)

    # 96 bytes badge
    rounded_box(ax, 17.2, 6.2, 1.4, 0.8, C_GOLD, alpha=0.3, radius=0.1)
    ax.text(17.2, 6.35, '96', fontsize=22, color=C_GOLD, fontweight='bold',
            ha='center', va='center', fontfamily=FONT)
    ax.text(17.2, 5.98, 'bytes', fontsize=9, color=C_LIGHT,
            ha='center', va='center', fontfamily=FONT)
    ax.text(17.2, 5.78, 'O(1) 验证', fontsize=9, color=C_TEAL,
            ha='center', fontfamily=FONT)

    ax.text(9, 5.55, '1024 节点委员会共识结果 → 96 字节聚合签名 → 单次双线性配对验证，与委员会规模无关',
            fontsize=10, color=C_TEAL, ha='center', fontfamily=FONT)

    # ── DKG three phases ──
    ax.text(0.5, 5.15, 'Feldman VSS 三阶段 DKG（总耗时 ~200s，后台并行）', fontsize=13,
            color=C_GOLD, fontweight='bold', fontfamily=FONT)

    phases = [
        (3.2, '阶段一\n0 ~ 80s', '验证向量广播', 'Feldman 承诺 V_i = [aᵢ₀·G2, ...]', C_BLUE),
        (9,   '阶段二\n80 ~ 160s', '加密份额交换', 'ECDH 加密点对点发送 fᵢ(j)', C_PURPLE),
        (14.8, '阶段三\n160 ~ 200s', '本地聚合', 'sk_j = Σ fᵢ(j+1)  PK = Σ aᵢ₀·G2', C_TEAL),
    ]
    for px, title, sub1, sub2, col in phases:
        rounded_box(ax, px, 4.1, 5.0, 1.5, col, alpha=0.2, radius=0.12)
        fancy_p = FancyBboxPatch((px - 2.5, 3.35), 5.0, 1.5,
                                 boxstyle='round,pad=0,rounding_size=0.12',
                                 facecolor='none', edgecolor=col,
                                 linewidth=2, zorder=3)
        ax.add_patch(fancy_p)
        rounded_box(ax, px, 4.7, 2.0, 0.45, col, radius=0.08)
        ax.text(px, 4.7, title, fontsize=10, color=C_WHITE, fontweight='bold',
                ha='center', va='center', fontfamily=FONT)
        ax.text(px, 4.25, sub1, fontsize=10, color=C_WHITE,
                ha='center', va='center', fontfamily=FONT)
        ax.text(px, 3.9, sub2, fontsize=8.5, color=C_LIGHT,
                ha='center', va='center', fontfamily=FONT)

    ax.annotate('', xy=(6.5, 4.1), xytext=(5.7, 4.1),
                arrowprops=dict(arrowstyle='->', color=C_GOLD, lw=2.5), zorder=5)
    ax.annotate('', xy=(12.3, 4.1), xytext=(11.5, 4.1),
                arrowprops=dict(arrowstyle='->', color=C_GOLD, lw=2.5), zorder=5)

    # ── Waiting shard / zero-downtime ──
    ax.text(0.5, 3.0, '等待分片机制 → 零停机换届', fontsize=13, color=C_GOLD,
            fontweight='bold', fontfamily=FONT)

    timeline_y = 2.35
    ax.axhline(y=timeline_y, xmin=0.03, xmax=0.97,
               color=C_GRAY, lw=1.5, linestyle='-', zorder=2)

    events = [
        (2.0,  'Epoch N 开始',   C_TEAL, '↓'),
        (5.5,  'DKG 启动',       C_GOLD, '↓'),
        (8.0,  'DKG 完成',       C_GREEN,'↓'),
        (10.5, 'Epoch N+1\n瞬间切换', C_ACCENT,'↑'),
        (14.0, 'Epoch N+1\n运行中', C_TEAL, '↑'),
    ]
    for ex, etxt, ecol, edir in events:
        ax.axvline(x=ex, ymin=0.17, ymax=0.25,
                   color=ecol, lw=2.5, zorder=4)
        yo = timeline_y + (0.35 if edir == '↓' else -0.7)
        ax.text(ex, yo, etxt, fontsize=9, color=ecol,
                ha='center', va='center', fontfamily=FONT)

    # Bands
    ax.fill_betweenx([timeline_y - 0.15, timeline_y + 0.15],
                      2.0, 10.5, color=C_BLUE, alpha=0.3, zorder=1)
    ax.text(6.25, timeline_y, '共识分片委员会 (Epoch N)', fontsize=9,
            color=C_TEAL, ha='center', va='center', fontfamily=FONT)

    ax.fill_betweenx([timeline_y - 0.12, timeline_y + 0.12],
                      5.5, 10.5, color=C_PURPLE, alpha=0.25, zorder=1)
    ax.text(8.0, timeline_y - 0.25, '等待分片 DKG 并行运行', fontsize=8.5,
            color=C_PURPLE, ha='center', va='center', fontfamily=FONT)

    ax.fill_betweenx([timeline_y - 0.15, timeline_y + 0.15],
                      10.5, 17.0, color=C_TEAL, alpha=0.2, zorder=1)
    ax.text(13.75, timeline_y, '共识分片委员会 (Epoch N+1)', fontsize=9,
            color=C_TEAL, ha='center', va='center', fontfamily=FONT)

    ax.text(10.5, 1.65,
            '换届时刻：新委员会密钥已就绪 → 服务无中断 → TPS 无损失',
            fontsize=11, color=C_GREEN, ha='center', fontweight='bold',
            fontfamily=FONT)
    ax.text(10.5, 1.3,
            'DKG 密钥复用优化：连任节点无需重跑全量DKG → 换届同步开销降低 90%',
            fontsize=10, color=C_LIGHT, ha='center', fontfamily=FONT)

    save(fig, 'fig6_consensus_dkg.jpg')


if __name__ == '__main__':
    print('Generating Shardora whitepaper diagrams...')
    diagram_architecture()
    diagram_tech_flow()
    diagram_cross_shard()
    diagram_economic()
    diagram_trilemma()
    diagram_consensus_dkg()
    print('All diagrams generated!')
