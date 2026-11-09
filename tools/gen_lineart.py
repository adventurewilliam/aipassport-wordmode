#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
P0 保底词包 · 线图生成脚本（可复现 / 可扩展到 60 词）

用途：把「简笔画」编译成 5 字节一笔的笔画指令，产出三类文件：
  p0_words.json  —— 可读元数据 + 笔画，家长/备课脚本核对用
  p0_pack.h      —— static const C 数组，放 .rodata，ESP-IDF 默认 Flash 只读映射，不占 RAM
  p0_pack.bin    —— 10 段顺序拼接的二进制（带 4 字节索引头），供 P1 打包脚本做参考实现

v1.1（2026-11-09 · 方案 A 落地）：
  banana / milk / tree 按 bbyingyu.com 的 emoji 形态重描；hand 换成 eye
  （unit-04 已有该词，杏仁眼眶一眼能认，且孩子能指着自己的眼睛学）。
  设备端渲染链路不变，仍是 5 字节一笔、LINE/CIRCLE/RECT/DOT 四种图元。
  音标一律不新增、不改动（小朋友看不懂，且原本就不上屏）。

格式（与 words/index.json 的 wordline-v1 完全一致）：
  每条 5 字节 [opcode, x1, y1, x2, y2]，坐标归一化 0..200
  OP_LINE=1 (x1,y1)-(x2,y2) / OP_CIRCLE=2 圆心(x1,y1) 半径x2 / OP_RECT=3 对角 / OP_DOT=4 点(x1,y1) 半径x2
  一个词 = magic(2B: 'W''L') + stroke_count(1B) + n*5B

