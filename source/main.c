#include <gba_interrupt.h>
#include <gba_input.h>
#include <gba_systemcalls.h>
#include <stdbool.h>
#include "graphics.h"
#include "save.h"
#include "vn_assets.h"
#include "audio.h"
#include "vn_audio.h"
#include "story.h"
#include "vn_script.h"
#include "animation.h"

enum Mode { TITLE, READING, ENDING, PAUSE, HISTORY, SYSTEM, EXTRA, LOAD, SAVE, CONFIRM, MESSAGE, EXITED };
enum Menu { MENU_TRUE, MENU_CONTINUE, MENU_START, MENU_LOAD, MENU_SYSTEM, MENU_EXTRA, MENU_EXIT };
static enum Mode mode = TITLE;
static int active_node = VN_START;
static unsigned revealed, total_chars, tick, wait_frames, selected_choice;
static unsigned title_selection = MENU_START, sub_selection;
static bool autoplay, replay;
static SaveData save_data;
static enum Mode message_return;
static enum UiText message_text;
static enum Mode load_return;
static enum Mode system_return;
static enum Mode pause_return;
static unsigned slot_selection, history_offset, node_frames;
static bool node_recorded, node_was_read, dialogue_hidden;
static bool visual_valid;
static unsigned visual_scene, visual_portrait;
static void record_read(void) {
    if (!node_recorded && revealed == total_chars) {
        if(!replay) save_mark_read(&save_data,active_node);
        story_history_push(active_node); node_recorded=true;
    }
}
static void begin_story(void) {
    story_reset(); dialogue_hidden=false;
    animation_finish(); visual_valid=false;
}
static void save_resume_bookmark(void);
static void notify(enum UiText text, enum Mode return_to);
static void save_current_slot(void) {
    save_resume_bookmark();
    *save_slot(&save_data,slot_selection)=save_data.resume;
    save_write(&save_data);
    notify(UI_SAVED,PAUSE);
}


static void save_resume_bookmark(void) {
    save_data.resume = (Bookmark){
        .valid = true, .ending = mode == ENDING || ((mode == PAUSE || mode == SAVE || mode == CONFIRM) && pause_return == ENDING),
        .autoplay = autoplay, .node = active_node, .revealed = revealed,
        .choice = selected_choice
    };
    story_get(save_data.resume.variables);
    save_write(&save_data);
}

static void checkpoint(void) {
    if (replay || (mode != READING && mode != ENDING)) return;
    save_resume_bookmark();
}

static void title(void) {
    checkpoint();
    animation_finish(); visual_valid=false;
    replay = false;
    mode = TITLE;
    audio_stop_voice();
    audio_music(vn_title_music);
    if (!save_true_unlocked(&save_data) && title_selection == MENU_TRUE)
        title_selection = MENU_START;
}

static void notify(enum UiText text, enum Mode return_to) {
    message_text = text;
    message_return = return_to;
    mode = MESSAGE;
}

static void enter_node(int index) {
    /* Source control commands do not require an extra button press. */
    for(unsigned hops=0;hops<VN_NODE_COUNT && vn_auto_nodes[index];++hops) {
        story_apply_node(index);
        int next=story_next(index);
        if(next<0 || next>=VN_NODE_COUNT)break;
        index=next;
    }
    const StoryNode *next = &vn_story[index];
    animation_start(visual_valid,visual_scene,visual_portrait,next->scene,next->character,&vn_transitions[index]);
    visual_valid=true; visual_scene=next->scene; visual_portrait=next->character;
    active_node = index;
    story_apply_node(index);
    node_frames=0; node_recorded=false; dialogue_hidden=false;
    node_was_read=save_is_read(&save_data,index);
    total_chars = graphics_text_length(vn_story[index].text);
    revealed = wait_frames = selected_choice = 0;
    if(vn_story[index].choice_count && !story_choice_visible(index,0)) selected_choice=1;
    mode = READING;
    audio_music(vn_node_audio[index].music);
    audio_stop_voice();
    if(!animation_active()) audio_voice(vn_node_audio[index].voice);
    checkpoint();
}

