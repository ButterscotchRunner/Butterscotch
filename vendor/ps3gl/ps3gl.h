#ifndef _BS_PS3GL_H_
#define _BS_PS3GL_H_

#define GL_GLEXT_PROTOTYPES
#include "GL/gl.h"
#ifdef __cplusplus
extern "C"
{
#endif

void ps3glInit(void);
void ps3glSwapBuffers(void);

#define PS3GL_SHADER_BINARY_VPO         0x1ED01
#define PS3GL_SHADER_BINARY_FPO         0x1ED02

typedef char GLchar;

#ifdef __cplusplus
}
#endif

#endif /* _BS_PS3GL_H_ */
