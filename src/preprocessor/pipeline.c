#include "native.h"
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *name, *key;
    Image image;
    int tpag, crop_x, crop_y, crop_w, crop_h;
} Asset;
typedef struct { Asset *items; size_t count; } Assets;
typedef struct { int atlas, x, y; } Location;

static bool starts(const char *s, const char *prefix) { return !strncmp(s,prefix,strlen(prefix)); }
static const char *name_or(const char *s, const char *prefix, size_t index, char **owned) {
    if (s) { *owned=NULL; return s; }
    *owned=format_string("%s%zu",prefix,index);
    return *owned;
}
static char *sprite_key(const char *name) {
    const char *prefixes[] = {
        "spr_floweyx_tv","spr_tvinside","spr_lefteye_","spr_flipeye_","spr_floweyx_mouth",
        "spr_floweyx_dimple","spr_mouthball","spr_mouthbeam","spr_mouthflash","spr_floweyx_fleshmound",
        "spr_fleshmound","spr_nostrils","spr_dentata_","spr_halfdentata_","spr_floweyx_flame",
        "spr_floweynuke","spr_venus_placeholder","spr_bgpipe","spr_pipepart","spr_sidestalk",
        "spr_vines_flowey","spr_floweyarm","spr_tv_warning","spr_tv_exface","spr_tv_floweyface",
        "spr_tv_floweyx_","spr_noise"
    };
    for (size_t i=0;i<sizeof prefixes/sizeof *prefixes;++i)
        if (starts(name,prefixes[i])) return copy_string("spr/_omega_flowey");
    return format_string("spr/%s",name);
}
static int max_dimension(const char *name, int size) {
    const struct { const char *name; int size; } rules[] = {
        {"spr_sidestalk",16},{"spr_mouthball_",32},{"spr_fa_seq_b_",64},{"spr_floweyarm",64},
        {"spr_fa_stemunder_",64},{"spr_floweynuke_explosion",16},{"spr_floweyx_flame_move_",16},
        {"spr_venus_placeholder",32},{"spr_f_handgun_neo_",32},{"spr_vines_flowey",32},
        {"spr_floweyx_flamethrower",64},{"spr_floweynuke",32},{"spr_bgpipe",64},
        {"spr_tvinside_old",64},{"spr_floweyx_tv",128},{"spr_floweyx_fleshmound",64},
        {"spr_tv_floweyx_laugh",64},{"spr_floweyx_mouthedge",64}
    };
    if (!starts(name,"spr/")) return size;
    for (size_t i=0;i<sizeof rules/sizeof *rules;++i)
        if (starts(name+4,rules[i].name)) return rules[i].size;
    return size;
}
static Image subimage(const Image *src,int x,int y,int w,int h) {
    if (x<0 || y<0 || w<1 || h<1 || x>src->width-w || y>src->height-h)
        fail("invalid subimage rectangle");
    Image out=new_image(w,h);
    for (int j=0;j<h;++j)
        memcpy(out.pixels+(size_t)j*w,src->pixels+(size_t)(y+j)*src->width+x,(size_t)w*4);
    return out;
}
static void debug_png(const Options *o,const char *dir,const char *name,const Image *img) {
    if (!o->debug) return;
    if (!*name || !strcmp(name,".") || !strcmp(name,"..") || strchr(name,'/') || strchr(name,'\\'))
        fail("unsafe debug image name '%s'",name);
    char *directory=join_path(o->debug,dir);
    mkdirs(directory);
    char *filename=format_string("%s.png",name);
    char *path=join_path(directory,filename);
    Image rendered=new_image(img->width,img->height);
    for (size_t i=0;i<(size_t)img->width*img->height;++i)
        rendered.pixels[i]=img->pixels[i]>>24 ? img->pixels[i]:0;
    save_png(path,&rendered);
    free_image(&rendered);
    free(directory); free(filename); free(path);
}
static void debug_tpag(const Options *o,const char *dir,const char *name,
                       const Tpag *t,const Image *pages,size_t count) {
    if (!o->debug) return;
    Image out=new_image(t->bw>0?t->bw:1,t->bh>0?t->bh:1);
    if (t->page>=0 && (size_t)t->page<count && pages[t->page].pixels && t->tw && t->th && t->sw && t->sh) {
        const Image *src=&pages[t->page];
        for (int y=0;y<t->th && t->ty+y<out.height;++y) {
            int sy=t->sy+(int)(((int64_t)y*2+1)*t->sh/(2*t->th));
            if (sy>=src->height) continue;
            for (int x=0;x<t->tw && t->tx+x<out.width;++x) {
                int sx=t->sx+(int)(((int64_t)x*2+1)*t->sw/(2*t->tw));
                if (sx<src->width)
                    out.pixels[(size_t)(t->ty+y)*out.width+t->tx+x]=src->pixels[(size_t)sy*src->width+sx];
            }
        }
    }
    debug_png(o,dir,name,&out);
    free_image(&out);
}
static void add_asset(Assets *assets,char *name,char *key,Image image,int tpag) {
    assets->items=resize_array(assets->items,assets->count+1,sizeof *assets->items);
    assets->items[assets->count++]=(Asset){name,key,image,tpag,0,0,image.width,image.height};
}
static void prepare_asset(Asset *a,int atlas_size) {
    Image *img=&a->image;
    if (starts(a->name,"spr/")) {
        int minx=img->width,miny=img->height,maxx=-1,maxy=-1;
        for (int y=0;y<img->height;++y) for (int x=0;x<img->width;++x)
            if (img->pixels[(size_t)y*img->width+x]>>24) {
                if (x<minx) minx=x;
                if (y<miny) miny=y;
                if (x>maxx) maxx=x;
                if (y>maxy) maxy=y;
            }
        Image cropped;
        if (maxx<0) { cropped=new_image(1,1); minx=0; miny=0; }
        else cropped=subimage(img,minx,miny,maxx-minx+1,maxy-miny+1);
        free_image(img); *img=cropped;
        a->crop_x=minx; a->crop_y=miny; a->crop_w=img->width; a->crop_h=img->height;
    }
    int max=max_dimension(a->name,atlas_size);
    if (img->width>max || img->height>max) {
        double sx=(double)max/img->width,sy=(double)max/img->height,scale=sx<sy?sx:sy;
        int w=(int)(img->width*scale),h=(int)(img->height*scale);
        if (w<1) w=1;
        if (h<1) h=1;
        Image resized=new_image(w,h);
        for (int y=0;y<h;++y) for (int x=0;x<w;++x)
            resized.pixels[(size_t)y*w+x]=img->pixels[(size_t)((int64_t)y*img->height/h)*img->width+(int64_t)x*img->width/w];
        free_image(img); *img=resized;
    }
}
static int tile_tpag(const DataWin *dw,const Tile *t) {
    if (t->sprite) {
        const Sprite *s=&dw->sprites[t->definition];
        return s->count ? resolve_tpag(dw,s->textures[0]):-1;
    }
    return resolve_tpag(dw,dw->backgrounds[t->definition].texture);
}
static char *tile_name(const DataWin *dw,const Tile *t,bool key) {
    const char *n=t->sprite ? dw->sprites[t->definition].name:dw->backgrounds[t->definition].name;
    char *fallback=NULL;
    if (!n) { fallback=format_string("%s%d",t->sprite ? "spr":"bg",t->definition); n=fallback; }
    char *out=key ? format_string("tile/%s",n):
        format_string("tile/%s_%d_%d_%dx%d",n,t->x,t->y,t->width,t->height);
    free(fallback);
    return out;
}
static Assets collect(const Options *o,const DataWin *dw,const Image *pages) {
    Assets a={0};
    if (o->debug) {
        const char *dirs[]={"sprites","backgrounds","fonts","tiles","atlas_debug"};
        for (size_t i=0;i<5;++i) { char *path=join_path(o->debug,dirs[i]); mkdirs(path); free(path); }
    }
    for (size_t i=0;i<dw->sprite_count;++i) {
        const Sprite *sp=&dw->sprites[i];
        char *owned;
        const char *n=name_or(sp->name,"sprite_",i,&owned);
        for (size_t j=0;j<sp->count;++j) {
            int index=resolve_tpag(dw,sp->textures[j]);
            if (index<0) continue;
            Image img=extract_tpag(&dw->tpags[index],pages,dw->texture_count);
            char *name=sp->count>1 ? format_string("%s_%zu",n,j):copy_string(n);
            debug_tpag(o,"sprites",name,&dw->tpags[index],pages,dw->texture_count);
            char *full=format_string("spr/%s",name);
            add_asset(&a,full,sprite_key(n),img,index);
            free(name);
        }
        free(owned);
    }
    for (int which=0;which<2;++which) {
        const NamedTexture *items=which ? dw->fonts:dw->backgrounds;
        size_t count=which ? dw->font_count:dw->background_count;
        for (size_t i=0;i<count;++i) {
            int index=resolve_tpag(dw,items[i].texture);
            if (index<0) continue;
            char *owned;
            const char *n=name_or(items[i].name,which?"font_":"bg_",i,&owned);
            Image img=extract_tpag(&dw->tpags[index],pages,dw->texture_count);
            debug_tpag(o,which?"fonts":"backgrounds",n,&dw->tpags[index],pages,dw->texture_count);
            char *name=format_string("%s/%s",which?"font":"bg",n);
            add_asset(&a,name,copy_string(name),img,index);
            free(owned);
        }
    }
    for (size_t i=0;i<dw->tile_count;++i) {
        const Tile *t=&dw->tiles[i];
        int index=tile_tpag(dw,t);
        if (index<0 || t->x<0 || t->y<0 || t->width<=0 || t->height<=0) continue;
        Image source=extract_tpag(&dw->tpags[index],pages,dw->texture_count);
        if (t->x>=source.width || t->y>=source.height) { free_image(&source); continue; }
        int w=t->width<source.width-t->x ? t->width:source.width-t->x;
        int h=t->height<source.height-t->y ? t->height:source.height-t->y;
        Image img=subimage(&source,t->x,t->y,w,h);
        free_image(&source);
        char *name=tile_name(dw,t,false);
        debug_png(o,"tiles",name+5,&img);
        add_asset(&a,name,tile_name(dw,t,true),img,-1);
    }
    for (size_t i=0;i<a.count;++i) prepare_asset(&a.items[i],o->atlas_size);
    return a;
}
static void output(const Options *o,const char *name,Buffer *buffer) {
    char *path=join_path(o->output,name);
    write_file(path,buffer->data,buffer->size);
    printf("Wrote %zu bytes to %s\n",buffer->size,path);
    free(path); free(buffer->data); *buffer=(Buffer){0};
}
static int group_row(const Groups *groups,int id) {
    int row4=0,row8=0;
    for (size_t i=0;i<groups->count;++i) {
        ClutGroup *g=&groups->items[i];
        int row=g->bpp==4 ? row4++:row8++;
        if (g->id==id) return row;
    }
    fail("missing CLUT group %d",id);
}
static void clut_files(const Options *o,const Groups *groups) {
    Buffer four={0},eight={0};
    for (size_t i=0;i<groups->count;++i) {
        const ClutGroup *g=&groups->items[i];
        int slots=g->bpp==4 ? 16:256;
        Buffer *out=g->bpp==4 ? &four:&eight;
        for (int j=0;j<slots;++j) {
            int k=j;
            if (slots==256 && (j&0x18)==8) k=j+8;
            else if (slots==256 && (j&0x18)==16) k=j-8;
            uint32_t c=g->palette[k];
            put8(out,(c>>16)&255); put8(out,(c>>8)&255); put8(out,c&255); put8(out,((c>>24)+1)>>1);
        }
    }
    output(o,"CLUT4.BIN",&four); output(o,"CLUT8.BIN",&eight);
}
static Buffer rle(const Buffer *in) {
    Buffer out={0};
    for (size_t i=0;i<in->size;) {
        size_t n=1;
        while (n<255 && n<in->size-i && in->data[i+n]==in->data[i]) ++n;
        put8(&out,(unsigned)n); put8(&out,in->data[i]); i+=n;
    }
    return out;
}
static void debug_atlas(const Options *o,const Atlas *a,const ClutImage *images) {
    if (!o->debug) return;
    const uint32_t colors[]={0xffff0000,0xff00ff00,0xff0000ff,0xffffff00,0xff00ffff,0xffff00ff,0xffffc800};
    Image out=new_image(a->width,a->height);
    for (size_t i=0;i<a->count;++i) {
        Placement p=a->entries[i];
        const ClutImage *img=&images[p.image];
        for (int y=0;y<img->height;++y) for (int x=0;x<img->width;++x)
            out.pixels[(size_t)(p.y+y)*out.width+p.x+x]=img->palette[img->indices[(size_t)y*img->width+x]];
    }
    for (size_t i=0;i<a->count;++i) {
        Placement p=a->entries[i]; const ClutImage *img=&images[p.image]; uint32_t c=colors[i%7];
        for (int x=0;x<img->width;++x) {
            out.pixels[(size_t)p.y*out.width+p.x+x]=c;
            out.pixels[(size_t)(p.y+img->height-1)*out.width+p.x+x]=c;
        }
        for (int y=0;y<img->height;++y) {
            out.pixels[(size_t)(p.y+y)*out.width+p.x]=c;
            out.pixels[(size_t)(p.y+y)*out.width+p.x+img->width-1]=c;
        }
    }
    char *name=format_string("atlas_%d_%dbpp",a->id,a->bpp);
    debug_png(o,"atlas_debug",name,&out); free(name); free_image(&out);
}
static void metadata_entry(Buffer *out,const Asset *a,const ClutImage *image,
                           Location location,const Groups *groups) {
    put16(out,location.atlas<0 ? 65535:(unsigned)location.atlas,false);
    put16(out,location.x,false); put16(out,location.y,false);
    put16(out,image ? image->width:0,false); put16(out,image ? image->height:0,false);
    put16(out,a ? a->crop_x:0,false); put16(out,a ? a->crop_y:0,false);
    put16(out,a ? a->crop_w:0,false); put16(out,a ? a->crop_h:0,false);
    put16(out,image ? group_row(groups,image->group):0,false);
}
static void ps2(const Options *o,const DataWin *dw,const Image *pages) {
    pcre2_code **patterns=allocate(o->pattern_count,sizeof *patterns);
    for (size_t i=0;i<o->pattern_count;++i) {
        int err; PCRE2_SIZE off;
        patterns[i]=pcre2_compile((PCRE2_SPTR)o->patterns[i],PCRE2_ZERO_TERMINATED,PCRE2_UTF,&err,&off,NULL);
        if (!patterns[i]) {
            PCRE2_UCHAR message[256];
            pcre2_get_error_message(err,message,sizeof message);
            fail("invalid --force-4bpp regex at byte %zu: %s",(size_t)off,(char *)message);
        }
    }
    Assets assets=collect(o,dw,pages);
    printf("Processing %zu images\n",assets.count);
    ClutImage *images=allocate(assets.count,sizeof *images);
    const char **keys=allocate(assets.count,sizeof *keys);
    Location *locations=allocate(assets.count,sizeof *locations);
    for (size_t i=0;i<assets.count;++i) {
        bool force=false;
        for (size_t j=0;j<o->pattern_count;++j) {
            pcre2_match_data *match=pcre2_match_data_create_from_pattern(patterns[j],NULL);
            if (!match) fail("cannot allocate regex match data");
            int rc=pcre2_match(patterns[j],(PCRE2_SPTR)assets.items[i].name,strlen(assets.items[i].name),
                               0,PCRE2_ANCHORED|PCRE2_ENDANCHORED,match,NULL);
            pcre2_match_data_free(match);
            if (rc>=0) force=true;
            else if (rc!=PCRE2_ERROR_NOMATCH) fail("regex matching failed (%d) for '%s'",rc,assets.items[i].name);
        }
        images[i]=make_clut(assets.items[i].name,&assets.items[i].image,force);
        keys[i]=assets.items[i].key;
        locations[i].atlas=-1;
    }
    for (size_t i=0;i<o->pattern_count;++i) pcre2_code_free(patterns[i]);
    free(patterns);
    Groups groups=merge_cluts(images,assets.count);
    Atlases atlases=pack_atlases(images,keys,assets.count,o->atlas_size);
    clut_files(o,&groups);
    Buffer textures={0},metadata={0};
    put8(&metadata,0); put16(&metadata,dw->tpag_count,false);
    put16(&metadata,dw->tile_count,false); put16(&metadata,atlases.count,false);
    for (size_t i=0;i<atlases.count;++i) {
        const Atlas *a=&atlases.items[i];
        size_t area=(size_t)a->width*a->height;
        uint8_t *canvas=allocate(area,1);
        for (size_t j=0;j<a->count;++j) {
            Placement p=a->entries[j]; const ClutImage *img=&images[p.image];
            locations[p.image]=(Location){a->id,p.x,p.y};
            for (int y=0;y<img->height;++y)
                memcpy(canvas+(size_t)(p.y+y)*a->width+p.x,img->indices+(size_t)y*img->width,(size_t)img->width);
        }
        Buffer pixels={0};
        if (a->bpp==4) {
            for (size_t j=0;j<area;j+=2) put8(&pixels,canvas[j]|(j+1<area ? canvas[j+1]<<4:0));
        } else append(&pixels,canvas,area);
        free(canvas);
        Buffer compressed=rle(&pixels);
        bool use=compressed.size<pixels.size;
        Buffer *payload=use ? &compressed:&pixels;
        put32(&metadata,textures.size,false); put16(&metadata,a->width,false); put16(&metadata,a->height,false);
        put8(&metadata,a->bpp); put32(&metadata,payload->size,false); put8(&metadata,use ? 1:0);
        append(&textures,payload->data,payload->size);
        free(pixels.data); free(compressed.data);
        debug_atlas(o,a,images);
    }
    for (size_t i=0;i<dw->tpag_count;++i) {
        size_t found=SIZE_MAX;
        for (size_t j=0;j<assets.count;++j) if (assets.items[j].tpag==(int)i) found=j;
        if (found==SIZE_MAX) metadata_entry(&metadata,NULL,NULL,(Location){-1,0,0},&groups);
        else metadata_entry(&metadata,&assets.items[found],&images[found],locations[found],&groups);
    }
    for (size_t i=0;i<dw->tile_count;++i) {
        const Tile *t=&dw->tiles[i]; char *name=tile_name(dw,t,false);
        size_t found=SIZE_MAX;
        for (size_t j=0;j<assets.count;++j) if (!strcmp(name,assets.items[j].name)) found=j;
        free(name);
        put16(&metadata,t->definition,false); put16(&metadata,t->x,false); put16(&metadata,t->y,false);
        put16(&metadata,t->width,false); put16(&metadata,t->height,false);
        if (found==SIZE_MAX) metadata_entry(&metadata,NULL,NULL,(Location){-1,0,0},&groups);
        else metadata_entry(&metadata,&assets.items[found],&images[found],locations[found],&groups);
    }
    output(o,"TEXTURES.BIN",&textures); output(o,"ATLAS.BIN",&metadata);
    char *directory=copy_string(o->input),*slash=strrchr(directory,'/');
    if (slash) { if (slash==directory) slash[1]='\0'; else *slash='\0'; }
    else { free(directory); directory=copy_string("."); }
    Buffer bank={0},sounds={0};
    process_audio(dw,directory,&bank,&sounds);
    output(o,"SOUNDBNK.BIN",&bank); output(o,"SOUNDS.BIN",&sounds); free(directory);
    for (size_t i=0;i<assets.count;++i) {
        free(assets.items[i].name); free(assets.items[i].key); free_image(&assets.items[i].image); free_clut(&images[i]);
    }
    free(assets.items); free(images); free(keys); free(locations); free(groups.items); free_atlases(&atlases);
}
static void ps3(const Options *o,const DataWin *dw,const Image *pages) {
    ClutImage *images=allocate(dw->tpag_count,sizeof *images);
    int *mapping=allocate(dw->tpag_count,sizeof *mapping);
    size_t count=0;
    for (size_t i=0;i<dw->tpag_count;++i) {
        const Tpag *t=&dw->tpags[i]; mapping[i]=-1;
        if (t->page<0 || (size_t)t->page>=dw->texture_count || !pages[t->page].pixels || !t->tw || !t->th) continue;
        Image img=extract_tpag(t,pages,dw->texture_count);
        char *name=format_string("tpag_%zu",i);
        images[count]=make_clut(name,&img,false);
        images[count].bpp=8;
        mapping[i]=(int)count++;
        free(name); free_image(&img);
    }
    Groups groups=merge_cluts(images,count);
    Buffer out={0};
    put8(&out,0); put16(&out,groups.count,true); put16(&out,dw->texture_count,true); put16(&out,dw->tpag_count,true);
    for (size_t i=0;i<groups.count;++i) for (int j=0;j<256;++j) {
        uint32_t c=groups.items[i].palette[j];
        put8(&out,(c>>16)&255); put8(&out,(c>>8)&255); put8(&out,c&255); put8(&out,c>>24);
    }
    for (size_t i=0;i<dw->tpag_count;++i) {
        int index=mapping[i];
        put16(&out,index<0 ? 65535:group_row(&groups,images[index].group),true);
    }
    uint8_t **canvases=allocate(dw->texture_count,sizeof *canvases);
    size_t cursor=0;
    for (size_t i=0;i<dw->texture_count;++i) {
        size_t size=(size_t)pages[i].width*pages[i].height;
        canvases[i]=allocate(size,1);
        put16(&out,pages[i].width,true); put16(&out,pages[i].height,true);
        put32(&out,cursor,true); put32(&out,size,true);
        if (size>UINT32_MAX-cursor) fail("PS3 page data exceeds 32-bit offsets");
        cursor+=size;
    }
    for (size_t i=0;i<dw->tpag_count;++i) {
        if (mapping[i]<0) continue;
        const Tpag *t=&dw->tpags[i]; const ClutImage *img=&images[mapping[i]]; const Image *page=&pages[t->page];
        for (int y=0;y<t->th;++y) for (int x=0;x<t->tw;++x)
            if (t->ty+y<img->height && t->sy+y<page->height && t->tx+x<img->width && t->sx+x<page->width)
                canvases[t->page][(size_t)(t->sy+y)*page->width+t->sx+x]=img->indices[(size_t)(t->ty+y)*img->width+t->tx+x];
    }
    for (size_t i=0;i<dw->texture_count;++i) {
        append(&out,canvases[i],(size_t)pages[i].width*pages[i].height); free(canvases[i]);
    }
    output(o,"textures.bin",&out);
    for (size_t i=0;i<count;++i) free_clut(&images[i]);
    free(images); free(mapping); free(canvases); free(groups.items);
}
void process(const Options *o) {
    DataWin dw=parse_datawin(read_file(o->input),false,o->ps3);
    mkdirs(o->output);
    Image *pages=allocate(dw.texture_count,sizeof *pages);
    for (size_t i=0;i<dw.texture_count;++i)
        if (dw.textures[i].size) pages[i]=decode_image(dw.textures[i],version_at_least(&dw,2022,5,0,0));
    if (o->ps3) ps3(o,&dw,pages);
    else ps2(o,&dw,pages);
    for (size_t i=0;i<dw.texture_count;++i) free_image(&pages[i]);
    free(pages); free_datawin(&dw);
    puts("Done!");
}
