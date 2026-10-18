"""Report ROM and static RAM usage after a successful build."""
import argparse, json, os, re, subprocess, wave
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--rom',type=Path,default=ROOT/'dist/galgame-framework.gba')
    rom=parser.parse_args().rom.resolve(); elf=rom.with_suffix('.elf')
    kit=Path(os.environ.get('DEVKITPRO','C:/devkitPro'))
    tool=kit/'devkitARM/bin/arm-none-eabi-size.exe'
    if not tool.exists(): tool=tool.with_suffix('')
    result=subprocess.run([str(tool),'-A',str(elf)],capture_output=True,text=True,check=True)
    sections=[]
    for line in result.stdout.splitlines():
        m=re.match(r'^([.][\w.]+)\s+(\d+)\s+(\d+)\s*$',line)
        if m: sections.append({'section':m[1],'bytes':int(m[2]),'address':int(m[3])})
    # Address high-water marks account for alignment/padding between static sections.
    def used(base,capacity):
        return max([s['address']+s['bytes']-base for s in sections if base<=s['address']<base+capacity]+[0])
    ram={'ewram_static_bytes':used(0x02000000,262144),'ewram_limit_bytes':262144,
         'iwram_static_bytes':used(0x03000000,32768),'iwram_limit_bytes':32768}
    story=json.loads((ROOT/'assets/story.json').read_text(encoding='utf-8'))
    audio=json.loads((ROOT/'assets/audio.json').read_text(encoding='utf-8'))
    seconds={}
    for track in ('music','voice'):
        seconds[track]=0
        for rel in audio.get(track,{}).values():
            with wave.open(str(ROOT/'assets'/rel),'rb') as f: seconds[track]+=f.getnframes()/f.getframerate()
    errors=[]; warnings=[]; rom_bytes=rom.stat().st_size
    if rom_bytes>32*1024*1024: errors.append('ROM exceeds the 32 MiB GBA cartridge limit')
    if rom_bytes>28*1024*1024: warnings.append('ROM exceeds 28 MiB; leave room for later assets')
    for name in ('ewram','iwram'):
        if ram[name+'_static_bytes']>ram[name+'_limit_bytes']: errors.append(name.upper()+' static allocation exceeds capacity')
    if 262144-ram['ewram_static_bytes']<32768: warnings.append('Less than 32 KiB EWRAM remains for the Maxmod heap and runtime allocations')
    report={'rom_bytes':rom_bytes,'rom_limit_bytes':32*1024*1024,'ram':ram,
        'nodes':len(story['nodes']),'variables':len(story.get('variables',{})),
        'scenes':len(story['scenes']),'portraits':len(story.get('portraits') or [story['character']]),
        'audio_clips':{k:len(audio.get(k,{})) for k in ('music','voice')},'audio_seconds':seconds,
        'soundbank_bytes':(ROOT/'generated/soundbank.bin').stat().st_size,
        'note':'RAM counts static sections only. Heap/stack and Maxmod runtime allocation need additional space.',
        'sections':sections,'warnings':warnings,'errors':errors}
    out=ROOT/'generated'; out.mkdir(exist_ok=True)
    (out/'resource-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    lines=['# 资源容量报告','',f'- ROM：{rom_bytes:,} 字节（{rom_bytes/1024:.1f} KiB），上限 32 MiB。',
        f'- EWRAM 静态占用：{ram["ewram_static_bytes"]:,} / 262,144 字节。',
        f'- IWRAM 静态占用：{ram["iwram_static_bytes"]:,} / 32,768 字节。',
        '- 内存统计为静态段地址范围；剩余空间还要容纳堆、栈和 Maxmod 动态分配，不等同于可添加图片的预算。',
        f'- 剧情 {report["nodes"]} 节点，变量 {report["variables"]} 个，背景 {report["scenes"]} 张，立绘 {report["portraits"]} 张。',
        f'- 音乐 {report["audio_clips"]["music"]} 段，人声 {report["audio_clips"]["voice"]} 段。音频库 {report["soundbank_bytes"]:,} 字节。',
        '', '## 静态段', '', '| 段 | 字节 | 地址 |','| --- | ---: | --- |']
    lines += [f'| {s["section"]} | {s["bytes"]:,} | 0x{s["address"]:08X} |' for s in sections if s['address']>=0x02000000]
    lines += ['', '## 检查结果', '']+[f'- 错误：{x}' for x in errors]+[f'- 提醒：{x}' for x in warnings]
    if not errors and not warnings: lines+=['容量检查通过。']
    (out/'resource-report.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    print(f'Resource report: ROM {rom_bytes:,} bytes; EWRAM {ram["ewram_static_bytes"]:,}/262144; IWRAM {ram["iwram_static_bytes"]:,}/32768')
    for item in warnings: print('Warning: '+item)
    if errors: raise SystemExit('; '.join(errors))
if __name__=='__main__':main()
