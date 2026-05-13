#pragma once

#include "GL/gl.h"
#ifdef __cplusplus
extern "C"
{
#endif

void ps3glInit(void);
void ps3glSwapBuffers(void);

// Multi-texture-unit support. Pass GL_TEXTURE0..GL_TEXTURE3.
void glActiveTexture(GLenum texture);

#define GL_VERTEX_SHADER                0x8B31
#define GL_FRAGMENT_SHADER              0x8B30
#define GL_COMPILE_STATUS               0x8B81
#define GL_LINK_STATUS                  0x8B82
#define GL_INFO_LOG_LENGTH              0x8B84

#define PS3GL_SHADER_BINARY_VPO         0x1ED01
#define PS3GL_SHADER_BINARY_FPO         0x1ED02

typedef char GLchar;

GLuint glCreateShader(GLenum type);
void   glShaderBinary(GLsizei count, const GLuint *shaders, GLenum binaryFormat, const void *binary, GLsizei length);
void   glDeleteShader(GLuint shader);
void   glGetShaderiv(GLuint shader, GLenum pname, GLint *params);

GLuint glCreateProgram(void);
void   glAttachShader(GLuint program, GLuint shader);
void   glDetachShader(GLuint program, GLuint shader);
void   glLinkProgram(GLuint program);
// Activates a user shader program for subsequent draws. Pass 0 to revert to the fixed-function pipeline.
void   glUseProgram(GLuint program);
void   glDeleteProgram(GLuint program);
void   glGetProgramiv(GLuint program, GLenum pname, GLint *params);

GLint  glGetUniformLocation(GLuint program, const GLchar *name);
GLint  glGetAttribLocation(GLuint program, const GLchar *name);

void   glUniform1i(GLint location, GLint v0);
void   glUniform1f(GLint location, GLfloat v0);
void   glUniform2f(GLint location, GLfloat v0, GLfloat v1);
void   glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
void   glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
void   glUniform3fv(GLint location, GLsizei count, const GLfloat *value);
void   glUniform4fv(GLint location, GLsizei count, const GLfloat *value);
void   glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);

#ifdef __cplusplus
}
#endif