static void restore(const Bookmark *mark) {
    animation_finish();
    replay = false;
    story_set(mark->variables); story_history_clear();
    node_frames=0; node_recorded=false; dialogue_hidden=false;
    active_node = mark->node;
    visual_valid=true; visual_scene=vn_story[active_node].scene; visual_portrait=vn_story[active_node].character;
    node_was_read=save_is_read(&save_data,active_node);
    revealed = mark->revealed;
    total_chars = graphics_text_length(vn_story[active_node].text);
    selected_choice = mark->choice;
    autoplay = mark->autoplay;
    wait_frames = 0;
    mode = mark->ending ? ENDING : READING;
    audio_music(vn_node_audio[active_node].music);
    audio_voice(mark->ending ? -1 : vn_node_audio[active_node].voice);
    checkpoint();
}

static void advance(void) {
    const StoryNode *node = &vn_story[active_node];
    record_read();
    if (node->choice_count) {
        if (!story_choice_visible(active_node,selected_choice)) return;
        story_apply_choice(active_node,selected_choice);
        enter_node(node->targets[selected_choice]);
    }
    else if (story_next(active_node) >= 0) enter_node(story_next(active_node));
    else {
        mode = ENDING;
        audio_stop_voice();
        if (!replay && node->ending_id >= 0)
            save_data.endings |= 1 << node->ending_id;
        checkpoint();
    }
}

static void activate_title(void) {
    switch (title_selection) {
    case MENU_TRUE:
        if (!save_true_unlocked(&save_data)) return;
        replay = false;
        autoplay = save_data.auto_default;
        begin_story();
        enter_node(VN_TRUE_START);
        break;
    case MENU_CONTINUE:
        if (save_data.resume.valid) restore(&save_data.resume);
        else notify(UI_NO_RESUME, TITLE);
        break;
    case MENU_START:
        replay = false;
        autoplay = save_data.auto_default;
        begin_story();
        enter_node(VN_START);
        break;
    case MENU_LOAD:
        slot_selection=0; load_return=TITLE; mode=LOAD;
        break;
    case MENU_SYSTEM:
        sub_selection = 0;
        system_return = TITLE;
        mode = SYSTEM;
        break;
    case MENU_EXTRA:
        sub_selection = 0;
        mode = EXTRA;
        break;
    case MENU_EXIT:
        audio_music(-1); audio_stop_voice();
        save_write(&save_data);
        mode = EXITED;
        break;
    }
}

