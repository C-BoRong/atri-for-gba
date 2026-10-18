"""Compile variable effects, conditions and stable node keys."""
import hashlib
import json
from pathlib import Path
from story_schema import load_story, OPS, transition_settings
ROOT=Path(__file__).resolve().parents[1]
def main():
    story=load_story(ROOT); out=ROOT/'generated'; out.mkdir(exist_ok=True)
    nodes=story['nodes']; ids={n['id']:i for i,n in enumerate(nodes)}
    variables=story.get('variables',{}); varids={k:i for i,k in enumerate(variables)}
    def condition(c):
        return '{-1,0,0}' if c is None else '{%d,%d,%d}'%(varids[c['var']],OPS[c.get('op','==')],c['value'])
    effects=[]; branches=[]; rows=[]; transitions=[]
    def add_effects(owner):
        start=len(effects)
        for op in ('set','add'):
            for key,value in owner.get(op,{}).items(): effects.append('{%d,%d,%d}'%(varids[key],op=='add',value))
        return start,len(effects)-start
    for n in nodes:
        t = transition_settings(story, n)
        motions = {'none':0, 'fade':1, 'slide_left':1, 'slide_right':2}
        transitions.append('{%d,%d,%d,%d,%d}' % tuple(
            [motions[t[k]] for k in ('background_in','background_out','character_in','character_out')] + [t['frames']]))
        start,count=add_effects(n); choices=n.get('choices',[])
        choice_effects=[add_effects(c) for c in choices]+[(0,0)]*(2-len(choices))
        conds=[condition(c.get('if')) for c in choices]+[condition(None)]*(2-len(choices))
        bs=len(branches)
        for b in n.get('branches',[]): branches.append('{%s,%d}'%(condition(b),ids[b['next']]))
        rows.append('{%d,%d,%d,%d,%d,%d,{%s},{%s},{%s}}'%(start,count,bs,len(branches)-bs,ids.get(n.get('else',n.get('next')),-1),n.get('wait',0),','.join(conds),','.join(str(e[0]) for e in choice_effects),','.join(str(e[1]) for e in choice_effects)))
    if len(effects)>65535: raise ValueError("Too many effects; reduce total set/add entries below 65536")
    keys=[int.from_bytes(hashlib.sha256(n['id'].encode()).digest()[:4],'little') for n in nodes]
    if len(set(keys))!=len(keys): raise ValueError('Node ID hash collision: rename one node')
    schema=int.from_bytes(hashlib.sha256(json.dumps([list(variables),[n['id'] for n in nodes]],ensure_ascii=False).encode()).digest()[:4],'little')
    header='''#ifndef VN_SCRIPT_H
#define VN_SCRIPT_H
#include <stdint.h>
#include "animation.h"
typedef struct { int8_t var; uint8_t op; int16_t value; } ScriptCondition;
typedef struct { uint8_t var, add; int16_t value; } ScriptEffect;
typedef struct { ScriptCondition condition; int16_t target; } ScriptBranch;
typedef struct { uint16_t effect_start,effect_count,branch_start,branch_count; int16_t fallback; uint16_t wait; ScriptCondition choices[2]; uint16_t choice_start[2],choice_count[2]; } ScriptNode;
extern const ScriptNode vn_script[];
extern const ScriptEffect vn_effects[];
extern const ScriptBranch vn_branches[];
extern const int16_t vn_variable_defaults[16];
extern const uint32_t vn_node_keys[],vn_story_schema;
extern const Transition vn_transitions[];
#endif
'''
    code='#include "vn_script.h"'+chr(10)
    for ctype,name,items,fallback in [('ScriptNode','vn_script',rows,'{0}'),('ScriptEffect','vn_effects',effects,'{0}'),('ScriptBranch','vn_branches',branches,'{{-1,0,0},0}'),('int16_t','vn_variable_defaults',[str(v) for v in variables.values()]+['0']*(16-len(variables)),'0'),('uint32_t','vn_node_keys',[str(k)+'u' for k in keys],'0')]:
        code+=f'const {ctype} {name}[] = {{'+','.join(items or [fallback])+'};'+chr(10)
    code+='const Transition vn_transitions[] = {'+','.join(transitions)+'};'+chr(10)
    code+=f'const uint32_t vn_story_schema={schema}u;'+chr(10)
    for name,data in [('vn_script.h',header),('vn_script.c',code)]:
        path=out/name
        if not path.exists() or path.read_text()!=data:path.write_text(data)
    print(f'Validated {len(nodes)} nodes, {len(variables)} variables, {len(effects)} effects, {len(branches)} branches.')
if __name__=='__main__':main()
