#ifndef _BS_DEFAULT_FONT_H_
#define _BS_DEFAULT_FONT_H_

#include "data_win.h"

void DefaultFont_initializeGlyphs(Font* font, FontGlyph* glyphs, float advanceScale);
int32_t DefaultFont_ensureLoaded(DataWin* dataWin);

#endif
