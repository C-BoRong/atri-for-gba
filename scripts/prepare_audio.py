"""Build named PCM WAV clips for Maxmod, using only Python's standard library."""
import json
import os
from pathlib import Path
import struct
import subprocess
import wave
import hashlib
from audio_schema import validate_audio

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'generated'

def convert(src, dest, loop):
    with wave.open(str(src), 'rb') as wav:
        channels, width, rate = wav.getnchannels(), wav.getsampwidth(), wav.getframerate()
        if channels not in (1, 2) or width not in (1, 2, 3, 4) or wav.getcomptype() != 'NONE':
            raise ValueError(f'{src}: use uncompressed PCM WAV, mono/stereo, 8/16/24/32-bit')
        raw = wav.readframes(wav.getnframes())
    samples = []
    for pos in range(0, len(raw), width * channels):
        values = []
        for ch in range(channels):
            value = raw[pos+ch*width:pos+(ch+1)*width]
            values.append((value[0]-128)*256 if width == 1 else int.from_bytes(value, 'little', signed=True) / (1 << (8*width-16)))
        samples.append(sum(values)/channels)
    if not samples:
        raise ValueError(f'{src}: empty audio')
    count = max(1, round(len(samples)*15768/rate))
    pcm = bytearray()
    for i in range(count):
        pos = i*rate/15768
        j = min(int(pos), len(samples)-1)
        value = samples[j] + (samples[min(j+1,len(samples)-1)]-samples[j])*(pos-j)
        pcm += struct.pack('<h', max(-32768,min(32767,round(value))))
    with wave.open(str(dest), 'wb') as wav:
        wav.setparams((1,2,15768,0,'NONE','not compressed'))
        wav.writeframes(pcm)
    if loop:
        chunk = struct.pack('<15I',0,0,round(1e9/15768),60,0,0,0,1,0,0,0,0,count-1,0,0)
        data = bytearray(dest.read_bytes())
        data += b'smpl' + struct.pack('<I',len(chunk)) + chunk
        struct.pack_into('<I',data,4,len(data)-8)
        dest.write_bytes(data)

def main(config_path=None, output=None):
    global OUT
    if output is not None: OUT = Path(output)
    OUT.mkdir(exist_ok=True)
    config_path = Path(config_path) if config_path else ROOT/'assets/audio.json'
    config = json.loads(config_path.read_text(encoding='utf-8'))
    story = json.loads((ROOT/'assets/story.json').read_text(encoding='utf-8'))
    validate_audio(config, story, ROOT/'assets')
    inputs = [Path(__file__), Path(__file__).with_name('audio_schema.py'), config_path, ROOT/'assets/story.json']
    inputs += [ROOT/'assets'/p for track in ('music','voice') for p in config.get(track,{}).values()]
    fingerprint = hashlib.sha256()
    for path in inputs: fingerprint.update(path.read_bytes())
    stamp = OUT/'audio-fingerprint.txt'
    if stamp.exists() and stamp.read_text() == fingerprint.hexdigest() and all((OUT/p).exists() for p in ('vn_audio.c','vn_audio.h','soundbank.bin')):
        print('Audio assets are up to date.')
        return
    files, ids = [], {'music':{}, 'voice':{}}
    for track in ids:
        for name, path in config.get(track,{}).items():
            src = (ROOT/'assets'/path).resolve()
            if not src.is_relative_to((ROOT/'assets').resolve()) or src.suffix.lower() != '.wav':
                raise ValueError(f'{name}: resource must be a WAV inside assets/')
            ids[track][name] = len(files)
            dest = OUT / f'clip_{len(files)}.wav'
            convert(src,dest,track=='music')
            files.append(dest)
    def music(name):
        return -1 if name is None else ids['music'][name]
    title = music(config.get('title_music'))
    default = config.get('default_music')
    rows = []
    for node in story['nodes']:
        bgm = music(node.get('music',default))
        voice = -1 if node.get('voice') is None else ids['voice'][node['voice']]
        rows.append((bgm,voice))
    if not files:
        dest = OUT/'clip_silence.wav'
        with wave.open(str(dest),'wb') as wav:
            wav.setparams((1,2,15768,0,'NONE','not compressed'))
            wav.writeframes(bytes(64))
        files.append(dest)
    kit = Path(os.environ.get('DEVKITPRO','C:/devkitPro'))
    utility = kit/'tools/bin/mmutil.exe'
    if not utility.exists(): utility = kit/'tools/bin/mmutil'
    subprocess.run([str(utility),*[str(p) for p in files],'-o'+str(OUT/'soundbank.bin'),'-h'+str(OUT/'soundbank.h')],check=True)
    bank = (OUT/'soundbank.bin').read_bytes()
    text = '''#include "vn_audio.h"
const unsigned char vn_soundbank[] __attribute__((aligned(4))) = {
'''
    text += chr(10).join(','.join(map(str,bank[i:i+32]))+',' for i in range(0,len(bank),32))+chr(10)+'};'+chr(10)
    text += 'const VnNodeAudio vn_node_audio[] = {'+','.join('{%d,%d}'%r for r in rows)+'};'+chr(10)
    text += f'const int vn_title_music = {title};'+chr(10)
    (OUT/'vn_audio.c').write_text(text)
    (OUT/'vn_audio.h').write_text('''#ifndef VN_AUDIO_H
#define VN_AUDIO_H
#include <stdint.h>
typedef struct { int16_t music, voice; } VnNodeAudio;
extern const unsigned char vn_soundbank[];
extern const VnNodeAudio vn_node_audio[];
extern const int vn_title_music;
#endif
''')
    stamp.write_text(fingerprint.hexdigest())
    print(f'Prepared audio: {len(ids["music"])} music, {len(ids["voice"])} voice clips.')

if __name__ == '__main__': main()
