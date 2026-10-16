#ifndef _SW_TEXT_H
#define _SW_TEXT_H

void swrDrawText(SWRenderer* swr, const char* text, float x, float y, float xscale, float yscale, float angleDeg, int32_t color, float alpha, float lineSeparation);
void swrDrawDebugText(SWRenderer* swr, const char* text, int x, int y, uint32_t color, float alpha);

#endif