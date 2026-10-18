#ifndef STORY_H
#define STORY_H
#include <stdbool.h>
#include <stdint.h>
#define STORY_VARIABLES 16
#define STORY_NODE_LIMIT 16384
#define STORY_HISTORY 64
void story_reset(void);
void story_apply_node(unsigned node);
bool story_choice_visible(unsigned node, unsigned choice);
void story_apply_choice(unsigned node, unsigned choice);
int story_next(unsigned node);
void story_get(int16_t values[STORY_VARIABLES]);
void story_set(const int16_t values[STORY_VARIABLES]);
void story_history_clear(void);
void story_history_push(unsigned node);
unsigned story_history_count(void);
unsigned story_history_node(unsigned newest_offset);
#endif
