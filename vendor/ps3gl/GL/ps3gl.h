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

#ifdef __cplusplus
}
#endif

#endif /* _BS_PS3GL_H_ */
