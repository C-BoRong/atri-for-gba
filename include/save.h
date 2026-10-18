#ifndef SAVE_H
#define SAVE_H
#include <stdbool.h>
#include <stdint.h>
#include "story.h"
#define SAVE_RECORD_SIZE 4096
#define SAVE_JOURNAL_STRIDE 4096
#define SAVE_SLOTS 3
typedef struct {
    bool valid, ending, autoplay;
    uint16_t node, revealed;
    uint8_t choice;
    int16_t variables[STORY_VARIABLES];
} Bookmark;
typedef struct {
    uint8_t endings, text_speed;
    bool auto_default;
    uint8_t music_volume, voice_volume;
    bool music_enabled, voice_enabled;
    Bookmark resume, manual, extra_slots[SAVE_SLOTS-1];
    uint8_t read_nodes[STORY_NODE_LIMIT/8];
} SaveData;
void save_defaults(SaveData *data);
bool save_decode(SaveData *data, const uint8_t record[SAVE_RECORD_SIZE]);
void save_encode(const SaveData *data, uint8_t record[SAVE_RECORD_SIZE]);
void save_load(SaveData *data);
void save_write(const SaveData *data);
bool save_true_unlocked(const SaveData *data);
Bookmark *save_slot(SaveData *data, unsigned slot);
void save_mark_read(SaveData *data, unsigned node);
bool save_is_read(const SaveData *data, unsigned node);
#endif
