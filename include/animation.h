#ifndef ANIMATION_H
#define ANIMATION_H
#include <stdbool.h>
#include <stdint.h>

/* Generated story settings use these numeric values. */
enum BackgroundMotion { BG_CUT, BG_FADE };
enum PortraitMotion { PORTRAIT_CUT, PORTRAIT_LEFT, PORTRAIT_RIGHT };
typedef struct {
    uint8_t background_in, background_out, character_in, character_out;
    uint8_t frames;
} Transition;
typedef struct {
    unsigned scene, portrait, darkness;
    int offset_x;
} AnimationFrame;

void animation_start(bool previous, unsigned old_scene, unsigned old_portrait,
                     unsigned new_scene, unsigned new_portrait, const Transition *settings);
bool animation_active(void);
void animation_tick(void);
void animation_finish(void);
AnimationFrame animation_frame(void);
#endif
