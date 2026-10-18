"""Convert source artwork and a UTF-8 story to a small, indexed GBA ROM asset set."""
from __future__ import annotations
import argparse
from collections import deque
import hashlib
import json
import os
from pathlib import Path
import struct
from PIL import Image, ImageDraw, ImageFont
from story_schema import load_story, validate, portrait_settings

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'generated'
NEWLINE = 65535


def write_changed(path, content):
    data = content.encode('utf-8') if isinstance(content, str) else content
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def transparent_character(path, mode='auto'):
    source = Image.open(path)
    if mode == 'alpha' or (mode == 'auto' and (source.mode == 'RGBA' or 'transparency' in source.info)):
        rgba = source.convert('RGBA')
        return rgba.convert('RGB'), rgba.getchannel('A').point(lambda a: 255 if a >= 128 else 0)
    image = source.convert('RGB')
    w, h = image.size
    # Only remove near-white pixels connected to the image boundary.
    mask = Image.new('L', image.size, 255)
    pixels = image.load()
    seen = set()
    queue = deque([(x, y) for x in range(w) for y in (0, h-1)] +
                  [(x, y) for y in range(h) for x in (0, w-1)])
    while queue:
        x, y = queue.popleft()
        if (x, y) in seen or not (0 <= x < w and 0 <= y < h):
            continue
        seen.add((x, y))
        if min(pixels[x, y]) < 245:
            continue
        mask.putpixel((x, y), 0)
        queue.extend(((x-1,y), (x+1,y), (x,y-1), (x,y+1)))
    return image, mask


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--font', default=os.environ.get('GBA_FONT', 'C:/Windows/Fonts/simsun.ttc'))
    args = parser.parse_args()
    font_path = Path(args.font)
    if not font_path.exists():
        raise SystemExit('Chinese font missing. Set GBA_FONT to a CJK TrueType font.')
    story_path = ROOT / 'assets/story.json'
    story = load_story(ROOT)
    portrait_defs = story.get('portraits') or [dict(story['character'], id='default')]
    portrait_ids = {p['id']: i+1 for i,p in enumerate(portrait_defs)}
    if 'character' not in story: story['character'] = dict(portrait_defs[0], name=portrait_defs[0].get('name','角色'))
    # Bake only used positions/orientations into lightweight asset variants. Pixel
    # data is shared for equal file/matte/flip combinations, independent of position.
    variants = [dict(p, **portrait_settings(p)) for p in portrait_defs]
    variant_ids = {(p['id'],p['x'],p['y'],p['flip_x']):i+1 for i,p in enumerate(variants)}
    node_portraits=[]
    for node in story['nodes']:
        c=node.get('character')
        if not c:
            node_portraits.append(0); continue
        base=portrait_defs[0] if c is True else portrait_defs[portrait_ids[c]-1]
        pose=portrait_settings(base,node)
        key=(base['id'],pose['x'],pose['y'],pose['flip_x'])
        if key not in variant_ids:
            variants.append(dict(base,**pose));variant_ids[key]=len(variants)
        node_portraits.append(variant_ids[key])
    if len(variants)>65534: raise ValueError('At most 65534 portrait variants including position/flip combinations')
    ui_path = ROOT / 'assets/ui.json'
    title_ui = json.loads(ui_path.read_text(encoding='utf-8'))
    for rel in [*title_ui['backgrounds'],title_ui['buttons']]:
        asset=(ROOT/'assets'/rel).resolve()
        if not asset.is_relative_to((ROOT/'assets').resolve()) or not asset.is_file():
            raise ValueError('UI asset must exist inside assets/: '+rel)
    inputs = [Path(__file__), story_path, ui_path, font_path]
    inputs += [ROOT / 'assets' / s['file'] for s in story['scenes']]
    inputs += [ROOT/'assets'/p['file'] for p in portrait_defs]
    inputs.append(Path(__file__).with_name('story_schema.py'))
    inputs += [ROOT / 'assets' / p for p in title_ui['backgrounds']]
    inputs.append(ROOT / 'assets' / title_ui['buttons'])
    fingerprint = hashlib.sha256()
    for p in inputs:
        fingerprint.update(p.read_bytes())
    digest = fingerprint.hexdigest()
    OUT.mkdir(exist_ok=True)
    stamp = OUT / 'fingerprint.txt'
    if stamp.exists() and stamp.read_text() == digest and all((OUT / n).exists() for n in
            ('vn_assets.c', 'vn_assets.h', 'preview.png', 'title-preview.png')):
        print('Assets are up to date.')
        return

    ids = {node['id']: i for i, node in enumerate(story['nodes'])}
    scenes = {scene['id']: i for i, scene in enumerate(story['scenes'])}
    assert len(ids) == len(story['nodes']), 'Duplicate node ID'
    assert len(scenes) == len(story['scenes']), 'Duplicate scene ID'
    assert story['start'] in ids
    assert story['true_start'] in ids
    ui = ['A 继续', 'B 快进', '上下选择  A 确认', 'START 开始', 'START 返回标题',
          'SELECT 自动', '自动播放', '手动播放', '终章', '序章  夏日港口', '选择你的回答',
          story['title'], story['subtitle'],
          '系统设置', '文字速度', '慢速', '标准', '快速', '默认自动', '关闭', '开启',
          '返回', '上下选择  左右调整', '结局回看', '未解锁', 'A 回看  B 返回',
          '没有继续记录', '没有手动存档', 'A / B 返回', '存档已保存',
          '示例已退出', 'A / START 返回标题', '读取存档', 'A 读取  B 返回',
          'L 保存  START 标题', '继续游戏', '保存', '读档', '历史记录', '设置',
          '退出', 'START 菜单  A 确认  B 返回', '历史记录', '上下翻阅  A 重听  B 返回',
          '音乐音量', '音乐开关', '人声音量', '人声开关'] + [str(i*10)+'%' for i in range(11)] + ['存档一','存档二','存档三','空槽','保存进度','上下选择  A 确认  B 返回','覆盖此存档？ A 是  B 否','暂无历史记录']
    strings = list(ui)
    for node in story['nodes']:
        assert node['scene'] in scenes
        strings += [node['speaker'], node['text'], node.get('ending', '')]
        choices = node.get('choices', [])
        assert len(choices) <= 2, 'This demo supports two choices per node'
        assert sum(bool(node.get(k)) for k in ('next', 'choices', 'ending', 'branches')) == 1
        for target in [node.get('next')] + [c['next'] for c in choices]:
            assert target is None or target in ids, f'Unknown target: {target}'
        strings += [c['text'] for c in choices]
    normal_ids = validate(story, ROOT/'assets')
    endings = sorted((n['ending_id'], ids[n['id']]) for n in story['nodes'] if n.get('ending'))
    assert [i for i, _ in endings] == [0, 1, 2], 'Three unique ending IDs (0, 1, 2) required'
    assert all(story['nodes'][ids[n]]['ending_id'] < 2 for n in normal_ids
               if story['nodes'][ids[n]].get('ending')), 'Normal routes must not unlock the true ending'
    charset = sorted(set(''.join(strings)) - {'\n'})
    glyph_ids = {c: i+1 for i, c in enumerate(charset)}
    font = ImageFont.truetype(str(font_path), 12)
    widths = [0] + [6 if ord(c) < 128 else 12 for c in charset]
    glyphs = [[0]*12]
    for c in charset:
        im = Image.new('1', (12, 12))
        ImageDraw.Draw(im).text((0, -1), c, font=font, fill=1)
        glyphs.append([sum((1 << x) for x in range(12) if im.getpixel((x,y))) for y in range(12)])
    # Validate layout so a modified script cannot silently overflow the dialogue box.
    for node in story['nodes']:
        assert sum(widths[glyph_ids[c]] for c in node['speaker']) <= 200, 'Speaker name too wide'
        for choice in node.get('choices', []):
            assert sum(widths[glyph_ids[c]] for c in choice['text']) <= 170, 'Choice label too wide'
        if node.get('ending'):
            assert sum(widths[glyph_ids[c]] for c in node['ending']) * 2 <= 196, 'Ending title too wide'
        x, lines = 0, 1
        for c in node['text']:
            if c == '\n':
                x, lines = 0, lines+1
            else:
                width = widths[glyph_ids[c]]
                if x + width > 216:
                    x, lines = 0, lines+1
                x += width
        assert lines <= 3, f'Dialogue exceeds three lines: {node["id"]}'
    backgrounds = [Image.open(ROOT/'assets'/s['file']).convert('RGB').resize((240,160), Image.Resampling.LANCZOS)
                   for s in story['scenes']]
    portraits = [transparent_character(ROOT/'assets'/p['file'], p.get('matte','auto')) for p in variants]
    portraits = [(im.transpose(Image.Transpose.FLIP_LEFT_RIGHT),mask.transpose(Image.Transpose.FLIP_LEFT_RIGHT))
                 if p['flip_x'] else (im,mask) for p,(im,mask) in zip(variants,portraits)]
    for definition, (im, _) in zip(variants, portraits):
        assert im.width % 2 == 0 and im.width <= 240 and im.height <= 160, 'Portrait must have even width and fit 240x160'
        assert 0 <= definition['x'] <= 240-im.width and 0 <= definition['y'] <=160-im.height, 'Portrait is outside screen'
    portrait, mask = portraits[0]
    # Pose variants must not reweight the shared palette or duplicate source art.
    palette_portraits=[transparent_character(ROOT/'assets'/p['file'],p.get('matte','auto')) for p in portrait_defs]
    atlas = Image.new('RGB', (240, 160*(len(backgrounds)+len(palette_portraits))))
    for i, bg in enumerate(backgrounds): atlas.paste(bg,(0,160*i))
    for i,(im,_) in enumerate(palette_portraits): atlas.paste(im,(0,160*(len(backgrounds)+i)))
    master = atlas.quantize(colors=239, method=Image.Quantize.MEDIANCUT)
    palette = master.getpalette()
    palette_rgb = [(0,0,0)] + [tuple(palette[i*3:i*3+3]) for i in range(239)]
    # UI colors follow the supplied title artwork: cool navy surfaces, cyan edges,
    # warm name accents, and a clear cyan selection bar.
    ui_colors = [(31,68,96), (245,250,255), (255,214,150), (173,205,220),
                 (82,174,201), (34,104,139), (8,24,40), (112,226,226)]
    palette_rgb += ui_colors + [(0,0,0)]*8
    indexed = []
    for bg in backgrounds:
        q = bg.quantize(palette=master, dither=Image.Dither.FLOYDSTEINBERG)
        indexed.append(bytes(v+1 for v in q.tobytes()))
    portrait_datas=[]
    for im, alpha in portraits:
        pq=im.quantize(palette=master,dither=Image.Dither.NONE)
        portrait_datas.append(bytes(v+1 if a else 0 for v,a in zip(pq.tobytes(),alpha.tobytes())))
    portrait_data=portrait_datas[0]
    # Title artwork uses its own palette, keeping the original button styles crisp.
    title_backgrounds = [Image.open(ROOT/'assets'/p).convert('RGB') for p in title_ui['backgrounds']]
    assert len(title_backgrounds) == 2 and all(im.size == (240, 160) for im in title_backgrounds)
    sheet = Image.open(ROOT/'assets'/title_ui['buttons']).convert('RGB')
    bw, bh = title_ui['button_width'], title_ui['button_height']
    assert bw % 2 == 0 and len(title_ui['rows']) == 7 and len(title_ui['columns']) == 2
    buttons, button_masks = [], []
    for y in title_ui['rows']:
        for x in title_ui['columns']:
            assert x >= 0 and y >= 0 and x+bw <= sheet.width and y+bh <= sheet.height
            button = sheet.crop((x, y, x+bw, y+bh))
            # BMP uses pure white as a color key; internal blue lettering is preserved.
            alpha = Image.new('L', button.size)
            pixels = list(zip(*[iter(button.tobytes())]*3))
            alpha.putdata([0 if p == (255, 255, 255) else 255 for p in pixels])
            buttons.append(button)
            button_masks.append(alpha)
    tx, ty, spacing = title_ui['menu_x'], title_ui['menu_y'], title_ui['menu_spacing']
    assert tx >= 0 and tx+bw <= 240 and ty >= 0 and ty+6*spacing+bh <= 160
    title_atlas = Image.new('RGB', (240, 160*2+bh*len(buttons)))
    for i, bg in enumerate(title_backgrounds):
        title_atlas.paste(bg, (0, i*160))
    for i, button in enumerate(buttons):
        title_atlas.paste(button, (0, 320+i*bh))
    title_master = title_atlas.quantize(colors=239, method=Image.Quantize.MEDIANCUT)
    title_palette = title_master.getpalette()
    title_rgb = [(0,0,0)] + [tuple(title_palette[i*3:i*3+3]) for i in range(239)]
    title_rgb += ui_colors + [(0,0,0)]*8
    title_indexed = [bytes(v+1 for v in bg.quantize(palette=title_master,
                     dither=Image.Dither.FLOYDSTEINBERG).tobytes()) for bg in title_backgrounds]
    button_indexed = []
    for button, alpha in zip(buttons, button_masks):
        q = button.quantize(palette=title_master, dither=Image.Dither.NONE)
        button_indexed.append(bytes(v+1 if a else 0 for v,a in zip(q.tobytes(), alpha.tobytes())))
    header = '''/* Generated by scripts/prepare_assets.py. Do not edit. */
#ifndef VN_ASSETS_H
#define VN_ASSETS_H
#include <stdint.h>
typedef struct {
    const uint16_t *speaker, *text, *ending;
    const uint16_t *choices[2];
    int16_t next, targets[2];
    uint16_t scene, character;
    uint8_t choice_count;
    int8_t ending_id;
} StoryNode;
extern const uint16_t vn_palette[256];
extern const uint16_t vn_title_palette[256];
extern const uint8_t vn_panel_blend[256], vn_title_panel_blend[256];
extern const uint16_t *const vn_title_backgrounds[2];
extern const uint16_t *const vn_title_buttons[7][2];
extern const uint16_t vn_ending_nodes[3];
extern const uint8_t vn_true_nodes[];
extern const uint8_t vn_auto_nodes[];
extern const uint16_t *const vn_backgrounds[];
extern const uint16_t vn_portrait[];
typedef struct { const uint16_t *pixels; uint16_t w,h,x,y; } PortraitAsset;
extern const PortraitAsset vn_portraits[];
extern const uint16_t vn_glyphs[][12];
extern const uint8_t vn_widths[];
extern const uint16_t *const vn_ui[];
extern const StoryNode vn_story[];
'''
    header += f'#define VN_START {ids[story["start"]]}\n#define VN_NODE_COUNT {len(ids)}\n'
    header += f'#define VN_TRUE_START {ids[story["true_start"]]}\n'
    header += f'#define VN_TRUE_UNLOCK_MASK {sum(1<<i for i in set(story.get("true_unlock_endings",[0,1])))}\n'
    header += f'#define VN_BUTTON_W {bw}\n#define VN_BUTTON_H {bh}\n'
    header += f'#define VN_MENU_X {tx}\n#define VN_MENU_Y {ty}\n#define VN_MENU_SPACING {spacing}\n'
    header += f'#define VN_PORTRAIT_W {portrait.width}\n#define VN_PORTRAIT_H {portrait.height}\n'
    header += f'#define VN_PORTRAIT_X {variants[0]["x"]}\n#define VN_PORTRAIT_Y {variants[0]["y"]}\n#endif\n'
    code = ['/* Generated; source images and dialogue live in assets/. */', '#include "vn_assets.h"']

    def array(name, values, ctype='uint16_t', public=False):
        scope = 'const' if public else 'static const'
        code.append(f'{scope} {ctype} {name}[] __attribute__((aligned(4))) = {{')
        values = list(values)
        for i in range(0,len(values),24):
            code.append(','.join(str(v) for v in values[i:i+24]) + ',')
        code.append('};')

    array('vn_palette', [(r>>3)|((g>>3)<<5)|((b>>3)<<10) for r,g,b in palette_rgb], public=True)
    array('vn_title_palette', [(r>>3)|((g>>3)<<5)|((b>>3)<<10) for r,g,b in title_rgb], public=True)
    def panel_map(colors):
        # Match the actual 15-bit display colors; 55% blue glass over 45% scene.
        colors = [tuple((v >> 3) * 8 for v in rgb) for rgb in colors]
        result = []
        for rgb in colors:
            target = tuple(round(v * .45 + tint * .55) for v,tint in zip(rgb, (16,64,112)))
            result.append(min(range(1,248), key=lambda i: sum((a-b)**2 for a,b in zip(colors[i],target))))
        return bytes(result)
    scene_panel_map = panel_map(palette_rgb)
    array('vn_panel_blend', scene_panel_map, 'uint8_t', True)
    array('vn_title_panel_blend', panel_map(title_rgb), 'uint8_t', True)
    for i, data in enumerate(title_indexed):
        array(f'title_background_{i}', struct.unpack('<19200H', data))
    code.append('const uint16_t *const vn_title_backgrounds[2] = {title_background_0,title_background_1};')
    for i, data in enumerate(button_indexed):
        array(f'title_button_{i}', struct.unpack('<' + str(len(data)//2) + 'H', data))
    code.append('const uint16_t *const vn_title_buttons[7][2] = {' +
                ','.join('{title_button_' + str(i*2) + ',title_button_' + str(i*2+1) + '}' for i in range(7)) + '};')
    array('vn_ending_nodes', [node for _, node in endings], public=True)
    array('vn_true_nodes', [int(n['id'] not in normal_ids) for n in story['nodes']], 'uint8_t', True)
    array('vn_auto_nodes', [int(n.get('auto_advance',False)) for n in story['nodes']], 'uint8_t', True)
    for i, data in enumerate(indexed):
        array(f'background_{i}', struct.unpack('<19200H', data))
    code.append('const uint16_t *const vn_backgrounds[] = {' + ','.join(f'background_{i}' for i in range(len(indexed))) + '};')
    array('vn_portrait', struct.unpack('<' + str(len(portrait_data)//2) + 'H', portrait_data), public=True)
    data_names={}; portrait_names=[]
    for i,data in enumerate(portrait_datas):
        shape=portraits[i][0].size
        key=(shape,data)
        if key not in data_names:
            name=f'portrait_{i}'; data_names[key]=name
            array(name,struct.unpack('<'+str(len(data)//2)+'H',data))
        portrait_names.append(data_names[key])
    code.append('const PortraitAsset vn_portraits[] = {{0,0,0,0,0},'+','.join('{%s,%d,%d,%d,%d}'%(name,im.width,im.height,d['x'],d['y']) for name,d,(im,_) in zip(portrait_names,variants,portraits))+'};')
    code.append('const uint16_t vn_glyphs[][12] = {' + ',\n'.join('{'+','.join(map(str,g))+'}' for g in glyphs) + '};')
    array('vn_widths', widths, 'uint8_t', True)

    def text(name, value):
        array(name, [NEWLINE if c=='\n' else glyph_ids[c] for c in value] + [0])

    for i, value in enumerate(ui):
        text(f'ui_{i}', value)
    code.append('const uint16_t *const vn_ui[] = {' + ','.join(f'ui_{i}' for i in range(len(ui))) + '};')
    rows = []
    for i,node in enumerate(story['nodes']):
        text(f'speaker_{i}', node['speaker'])
        text(f'text_{i}', node['text'])
        text(f'ending_{i}', node.get('ending',''))
        choice_names, targets = [], []
        for j,choice in enumerate(node.get('choices', [])):
            name = f'choice_{i}_{j}'
            text(name, choice['text'])
            choice_names.append(name)
            targets.append(ids[choice['next']])
        count = len(choice_names)
        choice_names += ['0']*(2-count)
        targets += [-1]*(2-count)
        rows.append('{'+f'speaker_{i},text_{i},ending_{i},'+'{'+','.join(choice_names)+'},'+
                    f'{ids.get(node.get("next"),-1)},'+'{'+','.join(map(str,targets))+'},'+
                    f'{scenes[node["scene"]]},{node_portraits[i]},{count},{node.get("ending_id",-1)}'+'}')
    code.append('const StoryNode vn_story[] = {' + ',\n'.join(rows) + '};')
    write_changed(OUT/'vn_assets.h', header)
    write_changed(OUT/'vn_assets.c', '\n'.join(code)+'\n')
    # Preview converted colors, matte removal and actual packed Chinese font.
    flat_palette = [v for rgb in palette_rgb for v in rgb]
    preview = Image.frombytes('P',(240,160),indexed[0])
    preview.putpalette(flat_palette)
    preview = preview.convert('RGB')
    p = Image.frombytes('P',portrait.size,portrait_data)
    p.putpalette(flat_palette)
    preview.paste(p.convert('RGB'), (variants[0]['x'],variants[0]['y']),mask)
    def preview_panel(im, x, y, w, h):
        def inside(px, py, width, height, radius):
            if px < 0 or py < 0 or px >= width or py >= height: return False
            dx = radius-px if px < radius else px-(width-radius-1) if px >= width-radius else 0
            dy = radius-py if py < radius else py-(height-radius-1) if py >= height-radius else 0
            return dx*dx + dy*dy <= radius*radius
        indices = {rgb:i for i,rgb in enumerate(palette_rgb)}
        for row in range(h):
            for col in range(w):
                if not inside(col,row,w,h,5): continue
                rgb = im.getpixel((x+col,y+row))
                color = palette_rgb[scene_panel_map[indices[rgb]]] if inside(col-1,row-1,w-2,h-2,4) else ui_colors[4]
                im.putpixel((x+col,y+row),color)
    preview_panel(preview,8,87,sum(widths[glyph_ids[c]] for c in story['character']['name'])+20,14)
    preview_panel(preview,8,103,224,54)
    d = ImageDraw.Draw(preview)
    d.text((14,88),story['character']['name'],font=font,fill=ui_colors[2])
    d.text((12,105),'你终于来了。',font=font,fill=ui_colors[1])
    d.text((12,118),'我还以为，你已经忘记这个地方了。',font=font,fill=ui_colors[1])
    preview.resize((960,640),Image.Resampling.NEAREST).save(OUT/'preview.png')
    # A contact sheet of the exact converted pixels; these are asset previews, not emulator captures.
    title_preview = Image.new('RGB', (480, 160))
    for unlocked in range(2):
        im = Image.frombytes('P', (240,160), title_indexed[unlocked])
        im.putpalette([v for rgb in title_rgb for v in rgb])
        im = im.convert('RGB')
        selected = 0 if unlocked else 2
        for row in range(0 if unlocked else 1, 7):
            index = row*2 + int(row == selected)
            btn = Image.frombytes('P', (bw,bh), button_indexed[index])
            btn.putpalette([v for rgb in title_rgb for v in rgb])
            im.paste(btn.convert('RGB'), (tx, ty+row*spacing), button_masks[index])
        title_preview.paste(im, (240*unlocked,0))
    title_preview.resize((1440,480),Image.Resampling.NEAREST).save(OUT/'title-preview.png')
    stamp.write_text(digest)
    print(f'Prepared {len(indexed)} backgrounds, transparent portrait, {len(charset)} glyphs, {len(ids)} story nodes.')


if __name__ == '__main__':
    main()
