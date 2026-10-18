#include <string.h>
#include "story.h"
#include "vn_script.h"
static int16_t values[STORY_VARIABLES];
static uint16_t history[STORY_HISTORY];
static unsigned history_head, history_count;
static bool compare(const ScriptCondition *c) {
    if (c->var < 0) return true;
    int a=values[c->var], b=c->value;
    switch(c->op) { case 0:return a==b; case 1:return a!=b; case 2:return a>b;
        case 3:return a>=b; case 4:return a<b; default:return a<=b; }
}
static void effects(unsigned start, unsigned count) {
    for(unsigned i=0;i<count;++i) {
        const ScriptEffect *e=&vn_effects[start+i];
        int n=e->add ? values[e->var]+e->value : e->value;
        values[e->var]=n < -32768 ? -32768 : n > 32767 ? 32767 : n;
    }
}
void story_reset(void) { memcpy(values,vn_variable_defaults,sizeof(values)); story_history_clear(); }
void story_get(int16_t out[STORY_VARIABLES]) { memcpy(out,values,sizeof(values)); }
void story_set(const int16_t in[STORY_VARIABLES]) { memcpy(values,in,sizeof(values)); }
void story_apply_node(unsigned node) { const ScriptNode *n=&vn_script[node]; effects(n->effect_start,n->effect_count); }
bool story_choice_visible(unsigned node,unsigned choice) { return compare(&vn_script[node].choices[choice]); }
void story_apply_choice(unsigned node,unsigned choice) { const ScriptNode *n=&vn_script[node]; effects(n->choice_start[choice],n->choice_count[choice]); }
int story_next(unsigned node) {
    const ScriptNode *n=&vn_script[node];
    for(unsigned i=0;i<n->branch_count;++i) { const ScriptBranch *b=&vn_branches[n->branch_start+i]; if(compare(&b->condition))return b->target; }
    return n->fallback;
}
void story_history_clear(void) { history_head=history_count=0; }
void story_history_push(unsigned node) {
    history[history_head]=node; history_head=(history_head+1)%STORY_HISTORY;
    if(history_count<STORY_HISTORY)++history_count;
}
unsigned story_history_count(void) { return history_count; }
unsigned story_history_node(unsigned offset) { return history[(history_head+STORY_HISTORY-1-offset)%STORY_HISTORY]; }
