#include "animation.h"
#include "vn_assets.h"

static Transition config;
static unsigned from_scene, from_portrait, to_scene, to_portrait;
static unsigned phase = 4, elapsed;
static bool enabled[4];

static void next_phase(void) {
    while (phase < 4 && !enabled[phase]) ++phase;
}

void animation_start(bool previous, unsigned old_scene, unsigned old_portrait,
                     unsigned new_scene, unsigned new_portrait, const Transition *settings) {
    config = *settings;
    from_scene = old_scene; from_portrait = old_portrait;
    to_scene = new_scene; to_portrait = new_portrait;
    bool background_changed = !previous || old_scene != new_scene;
    bool character_changed = background_changed || old_portrait != new_portrait;
    enabled[0] = previous && character_changed && old_portrait && config.character_out;
    enabled[1] = previous && background_changed && config.background_out;
    enabled[2] = background_changed && config.background_in;
    enabled[3] = character_changed && new_portrait && config.character_in;
    phase = elapsed = 0;
    next_phase();
}

bool animation_active(void) { return phase < 4; }
void animation_finish(void) { phase = 4; }
void animation_tick(void) {
    if (phase >= 4) return;
    /* Present both endpoints; never divide by zero, even with a disabled config. */
    if (++elapsed > config.frames) { elapsed = 0; ++phase; next_phase(); }
}

static int offscreen(unsigned portrait, unsigned direction) {
    const PortraitAsset *p = &vn_portraits[portrait];
    return direction == PORTRAIT_LEFT ? -(int)(p->x + p->w) : 240 - (int)p->x;
}

AnimationFrame animation_frame(void) {
    AnimationFrame frame = {to_scene, to_portrait, 0, 0};
    unsigned duration = config.frames ? config.frames : 1;
    if (phase == 0) {
        frame.scene = from_scene; frame.portrait = from_portrait;
        frame.offset_x = offscreen(from_portrait, config.character_out) * (int)elapsed / (int)duration;
    } else if (phase == 1) {
        frame.scene = from_scene;
        /* If no portrait exit was requested, fade the old composition together. */
        frame.portrait = enabled[0] ? 0 : from_portrait;
        frame.darkness = 16 * elapsed / duration;
    } else if (phase == 2) {
        frame.portrait = enabled[3] ? 0 : to_portrait;
        frame.darkness = 16 - 16 * elapsed / duration;
    } else if (phase == 3) {
        frame.offset_x = offscreen(to_portrait, config.character_in) * (int)(duration - elapsed) / (int)duration;
    }
    return frame;
}
