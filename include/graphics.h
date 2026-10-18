#ifndef GRAPHICS_H
#define GRAPHICS_H
#include <stdint.h>
#include <stdbool.h>
enum Color { PANEL = 240, WHITE, GOLD, MUTED, BORDER, SELECTED, SHADOW, TEAL };
enum UiText {
    UI_NEXT, UI_FAST, UI_CHOOSE, UI_START, UI_RETURN, UI_AUTO_KEY,
    UI_AUTO_ON, UI_AUTO_OFF, UI_END, UI_CHAPTER, UI_QUESTION, UI_TITLE, UI_SUBTITLE,
    UI_SYSTEM, UI_SPEED, UI_SLOW, UI_NORMAL, UI_QUICK, UI_DEFAULT_AUTO, UI_OFF, UI_ON,
    UI_BACK, UI_SETTINGS_HELP, UI_EXTRA, UI_LOCKED, UI_EXTRA_HELP, UI_NO_RESUME,
    UI_NO_SAVE, UI_DISMISS, UI_SAVED, UI_EXITED, UI_EXIT_HELP, UI_LOAD, UI_LOAD_HELP,
    UI_SAVE_HELP, UI_PAUSE_CONTINUE, UI_PAUSE_SAVE, UI_PAUSE_LOAD,
    UI_PAUSE_HISTORY, UI_PAUSE_SYSTEM, UI_PAUSE_EXIT, UI_PAUSE_HELP,
    UI_HISTORY_TITLE, UI_HISTORY_HELP, UI_MUSIC_VOLUME, UI_MUSIC_ENABLED,
    UI_VOICE_VOLUME, UI_VOICE_ENABLED, UI_VOLUME_0,
    UI_SLOT1=UI_VOLUME_0+11, UI_SLOT2, UI_SLOT3, UI_EMPTY_SLOT, UI_SAVE_TITLE, UI_SLOT_HELP, UI_OVERWRITE, UI_HISTORY_EMPTY
};
void graphics_init(void);
void graphics_title(bool unlocked, unsigned selected);
void graphics_scene(unsigned scene, unsigned portrait);
void graphics_animated_scene(unsigned scene, unsigned portrait, int offset_x, unsigned darkness);
void graphics_rect(int x, int y, int width, int height, unsigned char color);
void graphics_panel(int x, int y, int width, int height);
void graphics_reading_scene(unsigned scene, unsigned portrait, const uint16_t *speaker);
void graphics_text(int x, int y, const uint16_t *text, unsigned color, unsigned scale);
int graphics_text_width(const uint16_t *text, unsigned scale);
unsigned graphics_text_length(const uint16_t *text);
void graphics_dialogue(const uint16_t *text, unsigned revealed);
void graphics_present(void);
#endif
