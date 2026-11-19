#ifndef _BS_VITA_TEXTURES_H_
#define _BS_VITA_TEXTURES_H_

#include "gl_common.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

bool VitaTextures_Init(FILE *binFile);
bool VitaTextures_LoadPage(GLRenderer* renderer, int pageIdx, int* outWidth, int* outHeight);
void VitaTextures_Free(void);
uint32_t VitaTextures_GetPageCount(void);
bool VitaTextures_Active(void);

#endif /* _BS_VITA_TEXTURES_H_ */
