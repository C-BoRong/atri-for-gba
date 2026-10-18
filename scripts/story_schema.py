"""Validate the authoring format before converting any images or sound."""
import json
from pathlib import Path

MAX_NODES = 16384
MAX_VARIABLES = 16
OPS = {'==': 0, '!=': 1, '>': 2, '>=': 3, '<': 4, '<=': 5}
TRANSITION_DEFAULTS = {'background_in':'none', 'background_out':'none',
                       'character_in':'none', 'character_out':'none', 'frames':16}

def portrait_settings(portrait, node=None):
    """Each node starts with the asset defaults, never the previous node's pose."""
    result = {k:portrait.get(k, False if k=='flip_x' else 0) for k in ('x','y','flip_x')}
    for key, value in result.items():
        if key == 'flip_x':
            if type(value) is not bool: raise ValueError('portrait.flip_x must be a boolean')
        elif type(value) is not int or not 0 <= value < (240 if key=='x' else 160):
            raise ValueError('portrait.'+key+' must be an on-screen integer coordinate')
    if node is not None:
        for key in result:
            field='character_'+key
            if field in node:
                value=node[field]
                if key=='flip_x':
                    if type(value) is not bool: raise ValueError(field+' must be a boolean')
                elif type(value) is not int or not 0 <= value < (240 if key=='x' else 160):
                    raise ValueError(field+' must be an on-screen integer coordinate')
                result[key]=value
    return result

def transition_settings(story, node):
    """Root defaults, then node overrides; false disables all effects on that node."""
    result = dict(TRANSITION_DEFAULTS)
    for owner in (story, node):
        value = owner.get('transition', {})
        if value is False:
            result = dict(TRANSITION_DEFAULTS)
        elif isinstance(value, dict):
            result.update(value)
        else:
            raise ValueError('transition must be an object or false')
        if set(result) != set(TRANSITION_DEFAULTS):
            raise ValueError('Unknown transition field')
        for field in ('background_in','background_out'):
            if result[field] not in ('none','fade'): raise ValueError(field+': use none/fade')
        for field in ('character_in','character_out'):
            if result[field] not in ('none','slide_left','slide_right'): raise ValueError(field+': use none/slide_left/slide_right')
        if type(result['frames']) is not int or not 1 <= result['frames'] <= 120:
            raise ValueError('transition.frames must be an integer in 1..120')
    return result

def load_story(root):
    story = json.loads((Path(root)/'assets/story.json').read_text(encoding='utf-8'))
    validate(story, Path(root)/'assets')
    return story

