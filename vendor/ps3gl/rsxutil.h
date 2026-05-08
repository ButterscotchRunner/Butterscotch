#ifndef __RSXUTIL_H__
#define __RSXUTIL_H__

#include <ppu-types.h>

#include <rsx/rsx.h>

#define CB_SIZE		0x100000
#define HOST_SIZE	(32*1024*1024)

#ifdef __cplusplus
extern "C" {
#endif
extern gcmContextData *context;
extern u32 display_width;
extern u32 display_height;
extern u32 curr_fb;

extern u32 color_pitch;
extern u32 color_offset[2];
extern u32 *color_buffer[2];

extern u32 depth_pitch;
extern u32 depth_offset;

void setRenderTarget(u32 index);
void waitflip();
void waitFinish(void);

void init_screen(void *host_addr,u32 size);
void flip();
#ifdef __cplusplus
}
#endif

#endif