/* Input is separate from hardware polling so menu and save transitions can be verified. */
static void handle_input(unsigned pressed, unsigned held) {
    if(mode==READING && animation_active()) {
        if(pressed & (KEY_A|KEY_B|KEY_START|KEY_L|KEY_R)) {
            animation_finish();
            audio_voice(vn_node_audio[active_node].voice);
            /* Skip only the animation; the same A/B must not advance the dialogue. */
            if(pressed & (KEY_A|KEY_B)) return;
        } else {
            if(pressed & KEY_SELECT) { autoplay=!autoplay; wait_frames=0; }
            if(node_was_read && (held & KEY_B)) animation_finish();
            else animation_tick();
            if(!animation_active()) audio_voice(vn_node_audio[active_node].voice);
            return;
        }
    }
    if (mode == TITLE) {
        unsigned first = save_true_unlocked(&save_data) ? MENU_TRUE : MENU_CONTINUE;
        if (pressed & KEY_DOWN) title_selection = title_selection == MENU_EXIT ? first : title_selection + 1;
        if (pressed & KEY_UP) title_selection = title_selection == first ? MENU_EXIT : title_selection - 1;
        if (pressed & (KEY_A | KEY_START)) activate_title();
        return;
    }
    if (mode == MESSAGE) {
        if (pressed & (KEY_A | KEY_B | KEY_START)) mode = message_return;
        return;
    }
    if (mode == EXITED) {
        if (pressed & (KEY_A | KEY_START)) title();
        return;
    }
    if (mode == CONFIRM) {
        if(pressed & KEY_A) save_current_slot();
        else if(pressed & (KEY_B|KEY_START)) mode=SAVE;
        return;
    }
    if (mode == SAVE) {
        if(pressed & (KEY_B|KEY_START)) { mode=PAUSE; sub_selection=1; return; }
        if(pressed & KEY_DOWN) slot_selection=(slot_selection+1)%SAVE_SLOTS;
        if(pressed & KEY_UP) slot_selection=(slot_selection+SAVE_SLOTS-1)%SAVE_SLOTS;
        if(pressed & KEY_A) {
            if(save_slot(&save_data,slot_selection)->valid)mode=CONFIRM;
            else save_current_slot();
        }
        return;
    }
    if (mode == PAUSE) {
        if (pressed & KEY_B) { mode = pause_return; return; }
        if (pressed & KEY_UP) sub_selection = sub_selection ? sub_selection - 1 : 6;
        if (pressed & KEY_DOWN) sub_selection = (sub_selection + 1) % 7;
        if (pressed & KEY_A) {
            switch (sub_selection) {
            case 0: mode = pause_return; break;
            case 1:
                if (!replay) { slot_selection=0; mode=SAVE; }
                break;
            case 2:
                slot_selection=0; load_return=PAUSE; mode=LOAD;
                break;
            case 3: history_offset=0; mode = HISTORY; break;
            case 4: sub_selection = 0; system_return = PAUSE; mode = SYSTEM; break;
            case 5: autoplay = !autoplay; wait_frames = 0; break;
            case 6: if(!replay)save_resume_bookmark(); audio_music(-1); audio_stop_voice(); save_write(&save_data); mode = EXITED; break;
            }
        }
        return;
    }
    if (mode == HISTORY) {
        if (pressed & (KEY_B | KEY_START)) { mode = PAUSE; sub_selection=3; return; }
        if((pressed & KEY_UP) && history_offset+1<story_history_count())++history_offset;
        if((pressed & KEY_DOWN) && history_offset)--history_offset;
        if((pressed & KEY_A) && story_history_count())audio_voice(vn_node_audio[story_history_node(history_offset)].voice);
        return;
    }
    if (mode == SYSTEM) {
        if (pressed & (KEY_B | KEY_START)) { mode = system_return; if(mode==PAUSE)sub_selection=4; return; }
        if (pressed & KEY_DOWN) sub_selection = (sub_selection + 1) % 7;
        if (pressed & KEY_UP) sub_selection = (sub_selection + 6) % 7;
        if ((pressed & KEY_A) && sub_selection == 6) { mode = system_return; if(mode==PAUSE)sub_selection=4; return; }
        if (pressed & (KEY_LEFT | KEY_RIGHT | KEY_A)) {
            if (sub_selection == 0) save_data.text_speed =
                (save_data.text_speed + ((pressed & KEY_LEFT) ? 2 : 1)) % 3;
            else if (sub_selection == 1) save_data.auto_default = !save_data.auto_default;
            else if (sub_selection == 2 || sub_selection == 4) {
                uint8_t *volume = sub_selection == 2 ? &save_data.music_volume : &save_data.voice_volume;
                if (pressed & KEY_LEFT) { if (*volume) --*volume; }
                else if (*volume < 10) ++*volume;
            } else if (sub_selection == 3) save_data.music_enabled = !save_data.music_enabled;
            else if (sub_selection == 5) save_data.voice_enabled = !save_data.voice_enabled;
            audio_settings(&save_data);
            save_write(&save_data);
        }
        return;
    }
    if (mode == EXTRA) {
        if (pressed & (KEY_B | KEY_START)) { title(); return; }
        if (pressed & KEY_DOWN) sub_selection = (sub_selection + 1) % 3;
        if (pressed & KEY_UP) sub_selection = (sub_selection + 2) % 3;
        if ((pressed & KEY_A) && (save_data.endings & (1 << sub_selection))) {
            replay = true;
            active_node = vn_ending_nodes[sub_selection];
            audio_stop_voice();
            audio_music(vn_node_audio[active_node].music);
            mode = ENDING;
        }
        return;
    }
    if (mode == LOAD) {
        if (pressed & (KEY_B|KEY_START)) mode=load_return;
        else if(pressed & (KEY_DOWN|KEY_RIGHT))slot_selection=(slot_selection+1)%SAVE_SLOTS;
        else if(pressed & (KEY_UP|KEY_LEFT))slot_selection=(slot_selection+SAVE_SLOTS-1)%SAVE_SLOTS;
        else if ((pressed & KEY_A) && save_slot(&save_data,slot_selection)->valid) restore(save_slot(&save_data,slot_selection));
        return;
    }
    if (mode == READING && (pressed & KEY_R)) {
        slot_selection=0; load_return=READING; mode=LOAD;
        return;
    }
    if(mode==READING) record_read();
    if ((mode == READING || mode == ENDING) && (pressed & KEY_START)) {
        pause_return = mode;
        sub_selection = 0;
        mode = PAUSE;
        return;
    }
    if (pressed & KEY_START) { title(); return; }
    if ((pressed & KEY_L) && !replay) {
        checkpoint();
        save_data.manual = save_data.resume;
        save_write(&save_data);
        notify(UI_SAVED, mode);
        return;
    }
    if (mode == ENDING) {
        if (pressed & KEY_A) title();
        return;
    }
    ++node_frames;
    if(dialogue_hidden) { if(pressed & (KEY_A|KEY_B|KEY_UP))dialogue_hidden=false; return; }
    if((pressed & KEY_UP) && !vn_story[active_node].choice_count) {dialogue_hidden=true;return;}
    if (pressed & KEY_SELECT) { autoplay = !autoplay; wait_frames = 0; }
    if (revealed < total_chars) {
        if ((pressed & (KEY_A | KEY_B)) || (node_was_read && (held & KEY_B))) revealed = total_chars;
        else {
            static const unsigned intervals[3] = {4, 2, 1};
            if (tick % intervals[save_data.text_speed] == 0) ++revealed;
        }
        wait_frames = 0;
        return;
    }
    record_read();
    if(node_frames<vn_script[active_node].wait)return;
    const StoryNode *node = &vn_story[active_node];
    if (node->choice_count && (pressed & KEY_DOWN)) selected_choice = (selected_choice + 1) % node->choice_count;
    if (node->choice_count && (pressed & KEY_UP)) selected_choice = (selected_choice + node->choice_count - 1) % node->choice_count;
    if(node->choice_count && !story_choice_visible(active_node,selected_choice))selected_choice^=1;
    if (pressed & (KEY_A | KEY_B)) { advance(); return; }
    if(node_was_read && (held&KEY_B) && !node->choice_count && story_next(active_node)>=0 && tick%6==0) {advance();return;}
    if (autoplay && !node->choice_count && story_next(active_node) >= 0 && !audio_voice_active() && ++wait_frames >= 150) advance();
}

