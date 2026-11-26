#ifndef _BS_SW_RENDERER_H_
#define _BS_SW_RENDERER_H_

#include "renderer.h"

Renderer* SWRenderer_create(void);
void SWRenderer_clearFrameBuffer(Renderer* renderer, uint32_t color);

#endif /* _BS_SW_RENDERER_H_ */
