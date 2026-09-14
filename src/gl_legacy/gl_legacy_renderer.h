#ifndef _BS_GL_LEGACY_RENDERER_H_
#define _BS_GL_LEGACY_RENDERER_H_

#include "common.h"
#include "gl_common.h"
#include "renderer.h"
#include "runner.h"

#ifdef PLATFORM_PS3
#include "ps3gl.h"
#include "rsxutil.h"
#elif PLATFORM_VITA
#include <vitaGL.h>
#else
#include <glad/glad.h>
#endif

typedef struct {
    GLRenderer base; // Must be first field for struct embedding

    int32_t primitiveCapacity;

    // True if the GPU doesn't support NPOT textures (GL < 2.0), requiring
    // FBO color-attachment textures to have power-of-two dimensions.
    bool needsPOT;
} GLLegacyRenderer;

bool GLLegacyRenderer_ensureTextureLoaded(GLRenderer* gl, uint32_t pageId);
Renderer* GLLegacyRenderer_create(void);

#endif /* _BS_GL_LEGACY_RENDERER_H_ */
