#include "native.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { Slice data; size_t count; } Table;

static Table table(Slice s, size_t at) {
    size_t count = u32(s, at);
    if (count > INT32_MAX || count > (s.size - at - 4) / 4)
        fail("invalid pointer table at offset %zu", at);
    return (Table){section(s, at + 4, count * 4), count};
}
static size_t pointer(Table t, size_t i) { return u32(t.data, i * 4); }
static char *string_at(Slice s, size_t at) {
    uint32_t ptr = u32(s, at);
    if (!ptr) return NULL;
    if (ptr<4) fail("invalid string pointer %u",ptr);
    size_t n=u32(s,ptr-4);
    Slice text_bytes=section(s,ptr,n);
    if (memchr(text_bytes.data,0,n)) fail("embedded NUL in resource name at offset %u",ptr);
    char *text = allocate(n + 1, 1);
    memcpy(text, text_bytes.data, n);
    return text;
}
bool version_at_least(const DataWin *dw, int a, int b, int c, int d) {
    int v[4] = {a,b,c,d};
    for (int i = 0; i < 4; ++i)
        if (dw->version[i] != v[i]) return dw->version[i] > v[i];
    return true;
}
static void bump(DataWin *dw, int a, int b, int c, int d) {
    if (version_at_least(dw,a,b,c,d)) return;
    int v[4] = {a,b,c,d};
    memcpy(dw->version, v, sizeof v);
}
static void add_tiles(DataWin *dw, Slice s, size_t at) {
    if (!at) return;
    Table t = table(s, at);
    for (size_t i = 0; i < t.count; ++i) {
        size_t p = pointer(t,i);
        section(s,p,48);
        Tile tile = {version_at_least(dw,2,0,0,0), i32(s,p+8), i32(s,p+12),
                     i32(s,p+16), i32(s,p+20), i32(s,p+24)};
        size_t defs = tile.sprite ? dw->sprite_count : dw->background_count;
        if (tile.definition < 0 || (size_t)tile.definition >= defs) continue;
        bool duplicate = false;
        for (size_t j = 0; j < dw->tile_count; ++j) {
            Tile x = dw->tiles[j];
            if (x.sprite == tile.sprite && x.definition == tile.definition &&
                x.x == tile.x && x.y == tile.y && x.width == tile.width && x.height == tile.height) {
                duplicate = true; break;
            }
        }
        if (!duplicate) {
            dw->tiles = resize_array(dw->tiles,dw->tile_count+1,sizeof *dw->tiles);
            dw->tiles[dw->tile_count++] = tile;
        }
    }
}
static void rooms(DataWin *dw, Slice s, Table t) {
    if (version_at_least(dw,2,0,0,0) && !version_at_least(dw,2,2,2,302)) {
        for (size_t i = 0; i < t.count; ++i) {
            size_t p = pointer(t,i), objects = u32(s,p+48);
            Table objs = table(s,objects);
            if (objs.count >= 2) {
                if (pointer(objs,1) - pointer(objs,0) == 48) bump(dw,2,2,2,302);
                break;
            }
        }
    }
    if (version_at_least(dw,2,3,0,0) && !version_at_least(dw,2022,1,0,0)) {
        for (size_t i = 0; i < t.count; ++i) {
            size_t p = pointer(t,i), lp = u32(s,p+88), seq = u32(s,p+92);
            if (!lp) continue;
            Table layers = table(s,lp);
            if (!layers.count) continue;
            size_t first = pointer(layers,0), next = layers.count == 1 ? seq : pointer(layers,1);
            int type = i32(s,first+8);
            bool detected = false;
            if (type == 0 || type == 7) continue;
            if (type == 1) detected = next > first + 12 + 64;
            else if (type == 2) detected = next != first + 40 + (uint64_t)u32(s,first+36)*4;
            else if (type == 3) {
                size_t tile = u32(s,first+36);
                detected = tile != first+48 && tile != first+52;
            } else if (type == 4) {
                uint64_t n = (uint64_t)u32(s,first+40)*u32(s,first+44);
                detected = next != first+48+n*4;
            } else if (type == 6) detected = next != first+44+(uint64_t)u32(s,first+40)*12;
            if (detected) bump(dw,2022,1,0,0);
            break;
        }
    }
    for (size_t i = 0; i < t.count; ++i) {
        size_t p = pointer(t,i);
        section(s,p,88);
        add_tiles(dw,s,u32(s,p+52));
        if (!version_at_least(dw,2,0,0,0)) continue;
        size_t layer_at = p+88;
        if (version_at_least(dw,2024,13,0,0)) {
            size_t n = u32(s,layer_at);
            section(s,layer_at+4,n*4);
            layer_at += 4+n*4;
        }
        size_t lp = u32(s,layer_at);
        if (!lp) continue;
        Table layers = table(s,lp);
        for (size_t j = 0; j < layers.count; ++j) {
            size_t q = pointer(layers,j);
            section(s,q,36);
            int type = i32(s,q+8);
            size_t body = q+36;
            if (version_at_least(dw,2022,1,0,0)) {
                size_t n = u32(s,body+8);
                section(s,body+12,n*12);
                body += 12+n*12;
            }
            if (type == 3) add_tiles(dw,s,u32(s,body));
            else if (type != 0 && type != 1 && type != 2 && type != 4 && type != 6 && type != 7)
                fail("unsupported room layer type %d",type);
        }
    }
}

