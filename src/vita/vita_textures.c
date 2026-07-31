#include "vita_textures.h"
#include "stb_image.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <psp2/kernel/clib.h>

static FILE* vitaTexBinF = NULL;
static int pageCount = 0;
static int* pageOffsets = NULL;
static int* pageSizes = NULL;

static const uint8_t PNG_SIGNATURE[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};  
static bool vitaUsingTexBin = false;

bool isPNG(FILE *f) {
    if (!f) return false;
    uint8_t header[8];
    if (fread(header, 1, 8, f) != 8) return false;
    fseek(f, -8, SEEK_CUR);
    return memcmp(header, PNG_SIGNATURE, 8) == 0;
}

bool VitaTextures_Active() { return vitaUsingTexBin; }
uint32_t VitaTextures_GetPageCount() { return (uint32_t)pageCount; }

bool VitaTextures_Init(const char* binPath) {
    if (vitaUsingTexBin) return true;
    vitaUsingTexBin = true;

    vitaTexBinF = fopen(binPath, "rb");
    if (!vitaTexBinF) {
        goto fail;
    }

    if (!fread(&pageCount, sizeof(int), 1, vitaTexBinF)) goto fail;
    pageOffsets = malloc(pageCount * sizeof(int));
    pageSizes = malloc(pageCount * sizeof(int));

    for (int i = 0; i < pageCount; i++) {
        pageOffsets[i] = (int)ftell(vitaTexBinF) + 4;
        if (!fread(&pageSizes[i], sizeof(int), 1, vitaTexBinF)) goto fail;
        fseek(vitaTexBinF, pageSizes[i], SEEK_CUR);
    }
    
    sceClibPrintf("Succesfully parsed textures.bin\n");
    return true;
fail:
    if (vitaTexBinF) fclose(vitaTexBinF);
    sceClibPrintf("textures.bin failed to load, bailin tf outta here\n");
    return false;
}

bool VitaTextures_LoadPage(int pageIdx, int* outWidth, int* outHeight) {
    if (!vitaTexBinF) return false;

    int width, height;
    uint32_t *ext_data;
    fseek(vitaTexBinF, pageOffsets[pageIdx] + pageSizes[pageIdx], SEEK_SET);
    uint32_t size = pageSizes[pageIdx] - 0x34;
    uint32_t metadata_size;
    fseek(vitaTexBinF, pageOffsets[pageIdx] + 0x08, SEEK_SET);
    uint64_t format;
    fread(&format, 1, 8, vitaTexBinF);
    fseek(vitaTexBinF, pageOffsets[pageIdx] + 0x18, SEEK_SET);
    fread(&height, 1, 4, vitaTexBinF);
    fread(&width, 1, 4, vitaTexBinF);
    fseek(vitaTexBinF, pageOffsets[pageIdx] + 0x30, SEEK_SET);
    fread(&metadata_size, 1, 4, vitaTexBinF);
    size -= metadata_size;
    ext_data = vglMalloc(size);
    fseek(vitaTexBinF, metadata_size, SEEK_CUR);
    fread(ext_data, 1, size, vitaTexBinF);
    
    switch (format) {
        case 0x00:
            glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG, width, height, 0, size, ext_data);
            break;
        case 0x01:
            glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG, width, height, 0, size, ext_data);
            break;
        case 0x02:
            glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG, width, height, 0, size, ext_data);
            break;
        case 0x03:
            glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG, width, height, 0, size, ext_data);
            break;
        case 0x04:
            glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_PVRTC_2BPPV2_IMG, width, height, 0, size, ext_data);
            break;
        case 0x05:
            glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_PVRTC_4BPPV2_IMG, width, height, 0, size, ext_data);
            break;
        case 0x06:
            glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, width, height, 0, size, ext_data);
            break;
        case 0x07:
            glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, width, height, 0, size, ext_data);
            break;
        case 0x09:
            glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, width, height, 0, size, ext_data);
            break;
        case 0x0B:
            if (metadata_size == 4) { // Load DXT5 as pre-swizzled
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
                SceGxmTexture *gxm_tex = vglGetGxmTexture(GL_TEXTURE_2D);
                vglFree(vglGetTexDataPointer(GL_TEXTURE_2D));
                void *tex_data = vglForceAlloc(size);
                sceClibMemcpy(tex_data, ext_data, size);
                sceGxmTextureInitSwizzledArbitrary(gxm_tex, tex_data, SCE_GXM_TEXTURE_FORMAT_UBC3_ABGR, width, height, 0);
                vglOverloadTexDataPointer(GL_TEXTURE_2D, tex_data);
            } else
                glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, width, height, 0, size, ext_data);
            break;
        default:
            fseek(vitaTexBinF, pageOffsets[pageIdx], SEEK_SET);
            if (isPNG(vitaTexBinF)) {
                ext_data = (uint32_t*)stbi_load_from_file(vitaTexBinF, &width, &height, NULL, 4);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, ext_data);
            } else {
                sceClibPrintf("Unsupported externalized texture format (0x%llX).\n", format);
                *outWidth = 0;
                *outHeight = 0;
                vglFree(ext_data);
                return false;
            }
            break;
    }
    vglFree(ext_data);

    bool isPOT = (width & (width - 1)) == 0 && (height & (height - 1)) == 0;
    GLint wrapMode = isPOT ? GL_REPEAT : GL_CLAMP_TO_EDGE;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapMode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapMode);

    *outWidth = width;
    *outHeight = height;

    return true;
}

void VitaTextures_Free() {
    if (!VitaTextures_Active()) return;
    if (vitaTexBinF) fclose(vitaTexBinF);
    free(pageSizes);
    free(pageOffsets);
}