运行： python3 gen_lineart.py
"""
import json, math, os, struct

HERE = os.path.dirname(os.path.abspath(__file__))
MAGIC = b'WL'

# ---------- 笔画原语 ----------
def L(x1, y1, x2, y2):  return [1, x1, y1, x2, y2]
def C(cx, cy, r):       return [2, cx, cy, r, 0]
def R(x1, y1, x2, y2):  return [3, x1, y1, x2, y2]
def D(cx, cy, r):       return [4, cx, cy, r, 0]

def poly(points, close=False):
    """折线：点列 -> 连续 LINE；close=True 时补一段回到起点"""
    pts = list(points) + ([points[0]] if close else [])
    return [L(*a, *b) for a, b in zip(pts, pts[1:])]

def bezier(p0, p1, p2, n):
    """二次贝塞尔 -> n 段 LINE（曲线一律在备课侧扁平化，设备端不需要 arc）"""
    pts = []
    for i in range(n + 1):
        t = i / n
        x = (1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t ** 2 * p2[0]
        y = (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t ** 2 * p2[1]
        pts.append((round(x), round(y)))
    return poly(pts)

def ellipse(cx, cy, rx, ry, n=12):
    """椭圆 -> n 段 LINE 闭合"""
    pts = []
    for i in range(n):
        a = 2 * math.pi * i / n
        pts.append((round(cx + rx * math.cos(a)), round(cy + ry * math.sin(a))))
    return poly(pts, close=True)

# ---------- 10 个词的简笔画（全部只用 LINE / CIRCLE / RECT / DOT，<=25 笔） ----------
ART = {}

# apple：沿用 words/lineart/apple.strokes.json 的 17 笔，保证已有示例不被改写
ART['apple'] = [
    [1,100,55,78,58],[1,78,58,62,72],[1,62,72,56,95],[1,56,95,60,120],
    [1,60,120,78,140],[1,78,140,100,145],[1,100,145,122,140],[1,122,140,140,120],
    [1,140,120,144,95],[1,144,95,138,72],[1,138,72,122,58],[1,122,58,100,55],
    [1,100,55,102,38],[1,102,42,120,30],[1,120,30,128,42],[1,128,42,102,42],
    [4,78,80,4,0],
]

# banana：照 🍌 重描 —— 弯月加厚 + 内侧棱线 + 粗柄 + 尾尖，做出「厚度」
ART['banana'] = (
    bezier((38,52),(78,168),(174,150), 6) +
    bezier((174,150),(108,112),(38,52), 6) +
    bezier((54,74),(98,140),(158,140), 5) +
    [L(38,52,28,40), L(46,56,36,40), L(174,150,182,142)]
)

# cat：圆脸 + 双耳 + 眼 + 鼻 + 嘴 + 胡须
ART['cat'] = (
    [C(100,112,52)] +
    poly([(58,78),(50,36),(88,64)], close=True) +
    poly([(112,64),(150,36),(142,78)], close=True) +
    [D(80,102,5), D(120,102,5), D(100,124,3)] +
    [L(100,127,92,133), L(100,127,108,133)] +
    [L(58,118,30,112), L(58,126,32,128), L(142,118,170,112), L(142,126,168,128)]
)

# fish：椭圆身 + 三角尾 + 眼 + 嘴 + 背鳍 + 两个泡泡
ART['fish'] = (
    ellipse(90,100,48,34,12) +
    poly([(138,100),(178,70),(178,130)], close=True) +
    [D(60,92,4)] +
    [L(42,104,52,108), L(42,104,50,98)] +
    poly([(78,68),(100,48),(118,66)]) +
    [C(45,55,6), C(32,38,4)]
)

# star：五角星 10 段折线
def star(cx, cy, R_, r_, n=5):
    pts = []
    for i in range(2 * n):
        a = -math.pi / 2 + math.pi * i / n
        rad = R_ if i % 2 == 0 else r_
        pts.append((round(cx + rad * math.cos(a)), round(cy + rad * math.sin(a))))
    return poly(pts, close=True)
ART['star'] = star(100,100,72,30)

# eye：换词（hand 试过修图仍像栅栏 / 笔筒，构图本身难像手），照 👀 重描
# 杏仁形眼眶 + 圆瞳 + 2 根睫毛；20 笔，与 unit-04 的 strokes_max=20 一致
ART['eye'] = (
    bezier((30,100),(100,52),(170,100), 8) +
    bezier((170,100),(100,148),(30,100), 8) +
    [C(100,100,22), D(100,100,9)] +
    [L(48,74,40,62), L(152,74,160,62)]
)

# milk：照 🥛 重描 —— 一杯牛奶（梯形杯身 + 液面 + 斜吸管 + 两道波纹）
# 原「牛奶盒 + 三角屋顶 + 中间日字」渲染出来就是一座房子，故改
ART['milk'] = [
    L(58,72,66,166), L(66,166,134,166), L(134,166,142,72), L(58,72,142,72),
    L(62,94,138,94), L(112,94,128,40),
    L(76,116,88,120), L(88,120,100,116),
    L(104,134,116,138), L(116,138,128,134),
]

# sun：圆 + 8 射线 + 笑脸
ART['sun'] = [C(100,100,40)]
for i in range(8):
    a = 2 * math.pi * i / 8
    ART['sun'] += [L(round(100+50*math.cos(a)), round(100+50*math.sin(a)),
                     round(100+72*math.cos(a)), round(100+72*math.sin(a)))]
ART['sun'] += [D(88,92,4), D(112,92,4), L(85,112,100,122), L(100,122,115,112)]

# tree：照 🌳 重描 —— 三层三角松树，一眼是树，不再像米老鼠 / 云朵 / 脸
ART['tree'] = (
    poly([(100,28),(52,88),(148,88)], close=True) +
    poly([(100,58),(42,120),(158,120)], close=True) +
    poly([(100,90),(34,152),(166,152)], close=True) +
    [R(90,152,110,180), L(40,180,160,180)]
)

# car：车身 + 车顶 + 车窗分隔 + 双轮 + 轮心 + 车灯
ART['car'] = [
    R(35,108,165,146),
    L(60,108,78,76), L(78,76,132,76), L(132,76,150,108),
    L(105,76,105,108),
    C(65,150,14), C(135,150,14),
    D(65,150,5), D(135,150,5),
    D(158,120,4),
]

# ---------- 词元数据（与 words/*.json 字段一致，不显示给孩子） ----------
META = {
    'apple':  dict(cn='苹果',   syllables=2, unit='unit-01', ipa='/ˈæpl/',      say='圆圆的、红红的，咬一口脆脆的',       draw='圆润果身 + 短柄 + 一片叶子',       strokes_max=24),
    'banana': dict(cn='香蕉',   syllables=3, unit='unit-01', ipa='/bəˈnɑːnə/',  say='弯弯的、黄黄的，剥开皮就能吃',       draw='两条长弧围成弯月形',               strokes_max=24),
    'cat':    dict(cn='猫',     syllables=1, unit='unit-02', ipa='/kæt/',       say='会喵喵叫，喜欢蹭你的腿',             draw='圆脸 + 两只尖耳朵 + 胡须',         strokes_max=24),
    'fish':   dict(cn='鱼',     syllables=1, unit='unit-02', ipa='/fɪʃ/',       say='住在水里，摇摇尾巴游啊游',           draw='椭圆身子 + 三角尾巴 + 泡泡',       strokes_max=24),
    'star':   dict(cn='星星',   syllables=1, unit='unit-03', ipa='/stɑː(r)/',   say='晚上天上亮晶晶，有五个角',           draw='五个角的星星',                     strokes_max=24),
    'eye':    dict(cn='眼睛',   syllables=1, unit='unit-04', ipa='/aɪ/',       say='用来看东西，会一眨一眨，指着自己的眼睛学', draw='杏仁形眼眶 + 圆瞳 + 2 根睫毛',     strokes_max=20),
    'milk':   dict(cn='牛奶',   syllables=1, unit='unit-05', ipa='/mɪlk/',      say='白白的，装在盒子里，喝了长高高',     draw='牛奶盒 + 屋顶 + 吸管',             strokes_max=24),
    'sun':    dict(cn='太阳',   syllables=1, unit='unit-06', ipa='/sʌn/',       say='白天挂在天上，暖暖的、亮亮的',       draw='一个圆 + 周围八道光芒 + 笑脸',     strokes_max=24),
    'tree':   dict(cn='树',     syllables=1, unit='unit-06', ipa='/triː/',      say='高高的，有绿绿的叶子和小果子',       draw='树干 + 三团圆树冠',               strokes_max=24),
    'car':    dict(cn='汽车',   syllables=1, unit='unit-06', ipa='/kɑː(r)/',    say='有四个轮子，嘀嘀嘀地跑',             draw='车身 + 车顶 + 两个圆轮子',         strokes_max=24),
}

ORDER = ['apple','banana','cat','fish','sun','star','milk','eye','tree','car']

# ---------- 校验 ----------
VALID_OP = {1,2,3,4}
def validate():
    errs = []
    for w in ORDER:
        s = ART[w]
        if len(s) > META[w]['strokes_max']:
            errs.append(f'{w}: {len(s)} 笔超过 strokes_max={META[w]["strokes_max"]}')
        for i, st in enumerate(s):
            if len(st) != 5: errs.append(f'{w}[{i}]: 不是 5 字节 {st}')
            elif st[0] not in VALID_OP: errs.append(f'{w}[{i}]: 非法 opcode {st[0]}')
            elif any(not isinstance(v,int) or v < 0 or v > 200 for v in st[1:4]):
                errs.append(f'{w}[{i}]: 坐标越界 {st}')
            elif not isinstance(st[4],int) or st[4] < 0 or st[4] > 200:
                errs.append(f'{w}[{i}]: 第 5 字节越界 {st}')
    if errs:
        raise SystemExit('校验失败:\n' + '\n'.join(errs))
    return True

def to_bin(strokes):
    b = bytearray(MAGIC)
    b.append(len(strokes) & 0xFF)
    for st in strokes:
        b += bytes(st)
    return bytes(b)

def to_c_array(name, data, per_line=12):
    lines = [f'static const uint8_t {name}[] = {{']
    for i in range(0, len(data), per_line):
        chunk = data[i:i+per_line]
        lines.append('    ' + ', '.join(f'0x{v:02X}' for v in chunk) + ',')
    lines.append('};')
    return '\n'.join(lines)

def main():
    validate()
    words, bins = [], []
    for w in ORDER:
        s = ART[w]; m = META[w]
        b = to_bin(s)
        bins.append((w, b))
        words.append(dict(
            id=w, word=w, ipa=m['ipa'], cn=m['cn'], syllables=m['syllables'],
            say=m['say'], draw=m['draw'], strokes_max=m['strokes_max'],
            unit=m['unit'], tags=[m['unit']],
            stroke_count=len(s), binary_size_bytes=len(b), strokes=s,
        ))

    # 1) 可读 JSON
    doc = dict(
        format='wordmode-wordlist-v1', pack='p0-fallback', version='1.0.0',
        date='2026-11-09', age=6, stage='P0',
        note='P0 内置保底包：断网/无 NAS 时可独立跑通 7 屏。10 词跨 6 单元抽样，P1 由 NAS 下发完整单元。',
        lineart=dict(format='wordline-v1', canvas=[200,200], stroke_width=3,
                     opcodes={'1':'LINE','2':'CIRCLE','3':'RECT','4':'DOT'},
                     word_binary='magic(2B WL) + stroke_count(1B) + n*5B'),
        screen_hint='屏幕不显示 word/cn/say/ipa；仅渲染 strokes。行宽 3px、单色深灰、0..200 线性映射到约 160x160 绘图区并居中于 (120,120)',
        totals=dict(words=len(words), strokes=sum(x['stroke_count'] for x in words),
                    bytes=sum(x['binary_size_bytes'] for x in words)),
        words=words,
    )
    with open(os.path.join(HERE,'p0_words.json'),'w',encoding='utf-8') as f:
        json.dump(doc, f, ensure_ascii=False, indent=2)

    # 2) C 头文件（Flash 只读映射，不占 RAM）
    h = []
    h.append('/* p0_pack.h — 由 gen_lineart.py 自动生成，请勿手改 */')
    h.append('/* 放置位置: main/assets/p0_pack.h ；static const 落在 .rodata，ESP-IDF 默认映射 Flash，不占 SRAM */')
    h.append('#pragma once')
    h.append('#include <stdint.h>')
    h.append('')
    h.append('#define WORD_P0_MAGIC0   0x57  /* W */')
    h.append('#define WORD_P0_MAGIC1   0x4C  /* L */')
    h.append('#define WORD_P0_COUNT    10')
    h.append('#define WORD_P0_STROKE_BYTES 5')
    h.append('')
    h.append('typedef struct {')
    h.append('    const char    *word;      /* TTS / 家长 / 小智用，不上屏 */')
    h.append('    const char    *say;       /* 6 岁中文讲解提示，不上屏 */')
    h.append('    const uint8_t *strokes;   /* magic2 + count1 + n*5 */')
    h.append('    uint16_t       len;       /* 整个 strokes 数组字节数 */')
    h.append('    uint8_t        stroke_count;')
    h.append('    uint8_t        syllables;')
    h.append('} word_p0_t;')
    h.append('')
    for w, b in bins:
        h.append(to_c_array(f'art_{w}', b))
        h.append('')
    h.append('static const word_p0_t kWordP0[WORD_P0_COUNT] = {')
    for w, b in bins:
        m = META[w]
        h.append(f'    {{ "{w}", "{m["say"]}", art_{w}, sizeof(art_{w}), {len(ART[w])}, {m["syllables"]} }},')
    h.append('};')
    h.append('')
    with open(os.path.join(HERE,'p0_pack.h'),'w',encoding='utf-8') as f:
        f.write('\n'.join(h))

    # 3) 二进制（4B 段数 + 每段 4B 长度 + 数据），P1 TLV 打包的参考实现
    blob = bytearray(struct.pack('<I', len(bins)))
    for w, b in bins:
        blob += struct.pack('<I', len(b)) + b
    with open(os.path.join(HERE,'p0_pack.bin'),'wb') as f:
        f.write(blob)

    print('校验通过：%d 词 / %d 笔 / %d B 笔画数据 / p0_pack.bin %d B' % (
        len(words), doc['totals']['strokes'], doc['totals']['bytes'], len(blob)))
    for w, b in bins:
        print('  %-8s %2d 笔  %3d B' % (w, len(ART[w]), len(b)))

if __name__ == '__main__':
    main()
