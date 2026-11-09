#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
P0 保底词包 · 预置发音生成脚本（可复现）

用 edge-tts 童声合成 10 个单词的英文发音（正常 + 慢速两版），
再用 ffmpeg 转成 16 kHz / 单声道 / Opus，约 1.5~3 KiB 一词。

  normal : 看图时自动播的那一次
  slow   : 「不记得 / 再听一次」时慢速重播

产物：
  audio/<word>.opus       正常
  audio/<word>_slow.opus  慢速
  audio/manifest.json     采样率/声道/编码/字节数/时长清单

依赖： python3.12 -m pip install edge-tts ； ffmpeg (含 libopus)
运行： /usr/bin/python3.12 gen_audio.py
"""
import json, os, subprocess, sys, glob

HERE = os.path.dirname(os.path.abspath(__file__))
AUDIO = os.path.join(HERE, 'audio')
os.makedirs(AUDIO, exist_ok=True)

PY = '/usr/bin/python3.12'          # edge-tts 装在这个解释器上
VOICE = 'en-US-AnaNeural'            # 童声，适合 6 岁
SAMPLE_RATE = 16000
BITRATE = '16k'

WORDS = ['apple','banana','cat','fish','sun','star','milk','eye','tree','car']  # v1.1 方案 A: hand -> eye
SAY = {                              # 慢速版多读一遍时可用的自然短句（可选，不生成）
    # 若要「单词 + 例句」版，把 (word, text) 换成这里即可
}


def run(cmd, timeout=90):
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    if r.returncode != 0:
        raise RuntimeError('命令失败: %s\n%s' % (' '.join(cmd), r.stderr[-400:]))
    return r


def synth(word, text, out_mp3, rate=None):
    cmd = [PY, '-m', 'edge_tts', '--voice', VOICE, '--text', text,
           '--write-media', out_mp3]
    if rate:
        cmd.append('--rate=%s' % rate)   # 追加，不能插到 --voice 与其值之间
    run(cmd)


def to_opus(src_mp3, dst_opus):
    run(['ffmpeg', '-y', '-hide_banner', '-loglevel', 'error',
         '-i', src_mp3, '-ar', str(SAMPLE_RATE), '-ac', '1',
         '-c:a', 'libopus', '-b:a', BITRATE, dst_opus])


def duration(path):
    r = run(['ffprobe', '-v', 'error', '-show_entries', 'format=duration',
             '-of', 'default=nw=1:nk=1', path])
    try:
        return round(float(r.stdout.strip()), 3)
    except ValueError:
        return None


def main():
    manifest = dict(
        format='wordmode-audio-v1', stage='P0',
        voice=VOICE, engine='edge-tts + ffmpeg/libopus',
        sample_rate=SAMPLE_RATE, channels=1, codec='opus', bitrate=BITRATE,
        note='normal=看图自动播；slow=不记得/再听一次时慢速重播。文件放 main/assets/audio/ 编进 Flash，或由 P1 备课脚本下发。',
        files={},
    )
    total = 0
    for w in WORDS:
        entry = {}
        for variant, rate in (('normal', None), ('slow', '-30%')):
            name = w if variant == 'normal' else w + '_slow'
            mp3 = os.path.join(AUDIO, '_' + name + '.mp3')
            opus = os.path.join(AUDIO, name + '.opus')
            # 增量：已有且非空的成品直接复用，重跑只补 manifest
            if not (os.path.exists(opus) and os.path.getsize(opus) > 0):
                synth(w, w, mp3, rate)
                to_opus(mp3, opus)
            size = os.path.getsize(opus)
            total += size
            entry[variant] = dict(file=name + '.opus', bytes=size,
                                  duration_s=duration(opus))
            if os.path.exists(mp3):
                os.remove(mp3)
            print('  %-14s %6d B  %s s' % (name + '.opus', size,
                                           entry[variant]['duration_s']))
        manifest['files'][w] = entry

    # 清掉探针/中间文件（去重 + 存在性判断，避免重复删除报错）
    for p in set(glob.glob(os.path.join(AUDIO, '_probe*')) +
                 glob.glob(os.path.join(AUDIO, '_*.mp3'))):
        if os.path.exists(p):
            os.remove(p)

    manifest['total_bytes'] = total
    with open(os.path.join(AUDIO, 'manifest.json'), 'w', encoding='utf-8') as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)
    print('完成：%d 词 x 2 版，合计 %d B（约 %.1f KiB）' % (len(WORDS), total, total / 1024))


if __name__ == '__main__':
    main()