static void update(void) {
    scanKeys();
    ++tick;
    handle_input(keysDown(), keysHeld());
}

static void panel(int x, int y, int w, int h) {
    graphics_panel(x, y, w, h);
}

static void centered(int y, const uint16_t *text, unsigned color, unsigned scale) {
    graphics_text((240 - graphics_text_width(text, scale)) / 2, y, text, color, scale);
}

static void title_dialog(enum UiText heading, enum UiText hint) {
    graphics_title(save_true_unlocked(&save_data), title_selection);
    panel(8, 40, 224, 116);
    centered(47, vn_ui[heading], GOLD, 1);
    graphics_rect(20, 63, 200, 1, BORDER);
    centered(139, vn_ui[hint], MUTED, 1);
}

static void pause_dialog(void) {
    const enum UiText labels[7] = { UI_PAUSE_CONTINUE, UI_PAUSE_SAVE, UI_PAUSE_LOAD,
        UI_PAUSE_HISTORY, UI_PAUSE_SYSTEM, autoplay ? UI_AUTO_ON : UI_AUTO_OFF, UI_PAUSE_EXIT };
    const StoryNode *node = &vn_story[active_node];
    graphics_scene(node->scene, node->character);
    panel(38, 12, 164, 136);
    centered(20, vn_ui[UI_TITLE], TEAL, 1);
    for (unsigned i = 0; i < 7; ++i) {
        int y = 36 + i * 14;
        if (i == sub_selection) {
            graphics_rect(52, y - 2, 136, 14, SELECTED);
            graphics_rect(52, y - 2, 3, 14, TEAL);
        }
        graphics_text(68, y, vn_ui[labels[i]], i == sub_selection ? WHITE : MUTED, 1);
    }
    centered(137, vn_ui[UI_PAUSE_HELP], MUTED, 1);
}

