"""Validate audio references before conversion."""
import wave

def validate_audio(config, story, assets):
    if not isinstance(config, dict): raise ValueError('audio.json must be an object')
    ids={}
    for track in ('music','voice'):
        clips=config.get(track,{})
        if not isinstance(clips,dict): raise ValueError(f'audio.{track} must be an object')
        ids[track]=set(clips)
        for name,rel in clips.items():
            if not isinstance(name,str) or not name or not isinstance(rel,str):
                raise ValueError(f'audio.{track}: use named file paths')
            src=(assets/rel).resolve()
            if not src.is_relative_to(assets.resolve()) or src.suffix.lower()!='.wav':
                raise ValueError(f'{track}.{name}: WAV must be inside assets/')
            if not src.is_file(): raise ValueError(f'{track}.{name}: missing file {rel}')
            with wave.open(str(src),'rb') as wav:
                if wav.getnchannels() not in (1,2) or wav.getsampwidth() not in (1,2,3,4) or wav.getcomptype()!='NONE' or wav.getnframes()==0:
                    raise ValueError(f'{rel}: use nonempty mono/stereo integer PCM WAV, 8/16/24/32-bit')
    def ref(track,name,where):
        if name is not None and (not isinstance(name,str) or name not in ids[track]):
            raise ValueError(f'{where}: unknown {track} clip {name!r}')
    ref('music',config.get('title_music'),'title_music')
    ref('music',config.get('default_music'),'default_music')
    for n in story['nodes']:
        ref('music',n.get('music',config.get('default_music')),n.get('id','node'))
        ref('voice',n.get('voice'),n.get('id','node'))
    if sum(len(x) for x in ids.values())>32767: raise ValueError('Too many audio clips')