DataWin parse_datawin(Buffer source, bool audio_only, bool ps3) {
    DataWin dw = {.source=source};
    Slice s = {source.data,source.size};
    if (s.size < 8 || memcmp(s.data,"FORM",4)) fail("invalid data.win: expected FORM header");
    if (u32(s,4) != s.size-8) fail("invalid FORM length");
    /* Chunk presence must seed version detection before any records are interpreted. */
    for (size_t p = 8; p < s.size;) {
        Slice head = section(s,p,8);
        uint32_t n = u32(s,p+4);
        section(s,p+8,n);
        if (!memcmp(head.data,"FEDS",4) || !memcmp(head.data,"ACRV",4) ||
            !memcmp(head.data,"SEQN",4) || !memcmp(head.data,"TAGS",4) || !memcmp(head.data,"EMBI",4))
            bump(&dw,2,3,0,0);
        else if (!memcmp(head.data,"FEAT",4)) bump(&dw,2022,8,0,0);
        else if (!memcmp(head.data,"PSEM",4) || !memcmp(head.data,"PSYS",4)) bump(&dw,2023,4,0,0);
        else if (!memcmp(head.data,"UILR",4)) bump(&dw,2024,13,0,0);
        p += 8+(size_t)n;
    }
    for (size_t p = 8; p < s.size;) {
        const uint8_t *tag = s.data+p;
        size_t n = u32(s,p+4), start = p+8, end = start+n;
        bool gen = !audio_only && !memcmp(tag,"GEN8",4);
        bool spr = !audio_only && !ps3 && !memcmp(tag,"SPRT",4);
        bool bg = !audio_only && !ps3 && !memcmp(tag,"BGND",4);
        bool font = !audio_only && !ps3 && !memcmp(tag,"FONT",4);
        bool room = !audio_only && !ps3 && !memcmp(tag,"ROOM",4);
        bool tp = !audio_only && !memcmp(tag,"TPAG",4);
        bool tx = !audio_only && !memcmp(tag,"TXTR",4);
        bool sound = !ps3 && !audio_only && !memcmp(tag,"SOND",4);
        bool audio = !ps3 && !memcmp(tag,"AUDO",4);
        if (gen) {
            section(s,start,60);
            dw.bytecode = u8(s,start+1);
            bump(&dw,i32(s,start+44),i32(s,start+48),i32(s,start+52),i32(s,start+56));
        } else if (spr || bg || font || room || tp || tx || sound || audio) {
            Table t = table((Slice){s.data,end},start);
            if (spr) {
                if (dw.sprites) fail("duplicate SPRT chunk");
                dw.sprite_count=t.count; dw.sprites=allocate(t.count,sizeof *dw.sprites);
                for (size_t i=0;i<t.count;++i) {
                    size_t q=pointer(t,i);
                    Sprite *sp=&dw.sprites[i];
                    sp->name=string_at(s,q);
                    size_t count_at=q+56;
                    int count=i32(s,count_at);
                    if (count == -1) {
                        int v=i32(s,q+60);
                        if (!version_at_least(&dw,2,0,0,0)) fail("unsupported special sprite before GMS2");
                        count_at=q+76+(v>=2 ? 4:0)+(v>=3 ? 4:0);
                        count=i32(s,count_at);
                    }
                    if (count<0) fail("invalid sprite texture count");
                    section(s,count_at+4,(size_t)count*4);
                    sp->count=(size_t)count;
                    sp->textures=allocate(sp->count,sizeof *sp->textures);
                    for (size_t j=0;j<sp->count;++j) sp->textures[j]=u32(s,count_at+4+j*4);
                }
            } else if (bg || font) {
                NamedTexture **items=bg ? &dw.backgrounds : &dw.fonts;
                size_t *count=bg ? &dw.background_count : &dw.font_count;
                if (*items) fail("duplicate named texture chunk");
                *count=t.count; *items=allocate(t.count,sizeof **items);
                for (size_t i=0;i<t.count;++i) {
                    size_t q=pointer(t,i);
                    (*items)[i]=(NamedTexture){string_at(s,q),u32(s,q+(bg ? 16:28))};
                }
            } else if (room) rooms(&dw,s,t);
            else if (tp) {
                if (dw.tpags) fail("duplicate TPAG chunk");
                dw.tpag_count=t.count; dw.tpags=allocate(t.count,sizeof *dw.tpags);
                for (size_t i=0;i<t.count;++i) {
                    size_t q=pointer(t,i);
                    dw.tpags[i]=(Tpag){(uint32_t)q,u16(s,q),u16(s,q+2),u16(s,q+4),u16(s,q+6),
                        u16(s,q+8),u16(s,q+10),u16(s,q+12),u16(s,q+14),u16(s,q+16),u16(s,q+18),
                        (int16_t)u16(s,q+20)};
                }
            } else if (tx) {
                if (dw.textures) fail("duplicate TXTR chunk");
                bool mips=version_at_least(&dw,2,0,0,0);
                if (t.count>=2 && mips && !version_at_least(&dw,2022,9,0,0)) {
                    size_t diff=pointer(t,1)-pointer(t,0);
                    if (diff==28) bump(&dw,2022,9,0,0);
                    else if (diff==16) bump(&dw,2022,3,0,0);
                }
                size_t field=4+(mips ? 4:0)+(version_at_least(&dw,2022,3,0,0) ? 4:0)+
                             (version_at_least(&dw,2022,9,0,0) ? 12:0);
                dw.texture_count=t.count; dw.textures=allocate(t.count,sizeof *dw.textures);
                for (size_t i=0;i<t.count;++i) {
                    size_t off=u32(s,pointer(t,i)+field);
                    if (!off) continue;
                    size_t limit=end;
                    if (i+1<t.count) {
                        size_t next=u32(s,pointer(t,i+1)+field);
                        if (next) limit=next;
                    }
                    if (off>limit || limit>end) fail("invalid texture blob range");
                    dw.textures[i]=section(s,off,limit-off);
                }
            } else if (sound) {
                if (dw.sounds) fail("duplicate SOND chunk");
                dw.sound_count=t.count; dw.sounds=allocate(t.count,sizeof *dw.sounds);
                for (size_t i=0;i<t.count;++i) {
                    size_t q=pointer(t,i); uint32_t flags=u32(s,q+4);
                    dw.sounds[i]=(Sound){string_at(s,q),string_at(s,q+12),flags,
                        f32(s,q+20),f32(s,q+24),(flags&0x64)==0x64 ? i32(s,q+28):0,i32(s,q+32)};
                }
            } else if (audio) {
                if (dw.audio) fail("duplicate AUDO chunk");
                dw.audio_count=t.count; dw.audio=allocate(t.count,sizeof *dw.audio);
                for (size_t i=0;i<t.count;++i) {
                    size_t q=pointer(t,i), len=u32(s,q);
                    dw.audio[i]=section(s,q+4,len);
                }
            }
        }
        p=end;
    }
    printf("Parsed: %zu sprites, %zu backgrounds, %zu fonts, %zu pages, %zu TPAGs, %zu sounds\n",
           dw.sprite_count,dw.background_count,dw.font_count,dw.texture_count,dw.tpag_count,dw.sound_count);
    return dw;
}