def validate(story, assets):
    def require(ok, message):
        if not ok: raise ValueError(message)
    def file(path):
        resolved = (assets/path).resolve()
        require(resolved.is_relative_to(assets.resolve()), f'Asset outside assets/: {path}')
        require(resolved.is_file(), f'Missing asset: {path}')
    nodes = story.get('nodes', [])
    transition_settings(story, {})
    require(0 < len(nodes) <= MAX_NODES, 'Use 1..16384 story nodes')
    ids = {n['id'] for n in nodes}
    require(len(ids) == len(nodes), 'Duplicate story node ID')
    require(story['start'] in ids and story['true_start'] in ids, 'Missing entry node')
    variables = story.get('variables', {})
    unlock=story.get('true_unlock_endings',[0,1])
    require(isinstance(unlock,list) and bool(unlock) and all(type(i) is int and i in (0,1) for i in unlock), 'true_unlock_endings must contain normal ending IDs 0/1')
    require(len(variables) <= MAX_VARIABLES, 'At most 16 variables')
    for name, value in variables.items():
        require(isinstance(value,int) and not isinstance(value,bool) and -32768 <= value <= 32767, f'Bad initial variable: {name}')
    scenes = {s['id'] for s in story['scenes']}
    require(len(scenes)==len(story['scenes']) and len(scenes)<=65535, 'Duplicate/too many scenes')
    for scene in story['scenes']: file(scene['file'])
    portraits = story.get('portraits', [dict(story.get('character', {}), id='default')])
    require(0 < len(portraits) <= 65534, 'Use 1..65534 portrait definitions')
    portrait_ids = {p['id'] for p in portraits}
    require(len(portrait_ids)==len(portraits), 'Duplicate portrait ID')
    for portrait in portraits:
        file(portrait['file'])
        portrait_settings(portrait)
        require(portrait.get('matte', 'auto') in ('auto','white','alpha'), 'matte must be auto, white or alpha')
    def condition(c, where):
        require(isinstance(c,dict) and c.get('var') in variables, f'{where}: unknown condition variable')
        require(c.get('op','==') in OPS, f'{where}: bad comparison operator')
        require(type(c.get('value')) is int and -32768 <= c['value'] <=32767, f'{where}: bad condition value')
    def effects(owner, where):
        for op in ('set','add'):
            require(isinstance(owner.get(op,{}),dict), f'{where}: {op} must be an object')
            for var, value in owner.get(op,{}).items():
                require(var in variables and type(value) is int and -32768<=value<=32767, f'{where}: invalid {op} variable/value: {var}')
    graph = {}
    for n in nodes:
        where = n['id']
        transition_settings(story, n)
        require(n['scene'] in scenes, f'{where}: unknown scene')
        c = n.get('character',False)
        require(isinstance(c,bool) or c is None or c in portrait_ids, f'{where}: unknown character {c}')
        if c:
            portrait = portraits[0] if c is True else next(p for p in portraits if p['id']==c)
            portrait_settings(portrait, n)
        else:
            require(not any(k in n for k in ('character_x','character_y','character_flip_x')),
                    f'{where}: position/flip requires a visible character')
        require(isinstance(n.get('speaker'),str) and isinstance(n.get('text'),str), f'{where}: speaker/text must be strings')
        require(type(n.get('auto_advance',False)) is bool, f'{where}: auto_advance must be boolean')
        require(not n.get('auto_advance') or (not n['text'] and not n.get('choices') and not n.get('ending')), f'{where}: auto_advance requires an empty control page')
        require(0 <= n.get('wait',0) <= 3600 and type(n.get('wait',0)) is int, f'{where}: wait must be 0..3600 frames')
        require(sum(bool(n.get(k)) for k in ('next','choices','ending','branches'))==1, f'{where}: select one of next/choices/ending/branches')
        effects(n,where)
        targets=[]
        if n.get('next'): targets.append(n['next'])
        if n.get('branches'):
            require(1<=len(n['branches'])<=8 and n.get('else'), f'{where}: branches need 1..8 conditions and else')
            targets.append(n['else'])
            for branch in n['branches']:
                condition(branch,where); targets.append(branch['next'])
        choices=n.get('choices',[])
        require(len(choices)<=2, f'{where}: at most two visible choices')
        for choice in choices:
            require(isinstance(choice.get('text'),str) and bool(choice['text']), f'{where}: choice text must be a nonempty string')
            effects(choice,where)
            if 'if' in choice: condition(choice['if'],where)
            targets.append(choice['next'])
        require(not choices or any('if' not in c for c in choices), f'{where}: provide an unconditional fallback choice')
        for target in targets: require(target in ids, f'{where}: missing target {target}')
        graph[where]=targets
    # Interactive loops are valid; traversal must be iterative and cycle-safe.
    def reachable(start):
        seen=set(); pending=[start]
        while pending:
            n=pending.pop()
            if n in seen: continue
            seen.add(n); pending.extend(graph[n])
        return seen
    normal=reachable(story['start']); true=reachable(story['true_start'])
    require(normal|true==ids, 'Unreachable nodes: '+', '.join(sorted(ids-normal-true)))
    endings=sorted(n.get('ending_id',-1) for n in nodes if n.get('ending'))
    require(endings==[0,1,2], 'This title layout requires ending_id 0, 1, 2 exactly once')
    require(all(n.get('ending_id',-1)<2 for n in nodes if n['id'] in normal), 'Normal entry must not reach true ending')
    automatic={n['id'] for n in nodes if n.get('auto_advance')}
    done=set()
    for start in automatic:
        visiting=set(); stack=[(start,False)]
        while stack:
            current,leave=stack.pop()
            if leave: visiting.remove(current);done.add(current);continue
            if current in done or current not in automatic:continue
            require(current not in visiting,'Automatic control cycle: '+current)
            visiting.add(current);stack.append((current,True))
            stack.extend((target,False) for target in graph[current] if target in automatic)
    return normal
