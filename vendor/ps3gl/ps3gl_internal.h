#pragma once

#include "GL/gl.h"
#include <vectormath/c/vectormath_aos.h>

// TODO: Drop need to link to librsx eventually
#include <rsx/rsx.h>
#include <ppu-types.h>

#include <string.h>
#include <malloc.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include <ppu-asm.h>

#define MAX_TEXTURES 1024
#define MAX_PROJ_STACK 4 
#define MAX_MODV_STACK 16

// Aliases for simplicity
#define GCM_TEXTURE_CONVOLUTION_NONE 0
#define GCM_TEXTURE_CLAMP_TO_BORDER GCM_TEXTURE_BORDER


enum _ps3gl_rsx_constants
{
	// Vertex Uniforms
	PS3GL_Uniform_ModelViewMatrix,
	PS3GL_Uniform_ProjectionMatrix,

	// Fragment Uniforms
	PS3GL_Uniform_TextureEnabled,
	PS3GL_Uniform_TextureMode,
	PS3GL_Uniform_FogEnabled,
	PS3GL_Uniform_FogColor,

	// Num
	PS3GL_Uniform_Count,
};

enum _ps3gl_texenv_modes
{
	PS3GL_TEXENV_BLEND = 0,
	PS3GL_TEXENV_MODULATE = 1,
	PS3GL_TEXENV_REPLACE = 2,
};

struct ps3gl_texture {
	GLuint id, target;
	GLboolean allocated;
	GLubyte* data;
	GLint minFilter, magFilter;
	GLint wrapS, wrapR, wrapT;
	gcmTexture gcmTexture;
};

struct ps3gl_opengl_state
{
	// Color
	GLuint clear_color;
	GLuint color_mask;

	struct {
		GLushort x,y,w,h;
		GLfloat scale[4], offset[4];
	} viewport;

	struct {
		GLboolean enabled;
		GLushort x,y,w,h;
	} scissor;

	// Depth
	GLdouble clear_depth;
	GLfloat depth_near;
	GLfloat depth_far;
	GLboolean depth_mask;
	GLboolean depth_test;
	GLuint depth_func;

	// Stenciling
	GLuint clear_stencil;

	// Alpha
	GLboolean alpha_test_enabled;
	GLenum alpha_func;
	GLuint alpha_func_ref;

	// Blend
	GLboolean blend_enabled;
	GLenum blend_equation;
	GLenum blend_func_sfactor;
	GLenum blend_func_dfactor;

	// Matrices
	GLuint matrix_mode;
	
	int cur_modv_mat;
	VmathMatrix4 modelview_matrix;
	VmathMatrix4 modelview_stack[MAX_MODV_STACK];
	
	int cur_proj_mat;
    VmathMatrix4 projection_matrix;
    VmathMatrix4 projection_stack[MAX_PROJ_STACK];

    VmathMatrix4 *curr_mtx;

	// Textures
	rsxProgramAttrib* texture0Unit;
	GLboolean texture0_enabled;
	GLuint blend_color_rsx;
	GLfloat texEnvMode; 
	struct ps3gl_texture textures[MAX_TEXTURES];
	struct ps3gl_texture *bound_texture;
	GLuint nextTextureID;

	// Lighting
	GLuint shade_model;

	// Fog
	GLboolean fog_enabled;
	GLint fog_mode;
	GLfloat fog_start, fog_end, fog_density;
	GLfloat fog_color[4];

	// FFP Shader Consts
	rsxProgramConst *prog_consts[PS3GL_Uniform_Count];

	// Misc
	GLenum front_face;
	GLfloat point_size;
	GLenum polygon_mode_face;
	GLenum polygon_mode;

	GLboolean logic_op_enabled;
	GLenum logic_op;
	GLboolean cull_face_enabled;
	GLenum cull_face;
};

// Helper functions for PS3GL, these are internal only!

// From PSL1GHT, can't use it since it's RSX_INTERNAL
static inline GLfloat swapF32_16(GLfloat v)
{
	ieee32 d;
	d.f = v;
	d.u = ( ( ( d.u >> 16 ) & 0xffff ) << 0 ) | ( ( ( d.u >> 0 ) & 0xffff ) << 16 );
	return d.f;
}

static inline void rsxSetFragmentProgramParameterBool(gcmContextData *context,const rsxFragmentProgram *program,const rsxProgramConst *param,GLboolean value,GLuint offset,GLuint location)
{
	GLfloat params[4] = {0.0f,0.0f,0.0f,0.0f};
	params[0] = swapF32_16((float)value);
	rsxConstOffsetTable *co_table = rsxFragmentProgramGetConstOffsetTable(program, param->index);
	for(int i = 0; i < co_table->num; ++i)
	{
		rsxInlineTransfer(context,
			offset + co_table->offset[i],
			params,
			1,
			location);
	}
}

static inline void rsxSetFragmentProgramParameterF32(gcmContextData *context,const rsxFragmentProgram *program,const rsxProgramConst *param,float value,GLuint offset,GLuint location)
{
	GLfloat params[4] = {0.0f,0.0f,0.0f,0.0f};
	params[0] = swapF32_16(value);
	rsxConstOffsetTable *co_table = rsxFragmentProgramGetConstOffsetTable(program, param->index);
	for(int i = 0; i < co_table->num; ++i)
	{
		rsxInlineTransfer(context,
			offset + co_table->offset[i],
			params,
			1,
			location);
	}
}

static inline void rsxSetFragmentProgramParameterF32Vec4(gcmContextData *context,const rsxFragmentProgram *program,const rsxProgramConst *param,float *value,GLuint offset,GLuint location)
{
	GLfloat params[4] = {0.0f,0.0f,0.0f,0.0f};
	params[0] = swapF32_16(value[0]);
	params[1] = swapF32_16(value[1]);
	params[2] = swapF32_16(value[2]);
	params[3] = swapF32_16(value[3]);
	rsxConstOffsetTable *co_table = rsxFragmentProgramGetConstOffsetTable(program,
		param->index);
	for(int i = 0; i < co_table->num; ++i)
	{
		rsxInlineTransfer(context,
			offset + co_table->offset[i],
			params,
			4,
			GCM_LOCATION_RSX);
	}
}