static void slot_label(int y, const uint16_t *text, unsigned color) {
    uint16_t clipped[32]; unsigned i=0;
    while(text[i] && i<31) {
        clipped[i]=text[i]; clipped[i+1]=0;
        if(graphics_text_width(clipped,1)>116)break;
        ++i;
    }
    clipped[i]=0; graphics_text(100,y,clipped,color,1);
}

static void draw(void) {
    if(mode==READING && animation_active()) {
        AnimationFrame frame=animation_frame();
        graphics_animated_scene(frame.scene,frame.portrait,frame.offset_x,frame.darkness);
        return;
    }
    if (mode == TITLE) {
        graphics_title(save_true_unlocked(&save_data), title_selection);
        return;
    }
    if (mode == SYSTEM) {
        graphics_title(save_true_unlocked(&save_data), title_selection);
        panel(8, 6, 224, 148);
        centered(13, vn_ui[UI_SYSTEM], TEAL, 1);
        const enum UiText labels[] = {UI_SPEED, UI_DEFAULT_AUTO, UI_MUSIC_VOLUME, UI_MUSIC_ENABLED,
            UI_VOICE_VOLUME, UI_VOICE_ENABLED, UI_BACK};
        const enum UiText values[] = {UI_SLOW + save_data.text_speed, save_data.auto_default ? UI_ON : UI_OFF,
            UI_VOLUME_0 + save_data.music_volume, save_data.music_enabled ? UI_ON : UI_OFF,
            UI_VOLUME_0 + save_data.voice_volume, save_data.voice_enabled ? UI_ON : UI_OFF};
        for (unsigned i = 0; i < 7; ++i) {
            int y = 33 + i * 15;
            if (sub_selection == i) graphics_rect(18, y - 1, 204, 14, SELECTED);
            graphics_text(25, y, vn_ui[labels[i]], WHITE, 1);
            if (i < 6) graphics_text(169, y, vn_ui[values[i]], GOLD, 1);
        }
        centered(140, vn_ui[UI_SETTINGS_HELP], MUTED, 1);
        return;
    }
    if (mode == PAUSE) {
        pause_dialog();
        return;
    }
    if (mode == HISTORY) {
        const StoryNode *node = &vn_story[story_history_count()?story_history_node(history_offset):(unsigned)active_node];
        graphics_scene(node->scene,node->character);
        panel(8,8,224,144); centered(15,vn_ui[UI_HISTORY_TITLE],GOLD,1);
        if(story_history_count()) {
            graphics_text(16,72,node->speaker,GOLD,1);
            graphics_dialogue(node->text,graphics_text_length(node->text));
        } else centered(75,vn_ui[UI_HISTORY_EMPTY],MUTED,1);
        centered(140,vn_ui[UI_HISTORY_HELP],MUTED,1); return;
    }
    if (mode == EXTRA) {
        title_dialog(UI_EXTRA, UI_EXTRA_HELP);
        for (unsigned i = 0; i < 3; ++i) {
            int y = 73 + i * 21;
            bool unlocked = save_data.endings & (1 << i);
            if (sub_selection == i) graphics_rect(18, y - 2, 204, 18, SELECTED);
            graphics_text(29, y, unlocked ? vn_story[vn_ending_nodes[i]].ending : vn_ui[UI_LOCKED],
                          unlocked ? GOLD : MUTED, 1);
        }
        return;
    }
    if (mode == LOAD || mode == SAVE || mode == CONFIRM) {
        const StoryNode *current=&vn_story[active_node];
        if(load_return==TITLE && mode==LOAD)graphics_title(save_true_unlocked(&save_data),title_selection);
        else graphics_scene(current->scene,current->character);
        panel(8,6,224,148);
        centered(13,vn_ui[mode==LOAD?UI_LOAD:UI_SAVE_TITLE],GOLD,1);
        for(unsigned i=0;i<SAVE_SLOTS;++i) {
            int y=34+i*19;const Bookmark *mark=save_slot(&save_data,i);
            if(i==slot_selection)graphics_rect(16,y-1,208,16,SELECTED);
            graphics_text(23,y,vn_ui[UI_SLOT1+i],WHITE,1);
            slot_label(y,mark->valid?vn_story[mark->node].speaker:vn_ui[UI_EMPTY_SLOT],mark->valid?GOLD:MUTED);
        }
        const Bookmark *mark=save_slot(&save_data,slot_selection);
        if(mark->valid)graphics_dialogue(vn_story[mark->node].text,graphics_text_length(vn_story[mark->node].text));
        centered(140,vn_ui[mode==CONFIRM?UI_OVERWRITE:UI_SLOT_HELP],MUTED,1); return;
    }
    if (mode == MESSAGE || mode == EXITED) {
        if (mode == MESSAGE && message_return != TITLE) {
            const StoryNode *saved = &vn_story[active_node];
            graphics_scene(saved->scene, saved->character);
        } else graphics_title(save_true_unlocked(&save_data), title_selection);
        panel(15, 58, 210, 53);
        centered(66, vn_ui[mode == EXITED ? UI_EXITED : message_text], GOLD, 1);
        centered(91, vn_ui[mode == EXITED ? UI_EXIT_HELP : UI_DISMISS], WHITE, 1);
        return;
    }
    const StoryNode *node = &vn_story[active_node];
    if (mode == ENDING) {
        graphics_scene(node->scene, false);
        panel(18, 42, 204, 74);
        centered(50, vn_ui[UI_END], MUTED, 1);
        centered(68, node->ending, GOLD, 2);
        centered(99, vn_ui[UI_RETURN], WHITE, 1);
        return;
    }
    if(dialogue_hidden) { graphics_scene(node->scene,node->character); return; }
    graphics_reading_scene(node->scene, node->character, node->speaker);
    graphics_dialogue(node->text, revealed);
    if (revealed == total_chars) {
        if (node->choice_count) {
            panel(25, 29, 190, 54);
            unsigned row=0;
            for (unsigned i = 0; i < node->choice_count; ++i) {
                if(!story_choice_visible(active_node,i))continue;
                int y = 36 + row++ * 22;
                if (i == selected_choice) {
                    graphics_rect(30, y - 1, 180, 18, SELECTED);
                    graphics_rect(30, y - 1, 3, 18, TEAL);
                }
                graphics_text(42, y + 1, node->choices[i], i == selected_choice ? WHITE : MUTED, 1);
            }
        }
    }
}