int resolve_tpag(const DataWin *dw, uint32_t offset) {
    for (size_t i=0;i<dw->tpag_count;++i) if (dw->tpags[i].offset==offset) return (int)i;
    return -1;
}
Image extract_tpag(const Tpag *t, const Image *pages, size_t count) {
    Image out=new_image(t->bw>0 ? t->bw:1,t->bh>0 ? t->bh:1);
    if (t->page<0 || (size_t)t->page>=count || !t->tw || !t->th) return out;
    const Image *src=&pages[t->page];
    if (!src->pixels) return out;
    for (int y=0;y<t->th;++y) {
        int sy=t->sy+y,dy=t->ty+y;
        if (sy>=src->height || dy>=out.height) continue;
        for (int x=0;x<t->tw;++x) {
            int sx=t->sx+x,dx=t->tx+x;
            if (sx<src->width && dx<out.width) out.pixels[(size_t)dy*out.width+dx]=src->pixels[(size_t)sy*src->width+sx];
        }
    }
    return out;
}
void free_datawin(DataWin *dw) {
    for (size_t i=0;i<dw->sprite_count;++i) { free(dw->sprites[i].name); free(dw->sprites[i].textures); }
    for (size_t i=0;i<dw->background_count;++i) free(dw->backgrounds[i].name);
    for (size_t i=0;i<dw->font_count;++i) free(dw->fonts[i].name);
    for (size_t i=0;i<dw->sound_count;++i) { free(dw->sounds[i].name); free(dw->sounds[i].file); }
    free(dw->sprites); free(dw->backgrounds); free(dw->fonts); free(dw->sounds);
    free(dw->tpags); free(dw->textures); free(dw->tiles); free(dw->audio); free(dw->source.data);
    memset(dw,0,sizeof *dw);
}
