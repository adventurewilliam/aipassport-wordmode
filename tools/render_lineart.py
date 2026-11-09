#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
P0 词与图案过稿页 · v1.1（方案 A 已落地）

按 p0_words.json 的笔画指令精确渲染（与设备端 word_render.c 规则一致），产出：
  1) lineart-preview.png    —— 2x5 落地后 10 词拼图
  2) lineart-landed.png     —— 4 组「改前 → 改后」对比拼图（4 行 x 2 列）
  3) p0-words-review.html   —— 过稿页：10 词卡 + 4 组对比 + 真机主屏 + 元数据

v1.1 变更（2026-11-09 · 方案 A）：
  banana / milk / tree 按 bbyingyu.com 的 emoji 形态重描；hand 换词为 eye。
  改前笔画存档于 lineart-before.json，供本页做 A/B 对比。
  音标不新增、不改动；words/ 词表零改动；设备端渲染链路不变。

渲染规则：OP_LINE=1 / OP_CIRCLE=2 / OP_RECT=3 / OP_DOT=4，坐标 0..200，
单色深墨 #202a33，线宽 3，圆角 4；曲线已在备课侧扁平为折线。
"""
import json, os, math

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_HTML = '/workspace/p0-words-review.html'
INK = '#202a33'

doc = json.load(open(os.path.join(HERE, 'p0_words.json'), encoding='utf-8'))
WORDS = doc['words']
BEFORE = json.load(open(os.path.join(HERE, 'lineart-before.json'), encoding='utf-8'))

# ---------- 改动词映射与说明 ----------
CHANGED = ['banana', 'milk', 'eye', 'tree']
BEFORE_KEY = {'banana': 'banana', 'milk': 'milk', 'tree': 'tree', 'eye': 'hand'}
CHANGE_NOTE = {
    'banana': '按 🍌 重描：弯月加厚 + 内侧棱线 + 粗柄 + 尾尖，做出香蕉的厚度',
    'milk':   '按 🥛 重描：改画「一杯牛奶」，梯形杯身 + 液面 + 斜吸管 + 两道波纹',
    'eye':    'hand 换词为 eye：手改图试过仍像栅栏 / 笔筒，改用一眼能认的眼睛',
    'tree':   '按 🌳 重描：改画三层三角松树，轮廓明确',
}

# ---------- SVG 渲染 ----------
def strokes_svg(strokes, size=200):
    s = [f'<svg viewBox="0 0 200 200" xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}">',
         f'<g stroke="{INK}" stroke-width="3" stroke-linecap="round" stroke-linejoin="round" fill="none">']
    for st in strokes:
        op, x1, y1, x2, y2 = st
        if op == 1:
            s.append(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}"/>')
        elif op == 2:
            s.append(f'<circle cx="{x1}" cy="{y1}" r="{x2}"/>')
        elif op == 3:
            x, y = min(x1, x2), min(y1, y2)
            s.append(f'<rect x="{x}" y="{y}" width="{abs(x2-x1)}" height="{abs(y2-y1)}" rx="4"/>')
        elif op == 4:
            s.append(f'<circle cx="{x1}" cy="{y1}" r="{x2}" fill="{INK}" stroke="none"/>')
    s.append('</g></svg>')
    return '\n'.join(s)

def svg_for(w, size=200):
    return strokes_svg(w['strokes'], size)

# ---------- PNG 拼图 ----------
def setup_font():
    import matplotlib
    matplotlib.use('Agg')
    from matplotlib import font_manager
    for fp in ['/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc',
               '/usr/share/fonts/truetype/wqy/wqy-microhei.ttc']:
        if os.path.exists(fp):
            return font_manager.FontProperties(fname=fp)
    return None

def draw_strokes(ax, strokes):
    from matplotlib.patches import Circle, Rectangle
    ax.set_xlim(0, 200); ax.set_ylim(200, 0); ax.set_aspect('equal'); ax.axis('off')
    for st in strokes:
        op, x1, y1, x2, y2 = st
        if op == 1:
            ax.plot([x1, x2], [y1, y2], color=INK, lw=3, solid_capstyle='round')
        elif op == 2:
            ax.add_patch(Circle((x1, y1), x2, fill=False, ec=INK, lw=3))
        elif op == 3:
            ax.add_patch(Rectangle((min(x1, x2), min(y1, y2)), abs(x2-x1), abs(y2-y1),
                                   fill=False, ec=INK, lw=3))
        elif op == 4:
            ax.add_patch(Circle((x1, y1), x2, fc=INK, ec=INK))
    ax.add_patch(Rectangle((2, 2), 196, 196, fill=False, ec='#e6ebef', lw=1, ls='--'))

def make_preview_png(path):
    import matplotlib.pyplot as plt
    prop = setup_font()
    fig, axes = plt.subplots(2, 5, figsize=(15, 6))
    fig.patch.set_facecolor('#ffffff')
    for ax, w in zip(axes.ravel(), WORDS):
        draw_strokes(ax, w['strokes'])
        title = f"{w['word']} / {w['cn']}" if prop else w['word']
        ax.set_title(title, fontsize=13, fontweight='bold', pad=8, fontproperties=prop)
    fig.tight_layout(pad=1.6)
    fig.savefig(path, dpi=110, facecolor='#ffffff')
    plt.close(fig)
    print('PNG:', path, os.path.getsize(path), 'B')

def make_compare_png(path):
    import matplotlib.pyplot as plt
    prop = setup_font()
    fig, axes = plt.subplots(4, 2, figsize=(6.6, 12.4))
    fig.patch.set_facecolor('#ffffff')
    for r, word in enumerate(CHANGED):
        bk = BEFORE_KEY[word]
        old = BEFORE[bk]
        new = next(w for w in WORDS if w['word'] == word)
        for c, (strokes, tag, title) in enumerate([
            (old['strokes'], 'before', f"{bk} / {old['cn']}  [改前]" if prop else f'{bk} [before]'),
            (new['strokes'], 'after', f"{word} / {new['cn']}  [改后]" if prop else f'{word} [after]'),
        ]):
            ax = axes[r][c]
            draw_strokes(ax, strokes)
            ax.set_title(title, fontsize=12, fontweight='bold', pad=7,
                         color=('#c0503f' if tag == 'before' else '#3f8f6b'),
                         fontproperties=prop)
    fig.tight_layout(pad=1.8)
    fig.savefig(path, dpi=110, facecolor='#ffffff')
    plt.close(fig)
    print('PNG:', path, os.path.getsize(path), 'B')

make_preview_png(os.path.join(HERE, 'lineart-preview.png'))
make_compare_png(os.path.join(HERE, 'lineart-landed.png'))
_old_proposal = os.path.join(HERE, 'lineart-proposal.png')
if os.path.exists(_old_proposal):
    os.remove(_old_proposal)   # 提案已落地，删掉旧命名避免混淆

# ---------- 真机主屏模拟（200 → 160x160 绘图区，sx=40+x*0.8）----------
def phone_svg(w):
    def m(v): return 40 + v * 0.8
    s = ['<svg viewBox="0 0 240 320" xmlns="http://www.w3.org/2000/svg">',
         '<rect x="2" y="2" width="236" height="316" rx="20" fill="#fbfaf6" stroke="#202a33" stroke-width="2.5"/>',
         '<rect x="16" y="12" width="208" height="18" rx="6" fill="#eef1f3"/>',
         '<circle cx="28" cy="21" r="3.5" fill="#7fd6a4"/>',
         '<text x="40" y="24.5" font-size="8.5" fill="#8a97a3">学习中</text>',
         '<rect x="40" y="40" width="160" height="160" rx="12" fill="#fff" stroke="#e6ebef"/>',
         f'<g stroke="{INK}" stroke-width="3" stroke-linecap="round" stroke-linejoin="round" fill="none">']
    for st in w['strokes']:
        op, x1, y1, x2, y2 = st
        if op == 1:
            s.append(f'<line x1="{m(x1):.1f}" y1="{m(y1):.1f}" x2="{m(x2):.1f}" y2="{m(y2):.1f}"/>')
        elif op == 2:
            s.append(f'<circle cx="{m(x1):.1f}" cy="{m(y1):.1f}" r="{x2*0.8:.1f}"/>')
        elif op == 3:
            s.append(f'<rect x="{m(min(x1,x2)):.1f}" y="{m(min(y1,y2)):.1f}" '
                     f'width="{abs(x2-x1)*0.8:.1f}" height="{abs(y2-y1)*0.8:.1f}" rx="4"/>')
        elif op == 4:
            s.append(f'<circle cx="{m(x1):.1f}" cy="{m(y1):.1f}" r="{x2*0.8:.1f}" fill="{INK}" stroke="none"/>')
    s += ['</g>',
          '<rect x="24" y="232" width="84" height="52" rx="12" fill="#eaf6f0" stroke="#3f8f6b" stroke-width="2.4"/>',
          '<g transform="translate(66,250) scale(0.62)" fill="none" stroke="#3f8f6b" stroke-width="2.8" stroke-linejoin="round" stroke-linecap="round">'
          '<path d="M8 30 L8 6 Q8 3 11 3 L14 3 L14 -4 Q14 -7 17 -7 Q20 -7 20 -4 L20 8 L24 9 Q28 10 28 14 L28 22 Q28 26 24 26 L11 26 Q8 26 8 22 Z"/></g>',
          '<text x="66" y="280" font-size="9" text-anchor="middle" fill="#3f8f6b">UP 认识</text>',
          '<rect x="132" y="232" width="84" height="52" rx="12" fill="#fff4ea" stroke="#e07a3f" stroke-width="2.4"/>',
          '<text x="174" y="264" font-size="28" text-anchor="middle" fill="#e07a3f" font-weight="700">?</text>',
          '<text x="174" y="280" font-size="9" text-anchor="middle" fill="#e07a3f">DOWN 不记得</text>',
          '<text x="120" y="304" font-size="8.5" text-anchor="middle" fill="#9aa7b2">OK 短按再听 · 长按 OK 菜单</text>',
          '</svg>']
    return '\n'.join(s)

# ---------- 落地后逐词评估 ----------
UNIT_CN = {'unit-01': '水果', 'unit-02': '动物', 'unit-03': '颜色与形状',
           'unit-04': '身体与家人', 'unit-05': '食物', 'unit-06': '自然与交通'}
REVIEW = {
    'apple':  ('A', '果身圆润闭合 + 短柄 + 叶，一眼能认是苹果', '保持'),
    'banana': ('B', '三条平行弧线做出了厚度与弯曲，比改前「一片叶子」明显更像香蕉，但仍略像弯叶 / 羽毛；配合发音与「黄黄的」提示足够，真机可再确认', '按 🍌 重描'),
    'cat':    ('A', '圆脸 + 双尖耳 + 胡须 + 眼鼻嘴，特征最全', '保持'),
    'fish':   ('A', '椭圆身 + 三角尾 + 背鳍 + 泡泡 + 眼嘴，方向明确', '保持'),
    'sun':    ('A', '圆 + 8 射线 + 笑脸，辨识度最高', '保持'),
    'star':   ('A', '标准五角星，10 段折线，干净', '保持'),
    'milk':   ('A', '梯形玻璃杯 + 液面 + 斜吸管 + 波纹，一眼是「一杯牛奶」，不再是房子', '按 🥛 重描'),
    'eye':    ('A', '杏仁眼眶 + 圆瞳 + 睫毛，一眼是眼睛；孩子还能指着自己的眼睛学', 'hand 换词'),
    'tree':   ('A', '三层三角松树，轮廓明确，不再像米老鼠 / 云朵 / 脸', '按 🌳 重描'),
    'car':    ('A', '车身 + 车顶 + 车窗分隔 + 双轮 + 车灯，侧面小汽车很清楚', '保持'),
}
COLOR = {'A': '#3f8f6b', 'B': '#e07a3f', 'C': '#c0503f'}
BG = {'A': '#eaf6f0', 'B': '#fff4ea', 'C': '#fdf0ee'}
LABEL = {'A': '一眼能认', 'B': '能认但有歧义', 'C': '建议改'}

cards = []
for i, w in enumerate(WORDS, 1):
    g, comment, change = REVIEW[w['word']]
    compare_html = ''
    if w['word'] in CHANGED:
        bk = BEFORE_KEY[w['word']]
        b = BEFORE[bk]
        compare_html = f'''
  <div class="compare">
    <div class="cmpbox"><div class="cmptag before">改前 · {bk} {b['count']} 笔</div>{strokes_svg(b['strokes'], 168)}</div>
    <div class="cmparrow">→</div>
    <div class="cmpbox"><div class="cmptag after">改后 · {w['word']} {w['stroke_count']} 笔</div>{svg_for(w, 168)}</div>
  </div>
  <div class="propdesc"><b>落地做法：</b>{CHANGE_NOTE[w['word']]}　<span>{b['count']} 笔 / {b['bytes']} B → {w['stroke_count']} 笔 / {w['binary_size_bytes']} B</span></div>'''
    cards.append(f'''
<div class="card">
  <div class="cardhead"><span class="idx">{i:02d}</span>
    <span class="wname">{w['word']}</span><span class="cn">{w['cn']}</span>
    <span class="grade" style="background:{BG[g]};color:{COLOR[g]};border-color:{COLOR[g]}">{g} · {LABEL[g]}</span>
  </div>
  <div class="cardbody">
    <div class="art">{svg_for(w, 200)}</div>
    <div class="meta">
      <div class="row"><span>单元</span><b>{w['unit']} · {UNIT_CN.get(w['unit'],'')}</b></div>
      <div class="row"><span>音节</span><b>{w['syllables']}</b></div>
      <div class="row"><span>笔画 / 字节</span><b>{w['stroke_count']} 笔 · {w['binary_size_bytes']} B</b></div>
      <div class="row"><span>画的是什么</span><b>{w['draw']}</b></div>
      <div class="row"><span>给小智的提示</span><b>{w['say']}</b></div>
      <div class="say"><b>渲染实况：</b>{comment}</div>
      <div class="fix"><b>本轮：</b>{change if change != '保持' else '保持，不用改'}</div>
    </div>
  </div>{compare_html}
</div>''')

phones = '\n'.join(f'''
<div class="pcell">
  {phone_svg(w)}
  <div class="pcap"><b>{i:02d} {w['word']}</b> · {w['cn']} · {w['stroke_count']} 笔
    <span style="color:{COLOR[REVIEW[w['word']][0]]}">{REVIEW[w['word']][0]} 级</span></div>
</div>''' for i, w in enumerate(WORDS, 1))

tot = doc['totals']
grade_dist = {g: sum(1 for v in REVIEW.values() if v[0] == g) for g in 'ABC'}

html = f'''<!DOCTYPE html>
<html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>P0 词与图案过稿 · 方案 A 已落地 v1.1</title>
<style>
:root{{--paper:#fbfaf6;--ink:#202a33;--muted:#6c7885;--line:#d5dce2;--orange:#e07a3f;--green:#3f8f6b}}
*{{box-sizing:border-box}}
body{{margin:0;background:#eef1f3;color:var(--ink);font-family:-apple-system,BlinkMacSystemFont,"PingFang SC","Microsoft YaHei","Noto Sans CJK SC",sans-serif;line-height:1.7}}
.wrap{{max-width:1180px;margin:0 auto;padding:26px 20px 70px}}
header{{background:linear-gradient(135deg,#202a33,#2f4150);color:#fff;border-radius:18px;padding:28px 32px;box-shadow:0 8px 26px rgba(32,42,51,.18)}}
.kicker{{font-size:12px;letter-spacing:.18em;color:#f0b387;font-weight:700}}
h1{{font-size:28px;margin:9px 0 6px}}
.sub{{color:#c4d0da;font-size:14px;margin:0}}
.metabar{{display:flex;flex-wrap:wrap;gap:8px;margin-top:18px}}
.metabar span{{background:rgba(255,255,255,.11);border:1px solid rgba(255,255,255,.16);border-radius:999px;padding:5px 13px;font-size:12px}}
.metabar span.hot{{background:var(--orange);border-color:var(--orange);font-weight:700}}
.metabar span.ok{{background:var(--green);border-color:var(--green);font-weight:700}}
section{{background:#fff;border:1px solid var(--line);border-radius:16px;padding:24px 28px;margin-top:20px;box-shadow:0 2px 10px rgba(32,42,51,.04)}}
h2{{font-size:19px;margin:0 0 14px;padding-bottom:10px;border-bottom:2px solid var(--ink)}}
p{{margin:8px 0;font-size:13.6px}}
.note{{background:#eef4fb;border-left:4px solid #3b6ea5;border-radius:9px;padding:13px 17px;font-size:13.4px;margin:12px 0}}
.note.o{{background:#fff4ea;border-color:var(--orange)}}
.note.g{{background:#eaf6f0;border-color:var(--green)}}
.cards{{display:grid;grid-template-columns:repeat(2,1fr);gap:16px}}
.card{{border:1px solid var(--line);border-radius:13px;overflow:hidden;background:#fcfdfd}}
.cardhead{{display:flex;align-items:center;gap:9px;padding:11px 15px;background:#f7f9fa;border-bottom:1px solid var(--line);flex-wrap:wrap}}
.idx{{font:700 12px monospace;color:#9aa7b2}}
.wname{{font-size:17px;font-weight:800;letter-spacing:-.3px}}
.cn{{font-size:13px;color:var(--muted)}}
.grade{{margin-left:auto;font-size:11px;font-weight:700;border:1px solid;border-radius:6px;padding:2px 8px}}
.cardbody{{display:grid;grid-template-columns:200px 1fr;gap:14px;padding:14px}}
.art{{background:#fff;border:1px solid #e6ebef;border-radius:10px;display:flex;align-items:center;justify-content:center;padding:6px}}
.art svg{{width:100%;height:auto;display:block}}
.meta{{font-size:12.4px;min-width:0}}
.row{{display:flex;gap:8px;padding:3px 0;border-bottom:1px dashed #eceff2}}
.row span{{color:var(--muted);flex:none;width:88px}}
.row b{{font-weight:600;word-break:break-all}}
.say{{margin-top:9px;font-size:12.2px;color:#3d4b57}}
.fix{{margin-top:5px;font-size:12.2px;color:#2f7a58}}
.compare{{display:flex;align-items:center;gap:10px;padding:0 14px 8px;flex-wrap:wrap}}
.cmpbox{{background:#fff;border:1px solid var(--line);border-radius:10px;padding:8px;text-align:center;flex:1;min-width:150px}}
.cmpbox svg{{width:100%;max-width:168px;height:auto;display:block;margin:0 auto}}
.cmptag{{font-size:10.5px;font-weight:700;border-radius:5px;padding:2px 7px;margin-bottom:6px;display:inline-block}}
.cmptag.before{{background:#fdf0ee;color:#c0503f}} .cmptag.after{{background:#eaf6f0;color:#3f8f6b}}
.cmparrow{{font-size:20px;color:#9aa7b2;flex:none}}
.propdesc{{padding:0 14px 13px;font-size:11.8px;color:#3d4b57}}
.propdesc span{{color:var(--muted)}}
.phones{{display:grid;grid-template-columns:repeat(auto-fill,minmax(180px,1fr));gap:14px}}
.pcell{{background:#f4f6f7;border:1px solid var(--line);border-radius:12px;padding:11px;text-align:center}}
.pcell svg{{width:100%;max-width:196px;height:auto;display:block;margin:0 auto;border-radius:8px;background:#fff}}
.pcap{{font-size:11.6px;color:var(--muted);margin-top:8px;line-height:1.5}}
.pcap b{{color:var(--ink)}}
table{{width:100%;border-collapse:collapse;font-size:13px;margin:12px 0}}
th,td{{border:1px solid var(--line);padding:8px 10px;text-align:left}}
th{{background:#f5f7f9}}
ul{{margin:8px 0;padding-left:21px}} li{{margin:5px 0;font-size:13.4px}}
code{{background:#f4f6f8;padding:1px 5px;border-radius:4px;font-size:12px;color:#a33d2a}}
footer{{margin-top:24px;text-align:center;color:var(--muted);font-size:12px;line-height:1.8}}
@media(max-width:820px){{.cards{{grid-template-columns:1fr}}.cardbody{{grid-template-columns:1fr}}.wrap{{padding:14px 10px 50px}}}}
</style></head><body><div class="wrap">

<header>
  <div class="kicker">P0 WORDS &amp; LINEART · LANDED v1.1</div>
  <h1>10 个词 · 图案已按方案 A 落地</h1>
  <p class="sub">下面是按 <code>p0_words.json</code> 笔画指令<b>精确渲染</b>出来的图，也就是设备屏幕上真正会显示的样子。4 张问题图已重描 / 换词，每张都附「改前 → 改后」对比。</p>
  <div class="metabar">
    <span class="ok">方案 A 已落地</span>
    <span>C1 已定稿：长按 OK 呼菜单</span>
    <span>{tot['words']} 词 / {tot['strokes']} 笔 / {tot['bytes']} B</span>
    <span>20 段 ADP1 · 280.0 KiB</span>
    <span>改后 A {grade_dist['A']} · B {grade_dist['B']} · C {grade_dist['C']}</span>
    <span>音标不新增 · 词表零改动</span>
    <span>2026-11-09</span>
  </div>
</header>

<section>
<h2>怎么读这份过稿页</h2>
<div class="note"><b>屏幕上没有任何文字。</b>这里显示的单词、中文、音节、讲解提示<b>只给家长、TTS 和小智用</b>；孩子看到的只有线图 + 底下两个图案（UP 拇指 / DOWN 问号）。本轮按你的意见<b>不做音标</b>：不新增、不改动，已有的 <code>ipa</code> 保持原样（本来就不上屏）。</div>
<div class="note g"><b>本轮落地了 4 处改动：</b>banana / milk / tree 按 bbyingyu.com 的 emoji 形态重描；<b>hand 换词为 eye</b>（手改图试过仍像栅栏，构图本身难像手）。笔画从 126 笔 / 660 B 变为 <b>{tot['strokes']} 笔 / {tot['bytes']} B</b>（+23 笔 / +115 B），仍远小于 1 KiB；设备端渲染链路<b>一行未改</b>。</div>
<table><thead><tr><th style="width:120px">词</th><th style="width:150px">改动</th><th>依据</th><th style="width:120px">笔画变化</th></tr></thead><tbody>
<tr><td><b>banana</b></td><td>按 🍌 重描</td><td>弯月加厚 + 内侧棱线 + 粗柄 + 尾尖</td><td>18 → 20 笔</td></tr>
<tr><td><b>milk</b></td><td>按 🥛 重描</td><td>改画「一杯牛奶」，梯形杯身取代三角屋顶</td><td>6 → 10 笔</td></tr>
<tr><td><b>tree</b></td><td>按 🌳 重描</td><td>三层三角松树，取代三团圆树冠</td><td>7 → 11 笔</td></tr>
<tr><td><b>hand → eye</b></td><td>换词</td><td>眼睛一眼能认，孩子能指着自己学；重生成 2 段发音</td><td>7 → 20 笔</td></tr>
</tbody></table>
<p>第 2 节是 10 张图放大逐词看（4 张附改前对比）；第 3 节把它们放进 <b>240×320 真机主屏</b>（含 160×160 绘图区和两个 84×52 按钮），看缩到实际尺寸后还认不认得出 —— 这一步最关键。</p>
</section>

<section>
<h2>1 · 逐词线图、元数据与改前对比</h2>
<div class="cards">{''.join(cards)}</div>
</section>

<section>
<h2>2 · 真机主屏模拟（240×320，图缩到 160×160）</h2>
<div class="phones">{phones}</div>
</section>

<section>
<h2>3 · 汇总与边界</h2>
<table><thead><tr><th style="width:130px">等级</th><th>词</th><th>处理</th></tr></thead><tbody>
<tr><td><b style="color:#3f8f6b">A · 一眼能认（{grade_dist['A']}）</b></td><td>{' / '.join(w['word'] for w in WORDS if REVIEW[w['word']][0]=='A')}</td><td>全部进 P0，无需再改</td></tr>
<tr><td><b style="color:#e07a3f">B · 能认但有歧义（{grade_dist['B']}）</b></td><td>{' / '.join(w['word'] for w in WORDS if REVIEW[w['word']][0]=='B')}</td><td>比改前明显改善，但纯轮廓仍略像弯叶；配合发音与颜色提示足够，<b>真机重点验证这一张</b></td></tr>
</tbody></table>
<div class="note o"><b>为什么 banana 没给 A：</b>实际渲染出来是三条平行弧线，香蕉的「弯」和「厚」有了，但缺少能锁定「香蕉」的特征（比如成串、剥开的皮、两端的柄与蒂差异），6 岁孩子单独看图仍可能说「叶子」。这已经是纯线稿在不加颜色、不加场景前提下的较优解；<b>要再往上就得靠颜色或场景，属于升 v2 的范畴</b>，P0 不预先加复杂度。</div>
<div class="note o"><b>一个仍未解决的共性问题：</b>10 张图全是<b>单色线稿、没有填充</b>。「apple 红红的」「banana 黄黄的」这些颜色线索在图上完全没有，只能靠轮廓与发音、小智讲颜色。<br>两条路：① <b>保持纯线稿</b>（最省 Flash，符合「笔画指令」初衷）；② 每笔加 1 字节色号，10 词仍 &lt; 1 KiB，但 <code>wordline-v1</code> 和 <code>word_render.c</code> 要升到 v2。<b>P0 先保持 ①</b>，真机看孩子是否真的需要颜色再决定，不预先加复杂度。</div>
<div class="note"><b>hand 的诚实复盘：</b>先试过「手指加圆头 + 错落高度 + 拇指外张」的改图，渲染出来<b>仍像笔筒 / 试管架 / 栅栏</b> —— 4 根等宽竖条插在方框里，这个构图本身就难像手。所以不再修图，直接换词 <code>eye</code>：<code>unit-04</code> 里本来就有「眼睛」，20 笔一眼能认，且孩子可以指着自己的眼睛学。代价仅为重生成 2 段发音（已用现成脚本完成）。</div>
<div class="note g"><b>本轮没动的东西：</b><code>words/</code> 6 单元 60 词<b>零改动</b>（不加音标、不加字段）；网站词库只作为一次性「图案参照源」留在 <code>p0-pack/source-bbyingyu.json</code>；7 屏 UI、状态机、三键映射、PRD 架构、ADP1 / <code>wordline-v1</code> 格式全部保持原样。</div>
</section>

<section>
<h2>4 · 下一步</h2>
<ul>
<li><b>先拿真机让 6 岁小朋友按一按</b> —— 重点看缩到 160×160 后，banana / milk / eye / tree 这 4 张是否真的能一眼认出（<b>尤其 banana，唯一 B 级</b>），以及两个 84×52 图案是否够大。</li>
<li>确认无误后，按 <code>p0-prd.md</code> §10 产出第 1–3 步代码骨架（<code>word_mode.c/.h</code> + <code>ui_wordmode.c/.h</code> + 契约测试）。</li>
<li>若真机发现「没颜色认不出」，再评估是否把 <code>wordline-v1</code> 升到 v2（每笔加色号），不影响词表与状态机。</li>
</ul>
<div class="note">图案改动成本极低（改 <code>gen_lineart.py</code> 坐标 → 重跑），真机反馈后随时可再调，不需要改架构。</div>
</section>

<footer>
P0 词与图案过稿页 v1.1 · 依据 <code>/workspace/p0-pack/p0_words.json</code>（{tot['words']} 词 / {tot['strokes']} 笔 / {tot['bytes']} B）精确渲染<br>
改前笔画存档 <code>p0-pack/lineart-before.json</code>；生成脚本 <code>gen_lineart.py</code> / <code>render_lineart.py</code>（可复现）。<br>
配套：<code>p0-prd.md</code> / <code>p0-prd.html</code> · <code>wordmode-plan.html</code> v0.3.1 · <code>word-source-review.html</code>
</footer>
</div></body></html>
'''

open(OUT_HTML, 'w', encoding='utf-8').write(html)
print('HTML:', OUT_HTML, os.path.getsize(OUT_HTML), 'B')
print('词数:', len(WORDS), '| 改动词:', CHANGED)
print('评级分布:', {g: n for g, n in grade_dist.items() if n})