int main(void) {
    irqInit();
    irqEnable(IRQ_VBLANK);
    save_load(&save_data);
    story_reset();
    audio_init();
    audio_settings(&save_data);
    audio_music(vn_title_music);
    if (save_true_unlocked(&save_data)) title_selection = MENU_TRUE;
    graphics_init();
    int last_mode = -1, last_node = -1;
    unsigned last_revealed = ~0u, last_choice = ~0u, last_title = ~0u, last_sub = ~0u;
    unsigned last_speed = ~0u;
    bool last_auto = false, last_default_auto = false;
    unsigned last_audio = ~0u, last_slot=~0u, last_history=~0u;
    bool last_hidden=false, last_animation=false;
    while (true) {
        audio_frame();
        update();
        unsigned audio_state = save_data.music_volume | (save_data.voice_volume << 4) |
            (save_data.music_enabled << 8) | (save_data.voice_enabled << 9);
        if (last_mode != (int)mode || last_node != active_node || last_revealed != revealed ||
            last_choice != selected_choice || last_title != title_selection || last_sub != sub_selection ||
            last_auto != autoplay || last_speed != save_data.text_speed ||
            last_default_auto != save_data.auto_default || last_audio != audio_state ||
            last_slot!=slot_selection || last_history!=history_offset || last_hidden!=dialogue_hidden ||
            animation_active() || last_animation) {
            draw();
            graphics_present();
            last_mode = mode; last_node = active_node; last_revealed = revealed;
            last_choice = selected_choice; last_title = title_selection; last_sub = sub_selection;
            last_auto = autoplay; last_speed = save_data.text_speed;
            last_default_auto = save_data.auto_default;
            last_audio = audio_state; last_slot=slot_selection; last_history=history_offset; last_hidden=dialogue_hidden;
            last_animation=animation_active();
        } else VBlankIntrWait();
    }
}
