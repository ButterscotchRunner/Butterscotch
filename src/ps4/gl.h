#ifndef _BS_PS4_GL_H_
#define _BS_PS4_GL_H_

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#define GL_READ_FRAMEBUFFER GL_READ_FRAMEBUFFER_ANGLE
#define GL_DRAW_FRAMEBUFFER GL_DRAW_FRAMEBUFFER_ANGLE
#define GL_DRAW_FRAMEBUFFER_BINDING GL_DRAW_FRAMEBUFFER_BINDING_ANGLE
#define GL_RGBA8 GL_RGBA8_OES
#define GL_DEPTH_COMPONENT24 GL_DEPTH_COMPONENT24_OES

GL_APICALL void GL_APIENTRY glBindVertexArray(GLuint array);
GL_APICALL void GL_APIENTRY glGenVertexArrays(GLsizei n, GLuint *arrays);
GL_APICALL void GL_APIENTRY glDeleteVertexArrays(GLsizei n,
                                                 const GLuint *arrays);
GL_APICALL void GL_APIENTRY glBlitFramebuffer(GLint srcX0, GLint srcY0,
                                              GLint srcX1, GLint srcY1,
                                              GLint dstX0, GLint dstY0,
                                              GLint dstX1, GLint dstY1,
                                              GLbitfield mask, GLenum filter);

#endif
