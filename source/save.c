#include <string.h>
#include "save.h"
#include "vn_assets.h"
#include "vn_script.h"
const char gba_save_type[] __attribute__((used)) = "SRAM_V113";
static const uint8_t magic[8]={'T','I','D','E','V','N','0','2'};
static uint32_t sequence;
static uint32_t checksum(const uint8_t *p,unsigned count) { uint32_t h=2166136261u; for(unsigned i=0;i<count;++i)h=(h^p[i])*16777619u;return h; }
static uint32_t get32(const uint8_t *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static void put32(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;++i)p[i]=v>>(8*i); }
bool save_true_unlocked(const SaveData *d) { return (d->endings&VN_TRUE_UNLOCK_MASK)!=0; }
Bookmark *save_slot(SaveData *d,unsigned slot) { return slot ? &d->extra_slots[slot-1] : &d->manual; }
void save_mark_read(SaveData *d,unsigned node) { if(node<STORY_NODE_LIMIT)d->read_nodes[node/8]|=1<<(node%8); }
bool save_is_read(const SaveData *d,unsigned node) { return node<STORY_NODE_LIMIT && (d->read_nodes[node/8]&(1<<(node%8))); }
void save_defaults(SaveData *d) {
    memset(d,0,sizeof(*d)); d->text_speed=1; d->music_volume=6; d->voice_volume=10;
    d->music_enabled=d->voice_enabled=true;
}
static void encode_mark(const Bookmark *m,uint8_t *p,bool extended) {
    p[0]=m->valid|(m->ending<<1)|(m->autoplay<<2);p[1]=m->choice;
    p[2]=m->node;p[3]=m->node>>8;p[4]=m->revealed;p[5]=m->revealed>>8;
    if(extended) { for(unsigned i=0;i<STORY_VARIABLES;++i){p[8+i*2]=m->variables[i];p[9+i*2]=m->variables[i]>>8;}
        put32(p+40,m->valid && m->node<VN_NODE_COUNT ? vn_node_keys[m->node]:0); }
}
static bool decode_mark(Bookmark *m,const uint8_t *p,const SaveData *d,bool extended) {
    if(p[0]&~7)return false;
    m->valid=p[0]&1;if(!m->valid)return true;
    m->ending=p[0]&2;m->autoplay=p[0]&4;m->choice=p[1];m->node=p[2]|(p[3]<<8);m->revealed=p[4]|(p[5]<<8);
    if(m->node>=VN_NODE_COUNT || (vn_true_nodes[m->node]&&!save_true_unlocked(d)))return false;
    const StoryNode *n=&vn_story[m->node];unsigned len=0;while(n->text[len])++len;
    if(m->revealed>len || m->choice>=(n->choice_count?n->choice_count:1))return false;
    if(m->ending && (n->ending_id<0 || !(d->endings&(1<<n->ending_id)) || m->revealed!=len))return false;
    memcpy(m->variables,vn_variable_defaults,sizeof(m->variables));
    if(extended) { if(get32(p+40)!=vn_node_keys[m->node])return false;
        for(unsigned i=0;i<STORY_VARIABLES;++i)m->variables[i]=(int16_t)(p[8+i*2]|(p[9+i*2]<<8)); }
    return true;
}
void save_encode(const SaveData *d,uint8_t r[SAVE_RECORD_SIZE]) {
    memset(r,0,SAVE_RECORD_SIZE);memcpy(r,magic,8);r[8]=5;r[9]=d->endings;r[10]=d->text_speed;r[11]=d->auto_default;
    encode_mark(&d->resume,r+12,false);encode_mark(&d->manual,r+20,false);
    r[18]=d->music_volume;r[19]=d->voice_volume;r[26]=d->music_enabled|(d->voice_enabled<<1);
    put32(r+32,vn_story_schema);put32(r+40,sequence);
    encode_mark(&d->resume,r+64,true);encode_mark(&d->manual,r+112,true);
    for(unsigned i=0;i<SAVE_SLOTS-1;++i)encode_mark(&d->extra_slots[i],r+160+i*48,true);
    memcpy(r+256,d->read_nodes,sizeof(d->read_nodes));
    put32(r+SAVE_RECORD_SIZE-4,checksum(r,SAVE_RECORD_SIZE-4));
}
bool save_decode(SaveData *d,const uint8_t r[SAVE_RECORD_SIZE]) {
    save_defaults(d);
    if(memcmp(r,magic,8) || r[8]<2 || r[8]>5)return false;
    bool extended=r[8]>=4;
    unsigned check_offset=r[8]==5?SAVE_RECORD_SIZE-4:extended?508:28;
    if(get32(r+check_offset)!=checksum(r,check_offset))return false;
    if(r[9]>7 || r[10]>2 || r[11]>1 || ((r[9]&4)&&!(r[9]&VN_TRUE_UNLOCK_MASK)))return false;
    SaveData loaded;save_defaults(&loaded);loaded.endings=r[9];loaded.text_speed=r[10];loaded.auto_default=r[11];
    if(r[8]>=3) {if(r[18]>10 || r[19]>10 || r[26]>3)return false;
        loaded.music_volume=r[18];loaded.voice_volume=r[19];loaded.music_enabled=r[26]&1;loaded.voice_enabled=r[26]&2;}
    /* Structural script edits invalidate positions; settings and unlocks survive. */
    if(extended && get32(r+32)!=vn_story_schema) { *d=loaded;return true; }
    if(!decode_mark(&loaded.resume,r+(extended?64:12),&loaded,extended) || !decode_mark(&loaded.manual,r+(extended?112:20),&loaded,extended))return false;
    if(extended) {
        for(unsigned i=0;i<SAVE_SLOTS-1;++i)if(!decode_mark(&loaded.extra_slots[i],r+160+i*48,&loaded,true))return false;
        memcpy(loaded.read_nodes,r+256,r[8]==5?sizeof(loaded.read_nodes):128);
    }
    *d=loaded;return true;
}
void save_load(SaveData *d) {
    const volatile uint8_t *ram=(const volatile uint8_t *)0x0E000000;
    uint8_t r[SAVE_RECORD_SIZE];SaveData candidate;bool found=false;sequence=0;
    save_defaults(d);
    const unsigned offsets[]={0,SAVE_JOURNAL_STRIDE,1024};
    for(unsigned copy=0;copy<3;++copy) {
        for(unsigned i=0;i<SAVE_RECORD_SIZE;++i)r[i]=ram[offsets[copy]+i];
        uint32_t seq=r[8]>=4?get32(r+40):0;
        if(save_decode(&candidate,r) && (!found || (int32_t)(seq-sequence)>0)) {*d=candidate;sequence=seq;found=true;}
    }
}
void save_write(const SaveData *d) {
    volatile uint8_t *ram=(volatile uint8_t *)0x0E000000;
    uint8_t r[SAVE_RECORD_SIZE];++sequence;save_encode(d,r);
    /* Alternate two records; a partially written record leaves the previous copy intact. */
    unsigned offset=(sequence&1)*SAVE_JOURNAL_STRIDE;ram[offset]=0;
    for(unsigned i=1;i<SAVE_RECORD_SIZE;++i)ram[offset+i]=r[i];
    ram[offset]=r[0];
}
