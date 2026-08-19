#ifndef _SW_TEXTURE_H
#define _SW_TEXTURE_H

SWTexture* swrCreateTextureEx(const void* srcBuffer, int width, int height, bool convert);
SWTexture* swrCreateTexture(const uint8_t* srcBuffer, int width, int height);
SWTexture* swrCopyTexture(SWTexture* texture);
void swrFreeTexture(SWTexture* texture);

#endif//_SW_TEXTURE_H
