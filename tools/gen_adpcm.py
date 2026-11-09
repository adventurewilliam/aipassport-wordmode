#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
P0 保底词包 · 预置发音转 ADP1（仓库现成 IMA-ADPCM 格式）

为什么不用 Opus：P0 是纯离线模式，不启动小智语音会话。仓库已有
  services/adpcm.c  (IMA-ADPCM 4bit 解码，纯逻辑、无 FPU、可 PC 单测)
  services/audio_clip.h (ADP1 容器：12B 头 + nibble 流)
  services/audio_service.c 的 audio_service_play(pcm, bytes) 直接播 PCM16
复用这套，零新增解码器、零 25 KiB 编解码内存预留；解码只需 240B 分块缓冲，
与仓库开机音「240 字节分块读取 Flash」的做法一致。

已生成的 audio/*.opus (Ogg/Opus 16kHz mono) 保留，供 P1 的 NAS 下发 /
在线 TTS / 家长网页试听使用；P0 设备端用本脚本产出的 *.adp1 + p0_audio.h。

容器（audio_clip_hdr_t，12 字节小端）：
  u32 magic = 0x31504441 ('ADP1') / u16 sample_rate=16000 / u8 ch=1 / u8 rsv=0 / u32 num_samples
其后为 IMA-ADPCM nibble 流：1 字节 = 2 样本，低 nibble 在前。

运行： python3 gen_adpcm.py      （依赖 ffmpeg，用于把 Opus 解成 PCM16）
校验： 编码后用仓库解码器算法的 Python 复刻解回，核对样本数与状态一致性
"""
import glob, json, os, struct, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
AUDIO = os.path.join(HERE, 'audio')
SAMPLE_RATE = 16000
MAGIC = 0x31504441  # 'ADP1'

# —— 与 components/core/services/src/adpcm.c 逐值一致 ——
STEP = [
    7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,
    107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,
    724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,
    3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,
    15289,16818,18500,20350,22385,24623,27086,29794,32767]
INDEX = [-1,-1,-1,-1,2,4,6,8]*2


def clamp16(v):
    return 32767 if v > 32767 else (-32768 if v < -32768 else v)


def decode_code(st, code):
    """复刻 adpcm.c 的 decode_code：既用于编码时更新状态，也用于校验"""
    step = STEP[st[1]]
    diff = step >> 3
    if code & 4: diff += step
    if code & 2: diff += step >> 1
    if code & 1: diff += step >> 2
    pred = st[0]
    pred = pred - diff if (code & 8) else pred + diff
    st[0] = clamp16(pred)
    idx = st[1] + INDEX[code & 0x0F]
    st[1] = 0 if idx < 0 else (88 if idx > 88 else idx)
    return st[0]


def encode_sample(st, sample):
    step = STEP[st[1]]
    diff = sample - st[0]
    code = 0
    if diff < 0:
        code = 8
        diff = -diff
    if diff >= step:
        code |= 4
        diff -= step
    if diff >= step >> 1:
        code |= 2
        diff -= step >> 1
    if diff >= step >> 2:
        code |= 1
    decode_code(st, code)   # 编码器必须与解码器同步推进状态
    return code


def opus_to_pcm(path):
    r = subprocess.run(
        ['ffmpeg', '-v', 'error', '-i', path, '-f', 's16le', '-acodec', 'pcm_s16le',
         '-ar', str(SAMPLE_RATE), '-ac', '1', '-'],
        capture_output=True, timeout=60)
    if r.returncode != 0:
        raise RuntimeError('解码失败 %s: %s' % (path, r.stderr[-300:]))
    raw = r.stdout
    n = len(raw) // 2
    return list(struct.unpack('<%dh' % n, raw[:n * 2]))


def to_adp1(samples):
    st = [0, 0]
    out = bytearray()
    for i in range(0, len(samples), 2):
        lo = encode_sample(st, samples[i])
        hi = encode_sample(st, samples[i + 1]) if i + 1 < len(samples) else 0
        out.append((lo & 0x0F) | ((hi & 0x0F) << 4))   # 低 nibble 在前
    hdr = struct.pack('<IHBBI', MAGIC, SAMPLE_RATE, 1, 0, len(samples))
    return bytes(hdr + out)


def verify_adp1(blob, original):
    """用解码器算法解回，核对样本数、头字段、以及首样本后误差在合理范围"""
    magic, sr, ch, rsv, n = struct.unpack('<IHBBI', blob[:12])
    assert magic == MAGIC and sr == SAMPLE_RATE and ch == 1 and n == len(original)
    st = [0, 0]
    dec = []
    for b in blob[12:]:
        dec.append(decode_code(st, b & 0x0F))
        dec.append(decode_code(st, (b >> 4) & 0x0F))
    dec = dec[:n]
    # IMA-ADPCM 有损；平均绝对误差应在几千以内（16bit 满量程 32768）
    err = sum(abs(a - b) for a, b in zip(dec, original)) / max(1, n)
    assert err < 6000, '解码偏差过大 %.0f' % err
    return round(err, 1)


def c_array(name, data, per_line=12):
    lines = ['static const uint8_t %s[] = {' % name]
    for i in range(0, len(data), per_line):
        lines.append('    ' + ', '.join('0x%02X' % v for v in data[i:i + per_line]) + ',')
    lines.append('};')
    return '\n'.join(lines)


def main():
    manifest = json.load(open(os.path.join(AUDIO, 'manifest.json'), encoding='utf-8'))
    words = list(manifest['files'].keys())
    blobs = {}
    total = 0
    for w in words:
        for variant, key in (('normal', 'adp1'), ('slow', 'adp1_slow')):
            opus = os.path.join(AUDIO, manifest['files'][w][variant.replace('adp1_slow', 'slow').replace('adp1', 'normal')]['file'])
            samples = opus_to_pcm(opus)
            blob = to_adp1(samples)
            err = verify_adp1(blob, samples)
            fname = w if variant == 'normal' else w + '_slow'
            with open(os.path.join(AUDIO, fname + '.adp1'), 'wb') as f:
                f.write(blob)
            blobs[fname] = blob
            total += len(blob)
            manifest['files'][w][variant]['adp1'] = dict(
                file=fname + '.adp1', bytes=len(blob), samples=len(samples),
                decode_mae=err, format='ADP1/IMA-ADPCM 16kHz mono')
            print('  %-16s %6d B  %5d 样本  解码MAE=%s' % (fname + '.adp1', len(blob), len(samples), err))

    # C 头文件：全部 const，落在 .rodata / Flash 只读映射，不占 RAM
    h = ['/* p0_audio.h — 由 gen_adpcm.py 自动生成，请勿手改 */',
         '/* 放置 main/assets 或 presentation/assets；解码见 services/adpcm.c + audio_service_play() */',
         '#pragma once', '#include <stdint.h>', '',
         '#define WORD_AUDIO_MAGIC   0x31504441u  /* ADP1 */',
         '#define WORD_AUDIO_RATE    16000',
         '#define WORD_AUDIO_COUNT   %d' % len(words), '',
         'typedef struct { const uint8_t *data; uint32_t len; } word_clip_t;',
         'typedef struct { const char *word; word_clip_t normal; word_clip_t slow; } word_audio_t;', '']
    for name, blob in blobs.items():
        h.append(c_array('clip_' + name.replace('-', '_'), blob))
        h.append('')
    h.append('static const word_audio_t kWordAudio[WORD_AUDIO_COUNT] = {')
    for w in words:
        h.append('    { "%s", { clip_%s, sizeof(clip_%s) }, { clip_%s_slow, sizeof(clip_%s_slow) } },'
                 % (w, w, w, w, w))
    h.append('};')
    h.append('')
    with open(os.path.join(HERE, 'p0_audio.h'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(h))

    manifest['adpcm'] = dict(
        format='ADP1 / IMA-ADPCM', sample_rate=SAMPLE_RATE, channels=1,
        decoder='components/core/services/src/adpcm.c (adpcm_decode)',
        player='services/audio_service.c (audio_service_play, PCM16)',
        container='audio_clip_hdr_t: 12B 小端 [magic u32][rate u16][ch u8][rsv u8][num_samples u32]',
        note='P0 设备端用 .adp1；.opus 保留供 P1 NAS 下发/在线 TTS/家长网页试听。解码用 240B 分块缓冲，勿一次性展开整段 PCM。',
        total_bytes=total)
    with open(os.path.join(AUDIO, 'manifest.json'), 'w', encoding='utf-8') as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)
    print('完成：%d 词 x 2 版 ADP1 合计 %d B（约 %.1f KiB），全部解码校验通过'
          % (len(words), total, total / 1024))


if __name__ == '__main__':
    main()
