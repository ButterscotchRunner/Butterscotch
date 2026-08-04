#ifndef _BS_VITA_TEXTURES_H_
#define _BS_VITA_TEXTURES_H_

#include <vitaGL.h>
#include <stdbool.h>
#include <stdint.h>

bool VitaTextures_Init(const char* binPath);
bool VitaTextures_LoadPage(int pageIdx, int* outWidth, int* outHeight);
void VitaTextures_Free();
uint32_t VitaTextures_GetPageCount();
bool VitaTextures_Active();

#endif /* _BS_VITA_TEXTURES_H_ */