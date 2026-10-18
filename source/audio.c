#include <gba_interrupt.h>
#include <maxmod.h>
#include "audio.h"
#include "vn_audio.h"

static bool ready;
static mm_sfxhand music_handle, voice_handle;
static int music_clip = -1;
static unsigned music_volume = 153, voice_volume = 255;

void audio_init(void) {
    mmInitDefault((mm_addr)vn_soundbank, 2);
    irqSet(IRQ_VBLANK, mmVBlank);
    ready = true;
}
static mm_sfxhand play(int id, unsigned volume) {
    mm_sound_effect effect = {{(mm_word)id}, 1024, 0, volume, 128};
    return mmEffectEx(&effect);
}
void audio_settings(const SaveData *s) {
    music_volume = s->music_enabled ? s->music_volume * 255 / 10 : 0;
    voice_volume = s->voice_enabled ? s->voice_volume * 255 / 10 : 0;
    if (!ready) return;
    if (music_handle && mmEffectActive(music_handle)) mmEffectVolume(music_handle, music_volume);
    if (voice_handle && mmEffectActive(voice_handle)) mmEffectVolume(voice_handle, voice_volume);
}
void audio_music(int clip) {
    if (!ready || music_clip == clip) return;
    if (music_handle && mmEffectActive(music_handle)) mmEffectCancel(music_handle);
    music_clip = clip;
    music_handle = clip < 0 ? 0 : play(clip, music_volume);
}
void audio_stop_voice(void) {
    if (ready && voice_handle && mmEffectActive(voice_handle)) mmEffectCancel(voice_handle);
    voice_handle = 0;
}
void audio_voice(int clip) {
    audio_stop_voice();
    if (ready && clip >= 0) voice_handle = play(clip, voice_volume);
}
bool audio_voice_active(void) {
    return ready && voice_volume && voice_handle && mmEffectActive(voice_handle);
}
void audio_frame(void) {
    if (ready) mmFrame();
}
