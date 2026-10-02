#ifndef _BS_NOOP_RENDERER_H_
#define _BS_NOOP_RENDERER_H_

#include "common.h"
#include "renderer.h"

// A no-op renderer that silently ignores all rendering calls.
// Useful for GML interpreter benchmarking where rendering can create unnecessary noise
Renderer* libndsRenderer_create(void);

// ===[ Atlas Entry (from ATLAS.BIN TPAG entries) ]===
typedef struct {
    uint16_t atlasId;   // TEX atlas index (0xFFFF = not mapped)
    uint16_t atlasX;    // X offset within the atlas
    uint16_t atlasY;    // Y offset within the atlas
    uint16_t width;     // Image width in the atlas (post-crop, post-resize)
    uint16_t height;    // Image height in the atlas (post-crop, post-resize)
    uint16_t cropX;     // X offset of cropped content within original bounding box
    uint16_t cropY;     // Y offset of cropped content within original bounding box
    uint16_t cropW;     // Pre-resize width of the cropped content
    uint16_t cropH;     // Pre-resize height of the cropped content
    uint16_t clutIndex; // CLUT index within the corresponding CLUT file
} AtlasTPAGEntry;

// ===[ Atlas Tile Entry (from ATLAS.BIN tile entries) ]===
typedef struct {
    int16_t bgDef;      // Background definition index
    uint16_t srcX;      // Source X in the original background image
    uint16_t srcY;      // Source Y in the original background image
    uint16_t srcW;      // Original tile width in pixels
    uint16_t srcH;      // Original tile height in pixels
    uint16_t atlasId;   // TEX atlas index (0xFFFF = not mapped)
    uint16_t atlasX;    // X offset within the atlas
    uint16_t atlasY;    // Y offset within the atlas
    uint16_t width;     // Tile width in the atlas (post-crop, post-resize)
    uint16_t height;    // Tile height in the atlas (post-crop, post-resize)
    uint16_t cropX;     // X offset of cropped content within original tile
    uint16_t cropY;     // Y offset of cropped content within original tile
    uint16_t cropW;     // Pre-resize width of the cropped content
    uint16_t cropH;     // Pre-resize height of the cropped content
    uint16_t clutIndex; // CLUT index within the corresponding CLUT file
} AtlasTileEntry;

// ===[ Tile Lookup Key (for O(1) hashmap lookup) ]===
typedef struct {
    int16_t bgDef;
    uint16_t srcX;
    uint16_t srcY;
    uint16_t srcW;
    uint16_t srcH;
} TileLookupKey;

// stb_ds hashmap entry: TileLookupKey -> AtlasTileEntry*
typedef struct {
    TileLookupKey key;
    AtlasTileEntry* value;
} TileEntryMap;





// ===[ LibNDSRenderer Struct ]===

typedef struct {
    Renderer base; // Must be first field for struct embedding

    // Minimal surface bookkeeping so surface_exists / get_width etc. behave sanely
    int32_t *surfaceWidths;
    int32_t *surfaceHeights;
    bool *surfaceExistsFlag;
    uint32_t surfaceCount;
    uint32_t surfaceCapacity;

    // View transform state
    float scaleX;
    float scaleY;
    float offsetX;
    float offsetY;
    int32_t viewX;
    int32_t viewY;

    //Atlas data
    uint16_t atlasTPAGCount;
    uint16_t atlasTileCount;
    AtlasTPAGEntry* atlasTPAGEntries;
    AtlasTileEntry* atlasTileEntries;
    TileEntryMap* tileEntryMap; // stb_ds hashmap: (bgDef, srcX, srcY, srcW, srcH) -> AtlasTileEntry*

    //Texture page cache
    uint16_t *texPixels[16];
    int32_t texW[16];
    int32_t texH[16];

    // GPU state shadows (returned by getters, mutated by setters)
    bool blendEnable;
    int32_t blendMode;
    BlendFactors blendFactors;
    bool alphaTestEnable;
    uint8_t alphaTestRef;
    bool colorWriteR, colorWriteG, colorWriteB, colorWriteA;
    bool fogEnable;
    uint32_t fogColor;
} LibNDSRenderer;

#endif /* _BS_NOOP_RENDERER_H_ */
