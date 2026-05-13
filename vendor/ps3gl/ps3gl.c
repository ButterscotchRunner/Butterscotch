// PS3GL - An OpenGL 1.5 Compatibility Layer on top of the RSX API

#include "GL/gl.h"
#include "ps3gl.h"
#include <stdint.h>
// TODO: Move the rsxutil functionality into ps3glInit
#include "rsxutil.h"

#include "ps3gl_internal.h"
#include "ffp_shader_vpo.h"
#include "ffp_shader_fpo.h"

#ifndef GCM_TEXTURE_MIRROR_CLAMP_TO_EDGE
#define GCM_TEXTURE_MIRROR_CLAMP_TO_EDGE GCM_TEXTURE_MIRROR_ONCE_CLAMP_TO_EDGE 
#endif

static struct ps3gl_opengl_state _opengl_state;

static inline struct ps3gl_texture* activeBoundTex(void)
{
	return _opengl_state.bound_textures[_opengl_state.active_texture_unit];
}

u32 fp_offset;
u32 *fp_buffer;

void *vp_ucode = NULL;
void *fp_ucode = NULL;

rsxVertexProgram *vpo = (rsxVertexProgram*)ffp_shader_vpo;
rsxFragmentProgram *fpo = (rsxFragmentProgram*)ffp_shader_fpo;

/*
 * Miscellaneous
 */

void glClearColor( GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha )
{
	_opengl_state.clear_color = 
	(((uint8_t)(alpha * 255.0f)) << 24) |
    (((uint8_t)(red   * 255.0f)) << 16) |
    (((uint8_t)(green * 255.0f)) << 8)  |
    (((uint8_t)(blue  * 255.0f)) << 0);
}

// from mesa's nouveau driver, specifically nv30/nv30_clear.c
static inline uint32_t
pack_zeta(bool use_stencil, double depth, unsigned stencil)
{
   uint32_t zuint = (uint32_t)(depth * UINT32_MAX);
    if (use_stencil)
 	  return (zuint & 0xffffff00) | (stencil & 0xff);
   return zuint >> 16;
}


void glClear( GLbitfield mask )
{
	uint32_t rsx_mask = 0;

	if(mask == (GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT))
	{
		rsx_mask = GCM_CLEAR_M;
		rsxSetClearColor(context, _opengl_state.clear_color);
		rsxSetClearDepthStencil(context, pack_zeta(true, _opengl_state.clear_depth, _opengl_state.clear_stencil));
		rsxClearSurface(context, rsx_mask);
		return;
	} 

	if(mask & GL_COLOR_BUFFER_BIT)
	{
		rsx_mask |= (GCM_CLEAR_A | GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B);
		rsxSetClearColor(context, _opengl_state.clear_color);
	}
	
	if(mask & GL_DEPTH_BUFFER_BIT)
	{
		rsxSetClearDepthStencil(context, pack_zeta(true, _opengl_state.clear_depth, _opengl_state.clear_stencil));
		rsx_mask |= GCM_CLEAR_Z;
	}
	
	if(mask & GL_STENCIL_BUFFER_BIT)
	{
		rsxSetClearDepthStencil(context, pack_zeta(true, _opengl_state.clear_depth, _opengl_state.clear_stencil));
		rsx_mask |= GCM_CLEAR_S;
	}

	rsxClearSurface(context, rsx_mask);
}

void glColorMask( GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha )
{
	_opengl_state.color_mask = 0;
	if(red)   _opengl_state.color_mask |= GCM_COLOR_MASK_R;
	if(green) _opengl_state.color_mask |= GCM_COLOR_MASK_G;
	if(blue)  _opengl_state.color_mask |= GCM_COLOR_MASK_B;
	if(alpha) _opengl_state.color_mask |= GCM_COLOR_MASK_A;
}

void glAlphaFunc( GLenum func, GLclampf ref )
{
	_opengl_state.alpha_func = func;
	_opengl_state.alpha_func_ref = ref * 255;
}

void glBlendFunc( GLenum sfactor, GLenum dfactor )
{
	_opengl_state.blend_func_sfactor = sfactor;
	_opengl_state.blend_func_dfactor = dfactor;
}

void glLogicOp( GLenum opcode )
{
	_opengl_state.logic_op = opcode;
}

void glCullFace( GLenum mode )
{
	_opengl_state.cull_face = mode;
}

void glFrontFace( GLenum mode )
{
	// The Enum values match BUT 
	// due to how the RSX Works GL_CW == GCM_CCW and GL_CCW == GL_CW
	_opengl_state.front_face = mode ^ 1u;
}

void glPointSize( GLfloat size )
{
	_opengl_state.point_size = size;	
}

void glPolygonMode( GLenum face, GLenum mode )
{
	_opengl_state.polygon_mode_face = face;
	_opengl_state.polygon_mode = mode;
}

void glScissor( GLint x, GLint y, GLsizei width, GLsizei height)
{
	_opengl_state.scissor.x = x;
	_opengl_state.scissor.y = y;
	_opengl_state.scissor.w = width;
	_opengl_state.scissor.h = height;
}

void glEnable( GLenum cap )
{
	switch(cap)
	{
		case GL_ALPHA_TEST:
			_opengl_state.alpha_test_enabled = true;
			break;
		case GL_BLEND:
			_opengl_state.blend_enabled = true;
			break;
		case GL_CULL_FACE:
			_opengl_state.cull_face_enabled = true;
			break;
		case GL_COLOR_LOGIC_OP:
			_opengl_state.logic_op_enabled = true;
			break;
		case GL_DEPTH_TEST:
			_opengl_state.depth_test = true;
			break;
		case GL_FOG:
			_opengl_state.fog_enabled = true;
			break;
		case GL_TEXTURE_2D:
			_opengl_state.texture_unit_enabled[_opengl_state.active_texture_unit] = true;
			break;
		case GL_SCISSOR_TEST:
			_opengl_state.scissor.enabled = true;
			break;
		default:
			break;
	}
}

GLboolean glIsEnabled( GLenum cap )
{
	switch(cap)
	{
		case GL_ALPHA_TEST:
			return _opengl_state.alpha_test_enabled;
		case GL_BLEND:
			return _opengl_state.blend_enabled;
		case GL_CULL_FACE:
			return _opengl_state.cull_face_enabled;
		case GL_COLOR_LOGIC_OP:
			return _opengl_state.logic_op_enabled;
		case GL_DEPTH_TEST:
			return _opengl_state.depth_test;
		case GL_FOG:
			return _opengl_state.fog_enabled;
		case GL_TEXTURE_2D:
			return _opengl_state.texture_unit_enabled[_opengl_state.active_texture_unit];
		case GL_SCISSOR_TEST:
			return _opengl_state.scissor.enabled;
		default:
			return false;
	}
}

void glDisable( GLenum cap )
{
	switch(cap)
	{
		case GL_ALPHA_TEST:
			_opengl_state.alpha_test_enabled = false;
			break;
		case GL_BLEND:
			_opengl_state.blend_enabled = false;
			break;
		case GL_CULL_FACE:
			_opengl_state.cull_face_enabled = false;
			break;
		case GL_COLOR_LOGIC_OP:
			_opengl_state.logic_op_enabled = false;
			break;
		case GL_DEPTH_TEST:
			_opengl_state.depth_test = false;
			break;
		case GL_FOG:
			_opengl_state.fog_enabled = false;
			break;
		case GL_TEXTURE_2D:
			_opengl_state.texture_unit_enabled[_opengl_state.active_texture_unit] = false;
			break;
		case GL_SCISSOR_TEST:
			_opengl_state.scissor.enabled = false;
			break;
		default:
			break;
	}
}

GLenum glGetError( void ) { return GL_NO_ERROR; } // TODO?

void glFinish( void ) {} // We call rsxFinish every frame

void glFlush( void ) {} // We call rsxFlushBuffer every frame

void glHint( GLenum target, GLenum mode ) {} // No idea how to implement this

/*
 * Depth Buffer
 */

void glClearDepth( GLclampd depth )
{
	_opengl_state.clear_depth = depth;	
}

void glDepthFunc( GLenum func )
{
	// Values is the same between what OpenGL defines 
	// and what the RSX expects
	_opengl_state.depth_func = func;
}

void glDepthMask( GLboolean flag )
{
	_opengl_state.depth_mask = flag;
}

void glDepthRange( GLclampd near_val, GLclampd far_val )
{
	_opengl_state.depth_near = near_val;
	_opengl_state.depth_far  = far_val;
}

/*
 * Transformation
 */


void glMatrixMode( GLenum mode )
{
	_opengl_state.matrix_mode = mode;
	switch(mode)
	{
		case GL_MODELVIEW:
			_opengl_state.curr_mtx = &_opengl_state.modelview_matrix;
			break;
		case GL_PROJECTION:
			_opengl_state.curr_mtx = &_opengl_state.projection_matrix;
			break;
		default:
			fprintf(stderr, "Unimplemented MatrixMode: %u", mode);
			break;
	}
}

// glPopMatrix and glPushMatrix are based on OpenGX
void glPopMatrix(void)
{
    switch (_opengl_state.matrix_mode) {
    case GL_PROJECTION:
        memcpy(&_opengl_state.projection_matrix, &_opengl_state.projection_stack[_opengl_state.cur_proj_mat], sizeof(VmathMatrix4));
        _opengl_state.cur_proj_mat--;
		
        break;
    case GL_MODELVIEW:
        memcpy(&_opengl_state.modelview_matrix, &_opengl_state.modelview_stack[_opengl_state.cur_modv_mat], sizeof(VmathMatrix4));
        _opengl_state.cur_modv_mat--;
        break;
    default:
        break;
    }
    //glparamstate.dirty.bits.dirty_matrices = 1;
}

void glPushMatrix(void)
{
    switch (_opengl_state.matrix_mode) {
    case GL_PROJECTION:
        _opengl_state.cur_proj_mat++;
        memcpy(&_opengl_state.projection_stack[_opengl_state.cur_proj_mat], &_opengl_state.projection_matrix, sizeof(VmathMatrix4));
        break;
    case GL_MODELVIEW:
        _opengl_state.cur_modv_mat++;
        memcpy(&_opengl_state.modelview_stack[_opengl_state.cur_modv_mat], &_opengl_state.modelview_matrix, sizeof(VmathMatrix4));
        break;
    default:
        break;
    }
}

void glOrtho( GLdouble left, GLdouble right,
                                 GLdouble bottom, GLdouble top,
                                 GLdouble near_val, GLdouble far_val )
{
	VmathMatrix4 ortho;
	vmathM4MakeOrthographic(&ortho, (GLfloat)left, (GLfloat)right, (GLfloat)bottom, (GLfloat)top, (GLfloat)near_val, (GLfloat)far_val);
	vmathM4Mul(_opengl_state.curr_mtx, _opengl_state.curr_mtx, &ortho);
}

void glFrustum( GLdouble left, GLdouble right,
                                   GLdouble bottom, GLdouble top,
                                   GLdouble near_val, GLdouble far_val )
{
	VmathMatrix4 frustum;
	vmathM4MakeFrustum(&frustum, (GLfloat)left, (GLfloat)right, (GLfloat)bottom, (GLfloat)top, (GLfloat)near_val, (GLfloat)far_val);
	
	VmathMatrix4 result;
	vmathM4Mul(&result, _opengl_state.curr_mtx, &frustum);
	vmathM4Copy(_opengl_state.curr_mtx, &result);
}

void glViewport( GLint x, GLint y, GLsizei width, GLsizei height )
{

	_opengl_state.viewport.x = x;
	_opengl_state.viewport.y = display_height - y - height;
	_opengl_state.viewport.w = width;
	_opengl_state.viewport.h = height;

	if(_opengl_state.scissor.h == 0)
	{
		_opengl_state.scissor.x = x;
		_opengl_state.scissor.y = y; // TODO: Check if this has to be display_height - y - height;
		_opengl_state.scissor.w = width;
		_opengl_state.scissor.h = height;
	}

	_opengl_state.viewport.scale[0] = width*0.5f;
	_opengl_state.viewport.scale[1] = height*-0.5f;
	_opengl_state.viewport.scale[2] = (_opengl_state.depth_far - _opengl_state.depth_near)*0.5f;
	_opengl_state.viewport.scale[3] = 0.0f;
	_opengl_state.viewport.offset[0] = x + width*0.5f;
	_opengl_state.viewport.offset[1] = y + height*0.5f;
	_opengl_state.viewport.offset[2] = (_opengl_state.depth_far + _opengl_state.depth_near)*0.5f;
	_opengl_state.viewport.offset[3] = 0.0f;
}

void glPushMatrix(void); // TODO

void glPopMatrix(void); // TODO

void glLoadIdentity(void)
{
	vmathM4MakeIdentity(_opengl_state.curr_mtx);
}

void glLoadMatrixf( const GLfloat *m )
{
	VmathVector4 col0;
	VmathVector4 col1;
	VmathVector4 col2;
	VmathVector4 col3;

	vmathV4MakeFromElems(&col0, m[0],  m[1],  m[2],  m[3]);
	vmathV4MakeFromElems(&col1, m[4],  m[5],  m[6],  m[7]);
	vmathV4MakeFromElems(&col2, m[8],  m[9],  m[10], m[11]);
	vmathV4MakeFromElems(&col3, m[12], m[13], m[14], m[15]);
	
	vmathM4MakeFromCols(_opengl_state.curr_mtx, &col0, &col1, &col2, &col3);
}


void glLoadMatrixd( const GLdouble *m )
{
	VmathVector4 col0;
	VmathVector4 col1;
	VmathVector4 col2;
	VmathVector4 col3;

	vmathV4MakeFromElems(&col0, (GLfloat)m[0],  (GLfloat)m[1],  (GLfloat)m[2],  (GLfloat)m[3]);
	vmathV4MakeFromElems(&col1, (GLfloat)m[4],  (GLfloat)m[5],  (GLfloat)m[6],  (GLfloat)m[7]);
	vmathV4MakeFromElems(&col2, (GLfloat)m[8],  (GLfloat)m[9],  (GLfloat)m[10], (GLfloat)m[11]);
	vmathV4MakeFromElems(&col3, (GLfloat)m[12], (GLfloat)m[13], (GLfloat)m[14], (GLfloat)m[15]);
	
	vmathM4MakeFromCols(_opengl_state.curr_mtx, &col0, &col1, &col2, &col3);
}

void glMultMatrixf( const GLfloat *m )
{
	VmathVector4 col0;
	VmathVector4 col1;
	VmathVector4 col2;
	VmathVector4 col3;

	vmathV4MakeFromElems(&col0, m[0],  m[1],  m[2],  m[3]);
	vmathV4MakeFromElems(&col1, m[4],  m[5],  m[6],  m[7]);
	vmathV4MakeFromElems(&col2, m[8],  m[9],  m[10], m[11]);
	vmathV4MakeFromElems(&col3, m[12], m[13], m[14], m[15]);

	VmathMatrix4 mulMatrix;
	vmathM4MakeFromCols(&mulMatrix, &col0, &col1, &col2, &col3);

	VmathMatrix4 result;
	vmathM4Mul(&result, _opengl_state.curr_mtx, &mulMatrix);
	vmathM4Copy(_opengl_state.curr_mtx, &result);
}

void glMultMatrixd( const GLdouble *m )
{
	VmathVector4 col0;
	VmathVector4 col1;
	VmathVector4 col2;
	VmathVector4 col3;
	
	vmathV4MakeFromElems(&col0, (GLfloat)m[0],  (GLfloat)m[1],  (GLfloat)m[2],  (GLfloat)m[3]);
	vmathV4MakeFromElems(&col1, (GLfloat)m[4],  (GLfloat)m[5],  (GLfloat)m[6],  (GLfloat)m[7]);
	vmathV4MakeFromElems(&col2, (GLfloat)m[8],  (GLfloat)m[9],  (GLfloat)m[10], (GLfloat)m[11]);
	vmathV4MakeFromElems(&col3, (GLfloat)m[12], (GLfloat)m[13], (GLfloat)m[14], (GLfloat)m[15]);
	
	VmathMatrix4 mulMatrix;
	vmathM4MakeFromCols(&mulMatrix, &col0, &col1, &col2, &col3);

	VmathMatrix4 result;
	vmathM4Mul(&result, _opengl_state.curr_mtx, &mulMatrix);
	vmathM4Copy(_opengl_state.curr_mtx, &result);
}

void glRotatef( GLfloat angle, GLfloat x, GLfloat y, GLfloat z )
{
	VmathVector3 unitVec;
	vmathV3MakeFromElems(&unitVec, x, y, z);
	vmathV3Normalize(&unitVec, &unitVec);

	VmathMatrix4 rotation;
	vmathM4MakeRotationAxis(&rotation, (M_PI/180)*angle, &unitVec);

	VmathMatrix4 result;
	vmathM4Mul(&result, _opengl_state.curr_mtx, &rotation);
	vmathM4Copy(_opengl_state.curr_mtx, &result);
}

void glRotated( GLdouble angle, GLdouble x, GLdouble y, GLdouble z )
{
	glRotatef((GLfloat)angle, (GLfloat)x, (GLfloat)y, (GLfloat)z);
}

void glScalef( GLfloat x, GLfloat y, GLfloat z )
{
	VmathVector3 scale;
	vmathV3MakeFromElems(&scale, x, y, z);

	VmathMatrix4 result;
	vmathM4AppendScale(&result, _opengl_state.curr_mtx, &scale);
	vmathM4Copy(_opengl_state.curr_mtx, &result);
}

void glScaled( GLdouble x, GLdouble y, GLdouble z )
{
	glScalef((GLfloat)x, (GLfloat)y, (GLfloat)z);
}


void glTranslatef( GLfloat x, GLfloat y, GLfloat z )
{
	VmathVector3 translation;
	vmathV3MakeFromElems(&translation, x, y, z);
	
    VmathMatrix4 translationMatrix;
	vmathM4MakeIdentity(&translationMatrix);
    vmathM4MakeTranslation(&translationMatrix, &translation);

	VmathMatrix4 result;
	vmathM4Mul(&result, _opengl_state.curr_mtx, &translationMatrix);
	vmathM4Copy(_opengl_state.curr_mtx, &result);
}

void glTranslated( GLdouble x, GLdouble y, GLdouble z )
{
	glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
}


/*
 * Drawing Functions
 */
 
void _setup_draw_env(void);
void glBegin(GLenum mode)
{
	_setup_draw_env();
	rsxDrawVertexBegin(context, mode+1);
}

void glEnd(void)
{
	rsxDrawVertexEnd(context);
}

void glVertex2f(GLfloat x, GLfloat y)
{
	GLfloat v[2] = {x,y};
	rsxDrawVertex2f(context, GCM_VERTEX_ATTRIB_POS, v);
}

void glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
	GLfloat v[3] = {x,y, z};
	rsxDrawVertex3f(context, GCM_VERTEX_ATTRIB_POS, v);
}

void glVertex3fv(const GLfloat *v)
{
	rsxDrawVertex3f(context, GCM_VERTEX_ATTRIB_POS, v);
}

void glNormal3f( GLfloat nx, GLfloat ny, GLfloat nz )
{
	// Stubbed until Lighting is added
#if 0
	GLfloat v[3] = {nx,ny,nz};
	rsxDrawVertex3f(context, GCM_VERTEX_ATTRIB_NORMAL, v);
#endif
}

void glColor3f( GLfloat red, GLfloat green, GLfloat blue )
{
	GLfloat v[3] = {red,green,blue};
	rsxDrawVertex3f(context, GCM_VERTEX_ATTRIB_COLOR0, v);
}

void glColor4f( GLfloat red, GLfloat green,
                                   GLfloat blue, GLfloat alpha )
{
	GLfloat v[4] = {red,green,blue,alpha};
	rsxDrawVertex4f(context, GCM_VERTEX_ATTRIB_COLOR0, v);
}

void glColor4ub( GLubyte red, GLubyte green,
                                   GLubyte blue, GLubyte alpha ){
	GLubyte v[4] = {red,green,blue,alpha};
	rsxDrawVertex4ub(context, GCM_VERTEX_ATTRIB_COLOR0, v);
}

void glColor4fv(const GLfloat * v)
{
	rsxDrawVertex4f(context, GCM_VERTEX_ATTRIB_COLOR0, v);
}

void glColor4ubv(const GLubyte * v)
{
	rsxDrawVertex4ub(context, GCM_VERTEX_ATTRIB_COLOR0, v);
}


void glTexCoord2f(GLfloat s, GLfloat t)
{
	GLfloat v[2] = {s,t};
	rsxDrawVertex2f(context, GCM_VERTEX_ATTRIB_TEX0, v);
}

/*
 * Lighting
 */
 void glShadeModel( GLenum mode )
 {
	switch(mode)
	{
		case GL_FLAT:
			_opengl_state.shade_model = GCM_SHADE_MODEL_FLAT;
			break;
		case GL_SMOOTH:
			_opengl_state.shade_model = GCM_SHADE_MODEL_SMOOTH;
			break;
 	}
 }

/*
 * Stenciling
 */
void glClearStencil(GLint s)
{
	_opengl_state.clear_stencil = s;
}

/*
 * Texture mapping
 */

void glTexEnvi( GLenum target, GLenum pname, GLint param )
{
	switch(target)
	{
		case GL_TEXTURE_ENV:
			switch(pname)
			{
				case GL_TEXTURE_ENV_MODE:
					switch(param)
					{
						case GL_REPLACE:
						_opengl_state.texEnvMode = PS3GL_TEXENV_REPLACE;
						break;
						case GL_MODULATE:
						_opengl_state.texEnvMode = PS3GL_TEXENV_MODULATE;
						break;
						case GL_BLEND:
						_opengl_state.texEnvMode = PS3GL_TEXENV_BLEND;
						break;
					}
					break;
				default:
					break;
			}
		default:
			break;
	}
}

void glTexEnvf( GLenum target, GLenum pname, GLfloat param )
{
	switch(target)
	{
		case GL_TEXTURE_ENV:
			switch(pname)
			{
				default:
					glTexEnvi(target, pname, (GLint)param);
					break;
			}
		default:
			break;
	}
}

void glTexParameteri( GLenum target, GLenum pname, GLint param )
{
		switch(pname)
		{
			case GL_TEXTURE_MIN_FILTER:
				switch(param)
				{
					case GL_NEAREST:
						activeBoundTex()->minFilter = GCM_TEXTURE_NEAREST;
						break;
					case GL_LINEAR:
						activeBoundTex()->minFilter = GCM_TEXTURE_LINEAR;
						break;
					case GL_NEAREST_MIPMAP_NEAREST:
						activeBoundTex()->minFilter = GCM_TEXTURE_NEAREST_MIPMAP_NEAREST;
						break;
					case GL_LINEAR_MIPMAP_NEAREST:
						activeBoundTex()->minFilter = GCM_TEXTURE_LINEAR_MIPMAP_NEAREST;
						break;
					case GL_NEAREST_MIPMAP_LINEAR:
						activeBoundTex()->minFilter = GCM_TEXTURE_NEAREST_MIPMAP_LINEAR;
						break;
					case GL_LINEAR_MIPMAP_LINEAR:
						activeBoundTex()->minFilter = GCM_TEXTURE_LINEAR_MIPMAP_LINEAR;
						break;
					
				}
				break;
		case GL_TEXTURE_MAG_FILTER:
			if(param == GL_NEAREST) 
				activeBoundTex()->magFilter = GCM_TEXTURE_NEAREST;
			if(param == GL_LINEAR) 
				activeBoundTex()->magFilter = GCM_TEXTURE_LINEAR;
			break;
		case GL_TEXTURE_WRAP_S:
			switch(param)
			{
				case GL_CLAMP_TO_EDGE:
					activeBoundTex()->wrapS = GCM_TEXTURE_CLAMP_TO_EDGE;
					break;
				case GL_CLAMP_TO_BORDER:
					activeBoundTex()->wrapS = GCM_TEXTURE_CLAMP_TO_BORDER;
					break;
				case GL_MIRRORED_REPEAT:
					activeBoundTex()->wrapS = GCM_TEXTURE_MIRRORED_REPEAT;
					break;
				case GL_REPEAT:
					activeBoundTex()->wrapS = GCM_TEXTURE_REPEAT;
					break;
				case GL_MIRROR_CLAMP_TO_EDGE:
					activeBoundTex()->wrapS = GCM_TEXTURE_MIRROR_CLAMP_TO_EDGE;
					break;
				}
			break;
		case GL_TEXTURE_WRAP_T:
			switch(param)
			{
				case GL_CLAMP_TO_EDGE:
					activeBoundTex()->wrapT = GCM_TEXTURE_CLAMP_TO_EDGE;
					break;
				case GL_CLAMP_TO_BORDER:
					activeBoundTex()->wrapT = GCM_TEXTURE_CLAMP_TO_BORDER;
					break;
				case GL_MIRRORED_REPEAT:
					activeBoundTex()->wrapT = GCM_TEXTURE_MIRRORED_REPEAT;
					break;
				case GL_REPEAT:
					activeBoundTex()->wrapT = GCM_TEXTURE_REPEAT;
					break;
				case GL_MIRROR_CLAMP_TO_EDGE:
					activeBoundTex()->wrapT = GCM_TEXTURE_MIRROR_CLAMP_TO_EDGE;
					break;
			}
			break;
		case GL_TEXTURE_WRAP_R:
			switch(param)
			{
				case GL_CLAMP_TO_EDGE:
					activeBoundTex()->wrapR = GCM_TEXTURE_CLAMP_TO_EDGE;
					break;
				case GL_CLAMP_TO_BORDER:
					activeBoundTex()->wrapR = GCM_TEXTURE_CLAMP_TO_BORDER;
					break;
				case GL_MIRRORED_REPEAT:
					activeBoundTex()->wrapR = GCM_TEXTURE_MIRRORED_REPEAT;
					break;
				case GL_REPEAT:
					activeBoundTex()->wrapR = GCM_TEXTURE_REPEAT;
					break;
				case GL_MIRROR_CLAMP_TO_EDGE:
					activeBoundTex()->wrapR = GCM_TEXTURE_MIRROR_CLAMP_TO_EDGE;
					break;
			}
			break;
	}
}
void glTexParameterf( GLenum target, GLenum pname, GLfloat param )
{
	switch (pname) {
		default:
			glTexParameteri(target, pname, (GLint)param);
			break;
	}
}

void glTexImage2D( GLenum target, GLint level,
                   GLint internalFormat,
                   GLsizei width, GLsizei height,
                   GLint border, GLenum format, GLenum type,
                   const GLvoid *pixels )
{
	if (activeBoundTex() == NULL)
		return;

	if(pixels == NULL) return;

	struct ps3gl_texture *currentTexture = activeBoundTex();
	currentTexture->gcmTexture.width = width;
	currentTexture->gcmTexture.height = height;
	currentTexture->gcmTexture.depth = 1;
	currentTexture->gcmTexture.mipmap = 1;
	currentTexture->gcmTexture.location = GCM_LOCATION_RSX;

	switch(target)
		{
			case GL_TEXTURE_1D:
				activeBoundTex()->gcmTexture.dimension = GCM_TEXTURE_DIMS_1D;
				break;
			default:
			case GL_TEXTURE_2D:
				activeBoundTex()->gcmTexture.dimension = GCM_TEXTURE_DIMS_2D;
				break;
			case GL_TEXTURE_3D:
				activeBoundTex()->gcmTexture.dimension = GCM_TEXTURE_DIMS_3D;
				break;
		}

	switch (internalFormat) {
		case GL_RGB: // There are no 24bpp textures in RSX
		case 3:
		case GL_RGBA:
		case 4:
			currentTexture->gcmTexture.pitch = width*4;
			break;
		case GL_RED:
		case 1:
			currentTexture->gcmTexture.pitch = width; // one byte per texel
			break;
	}

	switch(format)
	{
		case GL_RGB:
		{
			if (currentTexture->data != NULL)
				rsxFree(currentTexture->data);
			const uint8_t *src = (const uint8_t*)pixels;
			const int textureSize = width*height*4;
			currentTexture->data = (uint8_t*)rsxMemalign(128, textureSize);
			for(size_t i=0; i<width*height*4; i+=4) {
				((uint8_t*)currentTexture->data)[i + 0] = 0xFF;   // A
				((uint8_t*)currentTexture->data)[i + 1] = *src++; // R
				((uint8_t*)currentTexture->data)[i + 2] = *src++; // G
				((uint8_t*)currentTexture->data)[i + 3] = *src++; // B
			}
			rsxAddressToOffset(currentTexture->data, &currentTexture->gcmTexture.offset);
			currentTexture->gcmTexture.format = GCM_TEXTURE_FORMAT_A8R8G8B8|GCM_TEXTURE_FORMAT_LIN;
			currentTexture->gcmTexture.remap  = (
						   (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_A_SHIFT) |
						   (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_R_SHIFT) |
						   (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_G_SHIFT) |
						   (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_B_SHIFT) |
						   (GCM_TEXTURE_REMAP_COLOR_A << GCM_TEXTURE_REMAP_COLOR_A_SHIFT) |
						   (GCM_TEXTURE_REMAP_COLOR_R << GCM_TEXTURE_REMAP_COLOR_R_SHIFT) |
						   (GCM_TEXTURE_REMAP_COLOR_G << GCM_TEXTURE_REMAP_COLOR_G_SHIFT) |
						   (GCM_TEXTURE_REMAP_COLOR_B << GCM_TEXTURE_REMAP_COLOR_B_SHIFT)
			);
			break;
		}
		case GL_RGBA:
		{
			if (currentTexture->data != NULL)
				rsxFree(currentTexture->data);
			currentTexture->data = (uint8_t*)rsxMemalign(128, width*height*4);
			if(pixels)
				memcpy((void*)currentTexture->data, pixels, width*height*4);
			rsxAddressToOffset(currentTexture->data, &currentTexture->gcmTexture.offset);
			currentTexture->gcmTexture.format = GCM_TEXTURE_FORMAT_A8R8G8B8|GCM_TEXTURE_FORMAT_LIN;
			currentTexture->gcmTexture.remap  = (
						   (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_A_SHIFT) |
						   (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_R_SHIFT) |
						   (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_G_SHIFT) |
						   (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_B_SHIFT) |
						   (GCM_TEXTURE_REMAP_COLOR_A << GCM_TEXTURE_REMAP_COLOR_R_SHIFT) |
						   (GCM_TEXTURE_REMAP_COLOR_R << GCM_TEXTURE_REMAP_COLOR_G_SHIFT) |
						   (GCM_TEXTURE_REMAP_COLOR_G << GCM_TEXTURE_REMAP_COLOR_B_SHIFT) |
						   (GCM_TEXTURE_REMAP_COLOR_B << GCM_TEXTURE_REMAP_COLOR_A_SHIFT)
			);
			break;
		}
		case GL_RED:
		{
			if (type != GL_UNSIGNED_BYTE) {
				printf("GL_RED: only GL_UNSIGNED_BYTE supported, got %u\n", type);
				return;
			}
			if (currentTexture->data != NULL)
				rsxFree(currentTexture->data);
			const int textureSize = width * height; // one byte per texel
			currentTexture->data = (uint8_t*)rsxMemalign(128, textureSize);
			memcpy((void*)currentTexture->data, pixels, textureSize);
			rsxAddressToOffset(currentTexture->data, &currentTexture->gcmTexture.offset);
			// RSX stores B8 in the blue source channel.
			// Remap so the shader reads (byte, 0, 0, 1) to match GL_RED's spec semantic.
			currentTexture->gcmTexture.format = GCM_TEXTURE_FORMAT_B8 | GCM_TEXTURE_FORMAT_LIN;
			currentTexture->gcmTexture.remap  = (
						   (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_R_SHIFT) |
						   (GCM_TEXTURE_REMAP_TYPE_ZERO  << GCM_TEXTURE_REMAP_TYPE_G_SHIFT) |
						   (GCM_TEXTURE_REMAP_TYPE_ZERO  << GCM_TEXTURE_REMAP_TYPE_B_SHIFT) |
						   (GCM_TEXTURE_REMAP_TYPE_ONE   << GCM_TEXTURE_REMAP_TYPE_A_SHIFT) |
						   // For .r output, select B8's only populated channel (blue).
						   (GCM_TEXTURE_REMAP_COLOR_B << GCM_TEXTURE_REMAP_COLOR_R_SHIFT) |
						   (GCM_TEXTURE_REMAP_COLOR_R << GCM_TEXTURE_REMAP_COLOR_G_SHIFT) |
						   (GCM_TEXTURE_REMAP_COLOR_G << GCM_TEXTURE_REMAP_COLOR_B_SHIFT) |
						   (GCM_TEXTURE_REMAP_COLOR_A << GCM_TEXTURE_REMAP_COLOR_A_SHIFT)
			);
			break;
		}
		default:
			printf("Unimplemented Texture Format %u\n!!!!!!", format);
	}
}

void glTexSubImage2D( 
	GLenum target, GLint level,
    GLint xoffset, GLint yoffset,
    GLsizei width, GLsizei height,
    GLenum format, GLenum type,
    const GLvoid *pixels 
)
{
	
	if (activeBoundTex() == NULL)
		return;

	struct ps3gl_texture *currentTexture = activeBoundTex();
	if(currentTexture->target != target) return;

	uint8_t *transferBuffer = NULL;
	switch(format)
	{
		case GL_RGBA:
		case GL_RGBA8:
		case 4:
		{
			const int textureSize = width*height*4;
			transferBuffer = (uint8_t*)rsxMemalign(128, textureSize);
			u32 transferBufferOffset;
			memcpy((void*)transferBuffer, pixels, width*height*4);
			rsxAddressToOffset(transferBuffer, &transferBufferOffset);
			rsxSetTransferImage(
				context, // context
				GCM_TRANSFER_LOCAL_TO_LOCAL, //mode
				currentTexture->gcmTexture.offset, // dstOffset
				currentTexture->gcmTexture.pitch, // dstPitch
				xoffset, // dstX
				yoffset, // dstY
				transferBufferOffset, // srcOffset
				width*4, // srcPitch
				0, // srcX 
				0, //  srcY
				width, // width
				height, // height
				4 // bpp
			);
			break;
		}
	}
	rsxFinish(context, 1);
	waitFinish();
	if(transferBuffer != NULL)
		rsxFree(transferBuffer);
}

void glGenTextures( GLsizei n, GLuint *textures )
{
	if(textures == NULL || n == 0)
		return;

	for(size_t i = 0; i < n; i++)
	{
		_opengl_state.nextTextureID++;
		GLuint id = _opengl_state.nextTextureID;
		textures[i] = id;
		if(id < MAX_TEXTURES)
		{
			_opengl_state.textures[id].id = id;
			_opengl_state.textures[id].allocated = true;
			_opengl_state.textures[id].minFilter = GCM_TEXTURE_NEAREST_MIPMAP_LINEAR;
			_opengl_state.textures[id].magFilter = GCM_TEXTURE_LINEAR;
			_opengl_state.textures[id].data = NULL;
            _opengl_state.textures[id].target = GL_TEXTURE_2D;
            _opengl_state.textures[id].wrapS = GCM_TEXTURE_REPEAT;
            _opengl_state.textures[id].wrapT = GCM_TEXTURE_REPEAT;
            _opengl_state.textures[id].wrapR = GCM_TEXTURE_REPEAT;
		}
	}
}

void glDeleteTextures( GLsizei n, const GLuint *textures)
{
	for(size_t i = 0; n > i; i++)
	{
		if(_opengl_state.textures[textures[i]].data != NULL)
			rsxFree(_opengl_state.textures[textures[i]].data);
		_opengl_state.textures[textures[i]].data = NULL;
		_opengl_state.textures[textures[i]].allocated = false;
	}
}

void glBindTexture( GLenum target, GLuint texture )
{

	if(texture < MAX_TEXTURES && !_opengl_state.textures[texture].allocated) {
		_opengl_state.textures[texture].id = texture;
		_opengl_state.textures[texture].allocated = true;
		_opengl_state.textures[texture].minFilter = GCM_TEXTURE_NEAREST_MIPMAP_LINEAR;
		_opengl_state.textures[texture].magFilter = GCM_TEXTURE_LINEAR;
		_opengl_state.textures[texture].data = NULL;
        _opengl_state.textures[texture].target = GL_TEXTURE_2D;
        _opengl_state.textures[texture].wrapS = GCM_TEXTURE_REPEAT;
        _opengl_state.textures[texture].wrapT = GCM_TEXTURE_REPEAT;
        _opengl_state.textures[texture].wrapR = GCM_TEXTURE_REPEAT;
	}

    if (texture < MAX_TEXTURES && _opengl_state.textures[texture].allocated) {
        _opengl_state.textures[texture].target = target;
        _opengl_state.textures[texture].gcmTexture.cubemap = (target == GL_TEXTURE_CUBE_MAP);
    }

    _opengl_state.bound_textures[_opengl_state.active_texture_unit] = &_opengl_state.textures[texture];
}

void glActiveTexture(GLenum texture)
{
	GLuint unit = (GLuint)(texture - GL_TEXTURE0);
	if (unit >= MAX_TEX_UNITS) return;
	_opengl_state.active_texture_unit = unit;
}

/*
 * Fog
 */

void glFogi( GLenum pname, GLint param )
{
	switch(pname)
	{
		case GL_FOG_MODE:
			_opengl_state.fog_mode = param;
			break;
		case GL_FOG_START:
		case GL_FOG_END:
		case GL_FOG_DENSITY:
			glFogf(pname, (GLfloat)param);
			break;
		default:
			break;
	}
}

void glFogf( GLenum pname, GLfloat param )
{

	switch(pname)
	{
		case GL_FOG_MODE:
			glFogi(pname, (GLint)param);
			break;
		case GL_FOG_START:
			_opengl_state.fog_start = param;
			break;
		case GL_FOG_END:
			_opengl_state.fog_end = param;
			break;
		case GL_FOG_DENSITY:
			_opengl_state.fog_density = param;
			break;
		default:
			break;
	}
}

void glFogfv( GLenum pname, const GLfloat *params )
{
	if(pname == GL_FOG_COLOR)
	{
		if(params != NULL)
			memcpy(_opengl_state.fog_color, params, 4*sizeof(GLfloat));
	}
}

void glBlendEquation( GLenum mode )
{
	_opengl_state.blend_equation = mode;
}

void glBlendColor( GLclampf red, GLclampf green,
                                    GLclampf blue, GLclampf alpha )
{
	// Used in rsxSetBlendColor
	_opengl_state.blend_color_rsx =
	 (((uint8_t)(alpha * 255.0f)) << 24) |
    (((uint8_t)(red   * 255.0f)) << 16) |
    (((uint8_t)(green * 255.0f)) << 8)  |
    (((uint8_t)(blue  * 255.0f)) << 0);
}


/*
 * Shaders / Programs
 */

static struct ps3gl_shader* lookupShader(GLuint id)
{
	if (id == 0 || id >= MAX_SHADERS) return NULL;
	if (!_opengl_state.shaders[id].allocated) return NULL;
	return &_opengl_state.shaders[id];
}

static struct ps3gl_program* lookupProgram(GLuint id)
{
	if (id == 0 || id >= MAX_PROGRAMS) return NULL;
	if (!_opengl_state.programs[id].allocated) return NULL;
	return &_opengl_state.programs[id];
}

GLuint glCreateShader(GLenum type)
{
	if (type != GL_VERTEX_SHADER && type != GL_FRAGMENT_SHADER) return 0;
	for (GLuint i = 1; i < MAX_SHADERS; i++) {
		if (!_opengl_state.shaders[i].allocated) {
			struct ps3gl_shader *s = &_opengl_state.shaders[i];
			s->allocated = true;
			s->type = type;
			s->blob = NULL;
			s->blobSize = 0;
			s->fpUcode = NULL;
			s->fpOffset = 0;
			return i;
		}
	}
	return 0;
}

void glShaderBinary(GLsizei count, const GLuint *shaders, GLenum binaryFormat, const void *binary, GLsizei length)
{
	if (count != 1 || shaders == NULL || binary == NULL || length <= 0) return;
	struct ps3gl_shader *s = lookupShader(shaders[0]);
	if (s == NULL) return;

	if (binaryFormat == PS3GL_SHADER_BINARY_VPO && s->type != GL_VERTEX_SHADER) return;
	if (binaryFormat == PS3GL_SHADER_BINARY_FPO && s->type != GL_FRAGMENT_SHADER) return;

	if (s->blob != NULL) free(s->blob);
	s->blob = malloc(length);
	memcpy(s->blob, binary, length);
	s->blobSize = length;

	if (s->type == GL_FRAGMENT_SHADER) {
		// FP ucode must live in RSX-visible memory.
		rsxFragmentProgram *prog = (rsxFragmentProgram*)s->blob;
		void *ucode = NULL;
		u32 ucodeSize = 0;
		rsxFragmentProgramGetUCode(prog, &ucode, &ucodeSize);

		if (s->fpUcode != NULL) rsxFree(s->fpUcode);
		s->fpUcode = rsxMemalign(64, ucodeSize);
		memcpy(s->fpUcode, ucode, ucodeSize);
		rsxAddressToOffset(s->fpUcode, &s->fpOffset);
	}
}

void glDeleteShader(GLuint shader)
{
	struct ps3gl_shader *s = lookupShader(shader);
	if (s == NULL) return;
	if (s->blob != NULL) { free(s->blob); s->blob = NULL; }
	if (s->fpUcode != NULL) { rsxFree(s->fpUcode); s->fpUcode = NULL; }
	s->allocated = false;
}

void glGetShaderiv(GLuint shader, GLenum pname, GLint *params)
{
	if (params == NULL) return;
	struct ps3gl_shader *s = lookupShader(shader);
	if (pname == GL_COMPILE_STATUS) {
		*params = (s != NULL && s->blob != NULL) ? GL_TRUE : GL_FALSE;
	} else if (pname == GL_INFO_LOG_LENGTH) {
		*params = 0;
	}
}

GLuint glCreateProgram(void)
{
	for (GLuint i = 1; MAX_PROGRAMS > i; i++) {
		if (!_opengl_state.programs[i].allocated) {
			struct ps3gl_program *p = &_opengl_state.programs[i];
			p->allocated = true;
			p->linked = false;
			p->vertexShader = 0;
			p->fragmentShader = 0;
			p->uniformCount = 0;
			return i;
		}
	}
	return 0;
}

void glAttachShader(GLuint program, GLuint shader)
{
	struct ps3gl_program *p = lookupProgram(program);
	struct ps3gl_shader *s = lookupShader(shader);
	if (p == NULL || s == NULL) return;
	if (s->type == GL_VERTEX_SHADER) p->vertexShader = shader;
	else if (s->type == GL_FRAGMENT_SHADER) p->fragmentShader = shader;
	p->linked = false;
}

void glDetachShader(GLuint program, GLuint shader)
{
	struct ps3gl_program *p = lookupProgram(program);
	if (p == NULL) return;
	if (p->vertexShader == shader) p->vertexShader = 0;
	if (p->fragmentShader == shader) p->fragmentShader = 0;
	p->linked = false;
}

// cgcomp's eparams enum (PSL1GHT/tools/cgcomp/include/parser.h) packs sampler types into the range PARAM_SAMPLER1D..PARAM_SAMPLERSHADOWRECT == 14..21.
// These values are baked into every compiled .fpo and effectively stable.
#define PS3GL_PARAM_SAMPLER_FIRST 14
#define PS3GL_PARAM_SAMPLER_LAST  21

void glLinkProgram(GLuint program)
{
	struct ps3gl_program *p = lookupProgram(program);
	if (p == NULL) return;

	struct ps3gl_shader *vs = lookupShader(p->vertexShader);
	struct ps3gl_shader *fs = lookupShader(p->fragmentShader);
	// At least one stage must be attached.
	// The unattached stage falls back to the FFP shader at draw time.
	if ((vs == NULL || vs->blob == NULL) && (fs == NULL || fs->blob == NULL)) {
		p->linked = false;
		return;
	}

	p->uniformCount = 0;

	// The OpenGL spec says that samplers default to texture unit 0 at program creation.
	// To match this behavior, we'll prepopulate all sampler attribs to 0 to avoid unforeseen consequences.
	if (fs != NULL && fs->blob != NULL) {
		rsxFragmentProgram *fp = (rsxFragmentProgram*) fs->blob;
		u16 numAttr = rsxFragmentProgramGetNumAttrib(fp);
		rsxProgramAttrib *attribs = rsxFragmentProgramGetAttribs(fp);
		for (u16 i = 0; numAttr > i; i++) {
			if (p->uniformCount >= MAX_PROGRAM_UNIFORMS)
				break;
			if (!attribs[i].name_off)
				continue;
			if (PS3GL_PARAM_SAMPLER_FIRST > attribs[i].type)
				continue;
			if (attribs[i].type > PS3GL_PARAM_SAMPLER_LAST)
				continue;

			const char *name = ((const char*) fp) + attribs[i].name_off;
			struct ps3gl_program_uniform *u = &p->uniforms[p->uniformCount];
			strncpy(u->name, name, sizeof(u->name) - 1);
			u->name[sizeof(u->name) - 1] = '\0';
			u->stage = PS3GL_LOC_STAGE_FP;
			u->constHandle = NULL;
			u->samplerUnit = 0;
			u->samplerAttrib = &attribs[i];
			p->uniformCount++;
		}
	}

	p->linked = true;
}

void glUseProgram(GLuint program)
{
	if (program == 0) { _opengl_state.active_program = 0; return; }
	struct ps3gl_program *p = lookupProgram(program);
	if (p == NULL || !p->linked) return;
	_opengl_state.active_program = program;
}

void glDeleteProgram(GLuint program)
{
	struct ps3gl_program *p = lookupProgram(program);
	if (p == NULL) return;
	if (_opengl_state.active_program == program) _opengl_state.active_program = 0;
	p->allocated = false;
	p->linked = false;
}

void glGetProgramiv(GLuint program, GLenum pname, GLint *params)
{
	if (params == NULL) return;
	struct ps3gl_program *p = lookupProgram(program);
	if (pname == GL_LINK_STATUS) {
		*params = (p != NULL && p->linked) ? GL_TRUE : GL_FALSE;
	} else if (pname == GL_INFO_LOG_LENGTH) {
		*params = 0;
	}
}

// Find or insert a uniform slot for a given name on a linked program.
static GLint resolveUniform(struct ps3gl_program *p, const GLchar *name)
{
	for (GLuint i = 0; p->uniformCount > i; i++) {
		if (strcmp(p->uniforms[i].name, name) == 0) {
			return PS3GL_LOC_PACK(p->uniforms[i].stage, i);
		}
	}
	if (p->uniformCount >= MAX_PROGRAM_UNIFORMS) return -1;

	struct ps3gl_shader *vs = lookupShader(p->vertexShader);
	struct ps3gl_shader *fs = lookupShader(p->fragmentShader);

	// Samplers are pre-populated by glLinkProgram, so they'll always hit the cache above.
	// Only non-sampler FP consts need lazy resolution here.
	if (fs != NULL && fs->blob != NULL) {
		rsxFragmentProgram *fp = (rsxFragmentProgram*)fs->blob;
		rsxProgramConst *c = rsxFragmentProgramGetConst(fp, (char*)name);
		if (c != NULL) {
			struct ps3gl_program_uniform *u = &p->uniforms[p->uniformCount];
			strncpy(u->name, name, sizeof(u->name) - 1);
			u->name[sizeof(u->name) - 1] = '\0';
			u->stage = PS3GL_LOC_STAGE_FP;
			u->constHandle = c;
			u->samplerUnit = -1;
			u->samplerAttrib = NULL;
			return PS3GL_LOC_PACK(PS3GL_LOC_STAGE_FP, p->uniformCount++);
		}
	}

	if (vs != NULL && vs->blob != NULL) {
		rsxVertexProgram *vp = (rsxVertexProgram*)vs->blob;
		rsxProgramConst *c = rsxVertexProgramGetConst(vp, (char*)name);
		if (c != NULL) {
			struct ps3gl_program_uniform *u = &p->uniforms[p->uniformCount];
			strncpy(u->name, name, sizeof(u->name) - 1);
			u->name[sizeof(u->name) - 1] = '\0';
			u->stage = PS3GL_LOC_STAGE_VP;
			u->constHandle = c;
			u->samplerUnit = -1;
			u->samplerAttrib = NULL;
			return PS3GL_LOC_PACK(PS3GL_LOC_STAGE_VP, p->uniformCount++);
		}
	}

	return -1;
}

GLint glGetUniformLocation(GLuint program, const GLchar *name)
{
	struct ps3gl_program *p = lookupProgram(program);
	if (p == NULL || !p->linked || name == NULL) return -1;
	return resolveUniform(p, name);
}

GLint glGetAttribLocation(GLuint program, const GLchar *name)
{
	struct ps3gl_program *p = lookupProgram(program);
	if (p == NULL || !p->linked || name == NULL) return -1;
	struct ps3gl_shader *vs = lookupShader(p->vertexShader);
	if (vs == NULL || vs->blob == NULL) return -1;
	rsxProgramAttrib *a = rsxVertexProgramGetAttrib((rsxVertexProgram*)vs->blob, (char*)name);
	if (a == NULL) return -1;
	return a->index;
}

static struct ps3gl_program_uniform* uniformAt(GLint location)
{
	if (location < 0) return NULL;
	struct ps3gl_program *p = lookupProgram(_opengl_state.active_program);
	if (p == NULL) return NULL;
	GLuint idx = PS3GL_LOC_INDEX(location);
	if (idx >= p->uniformCount) return NULL;
	return &p->uniforms[idx];
}

static struct ps3gl_shader* activeShaderForStage(GLubyte stage)
{
	struct ps3gl_program *p = lookupProgram(_opengl_state.active_program);
	if (p == NULL) return NULL;
	return lookupShader(stage == PS3GL_LOC_STAGE_VP ? p->vertexShader : p->fragmentShader);
}

void glUniform1i(GLint location, GLint v0)
{
	struct ps3gl_program_uniform *u = uniformAt(location);
	if (u == NULL) return;
	// TODO: Implement sampler attributes!
	if (u->samplerAttrib != NULL) {
		u->samplerUnit = v0;
		return;
	}
	struct ps3gl_shader *s = activeShaderForStage(u->stage);
	if (s == NULL || s->blob == NULL) return;
	if (u->stage == PS3GL_LOC_STAGE_FP) {
		rsxSetFragmentProgramParameterF32(context, (rsxFragmentProgram*)s->blob, u->constHandle, (float)v0, s->fpOffset, GCM_LOCATION_RSX);
	} else {
		float v = (float)v0;
		rsxSetVertexProgramParameter(context, (rsxVertexProgram*)s->blob, u->constHandle, &v);
	}
}

void glUniform1f(GLint location, GLfloat v0)
{
	struct ps3gl_program_uniform *u = uniformAt(location);
	if (u == NULL || u->constHandle == NULL) return;
	struct ps3gl_shader *s = activeShaderForStage(u->stage);
	if (s == NULL || s->blob == NULL) return;
	if (u->stage == PS3GL_LOC_STAGE_FP) {
		rsxSetFragmentProgramParameterF32(context, (rsxFragmentProgram*)s->blob, u->constHandle, v0, s->fpOffset, GCM_LOCATION_RSX);
	} else {
		float v[4] = {v0, 0, 0, 0};
		rsxSetVertexProgramParameter(context, (rsxVertexProgram*)s->blob, u->constHandle, v);
	}
}

void glUniform4fv(GLint location, GLsizei count, const GLfloat *value)
{
	if (count != 1 || value == NULL) return;
	struct ps3gl_program_uniform *u = uniformAt(location);
	if (u == NULL || u->constHandle == NULL) return;
	struct ps3gl_shader *s = activeShaderForStage(u->stage);
	if (s == NULL || s->blob == NULL) return;
	if (u->stage == PS3GL_LOC_STAGE_FP) {
		rsxSetFragmentProgramParameterF32Vec4(context, (rsxFragmentProgram*)s->blob, u->constHandle, (float*)value, s->fpOffset, GCM_LOCATION_RSX);
	} else {
		rsxSetVertexProgramParameter(context, (rsxVertexProgram*)s->blob, u->constHandle, (float*)value);
	}
}

void glUniform2f(GLint location, GLfloat v0, GLfloat v1)
{
	GLfloat v[4] = {v0, v1, 0, 0};
	glUniform4fv(location, 1, v);
}

void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2)
{
	GLfloat v[4] = {v0, v1, v2, 0};
	glUniform4fv(location, 1, v);
}

void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)
{
	GLfloat v[4] = {v0, v1, v2, v3};
	glUniform4fv(location, 1, v);
}

void glUniform3fv(GLint location, GLsizei count, const GLfloat *value)
{
	if (count != 1 || value == NULL) return;
	GLfloat v[4] = {value[0], value[1], value[2], 0};
	glUniform4fv(location, 1, v);
}

void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
	if (count != 1 || value == NULL || transpose) return;
	struct ps3gl_program_uniform *u = uniformAt(location);
	if (u == NULL || u->constHandle == NULL || u->stage != PS3GL_LOC_STAGE_VP) return;
	struct ps3gl_shader *s = activeShaderForStage(PS3GL_LOC_STAGE_VP);
	if (s == NULL || s->blob == NULL) return;
	rsxSetVertexProgramParameter(context, (rsxVertexProgram*)s->blob, u->constHandle, (float*)value);
}


/* PS3GL Functions */

static void _program_exit_callback(void)
{
	gcmSetWaitFlip(context);
	rsxFinish(context,1);
}

// Bind a GL texture unit's currently-bound texture into the given RSX slot.
static void _ps3gl_bind_unit_to_rsx_slot(GLuint glUnit, int rsxSlot)
{
	struct ps3gl_texture *tex = _opengl_state.bound_textures[glUnit];
	if (tex == NULL) return;

	rsxLoadTexture(context, rsxSlot, &tex->gcmTexture);
	rsxTextureControl(context,
		rsxSlot,
		true,
		0<<8,  // TODO: minLOD
		12<<8, // TODO: maxLOD
		// MAX_ANISO_1 = no anisotropic filtering.
		GCM_TEXTURE_MAX_ANISO_1
	);
	rsxTextureFilter(context,
		rsxSlot,
		0,
		tex->minFilter,
		tex->magFilter,
		// NONE means "no extra convolution on top of min/mag filter."
		GCM_TEXTURE_CONVOLUTION_NONE
	);
	rsxTextureWrapMode(context,
		rsxSlot,
		tex->wrapS,
		tex->wrapT,
		tex->wrapR,
		0,
		GCM_TEXTURE_ZFUNC_LESS,
		0
	);
}

void _ps3gl_load_texture(void)
{
	rsxInvalidateTextureCache(context, GCM_INVALIDATE_TEXTURE);

	if (_opengl_state.active_program == 0) {
		// FFP path: single sampler, hardwired to GL unit 0.
		if (_opengl_state.ffp_tex_unit == NULL) return;
		if (!_opengl_state.texture_unit_enabled[0]) return;
		_ps3gl_bind_unit_to_rsx_slot(0, _opengl_state.ffp_tex_unit->index);
		return;
	}

	// Custom program: walk its cached sampler uniforms.
	// samplerUnit (set by glUniform1i) is the GL-side unit index;
	// samplerAttrib->index is the RSX slot pinned by Cg's : TEXUNITn semantic.
	struct ps3gl_program *p = &_opengl_state.programs[_opengl_state.active_program];
	for (GLuint i = 0; p->uniformCount > i; i++) {
		struct ps3gl_program_uniform *u = &p->uniforms[i];
		if (u->samplerAttrib == NULL) continue;
		GLuint glUnit = (u->samplerUnit >= 0) ? (GLuint) u->samplerUnit : 0;
		if (glUnit >= MAX_TEX_UNITS) continue;
		if (!_opengl_state.texture_unit_enabled[glUnit]) continue;
		_ps3gl_bind_unit_to_rsx_slot(glUnit, u->samplerAttrib->index);
	}
}

void glGetFloatv(GLenum pname, GLfloat *params)
{
	if (params == NULL) return;
	if (pname == GL_MODELVIEW_MATRIX) {
		__builtin_memcpy(params, (float*)&_opengl_state.modelview_matrix, sizeof(float)*16);
	} else if (pname == GL_PROJECTION_MATRIX) {
		__builtin_memcpy(params, (float*)&_opengl_state.projection_matrix, sizeof(float)*16);
	}
}

void _setup_draw_env(void)
{
	rsxSetShadeModel(context, _opengl_state.shade_model);
	rsxSetPointSize(context, _opengl_state.point_size);

	rsxSetColorMask(context, _opengl_state.color_mask);
	rsxSetColorMaskMrt(context,0);

	// Alpha
	rsxSetAlphaTestEnable(context, _opengl_state.alpha_test_enabled);
	rsxSetAlphaFunc(context, _opengl_state.alpha_func, _opengl_state.alpha_func_ref);

	// Blend
	// TODO: BlendFuncSeparate/BlendEquationSeparate
	rsxSetBlendEnable(context, _opengl_state.blend_enabled);
	rsxSetBlendColor(context, _opengl_state.blend_color_rsx, 0);
	rsxSetBlendFunc(context, 
		_opengl_state.blend_func_sfactor, 
		_opengl_state.blend_func_dfactor, 
		_opengl_state.blend_func_sfactor, 
		_opengl_state.blend_func_dfactor
	);
	rsxSetBlendEquation(context, _opengl_state.blend_equation, _opengl_state.blend_equation);
	
	rsxSetCullFaceEnable(context, _opengl_state.cull_face_enabled);
	rsxSetCullFace(context, _opengl_state.cull_face);

	rsxSetLogicOpEnable(context, _opengl_state.logic_op_enabled);
	rsxSetLogicOp(context, _opengl_state.logic_op);

	// Depth Testing
	rsxSetDepthTestEnable(context, _opengl_state.depth_test);
	rsxSetDepthWriteEnable(context, _opengl_state.depth_mask);
	rsxSetDepthFunc(context, _opengl_state.depth_func);

	switch(_opengl_state.polygon_mode_face)
	{
		case GL_FRONT_AND_BACK:
			rsxSetFrontPolygonMode(context, _opengl_state.polygon_mode);
			rsxSetBackPolygonMode(context, _opengl_state.polygon_mode);
			break;
		case GL_FRONT:
			rsxSetFrontPolygonMode(context, _opengl_state.polygon_mode);
			break;
		case GL_BACK:
			rsxSetBackPolygonMode(context, _opengl_state.polygon_mode);
			break;
		default:
			break;
	}

	// Fog
	if(_opengl_state.fog_enabled)
	{
		rsxSetFogMode(context, _opengl_state.fog_mode);
		float p0 = 0;
		float p1 = 0;
		switch(_opengl_state.fog_mode)
		{
			case GCM_FOG_MODE_LINEAR:
				// TODO: Confirm these are right
				p0 = _opengl_state.fog_end/(_opengl_state.fog_end-_opengl_state.fog_start);
				p1 = 1/(_opengl_state.fog_end-_opengl_state.fog_start);
				break;
			// TODO: Figure out EXP and EXP2 params
			default:
				break;

		}
		rsxSetFogParams(context, p0, p1);
	}

	// Viewport and Scissor
	rsxSetViewport(context, 
		_opengl_state.viewport.x, 
		_opengl_state.viewport.y, 
		_opengl_state.viewport.w, 
		_opengl_state.viewport.h, 
		_opengl_state.depth_near,
		_opengl_state.depth_far, 
		_opengl_state.viewport.scale, 
		_opengl_state.viewport.offset
	);

	if(_opengl_state.scissor.enabled)
		rsxSetScissor(context, 
			_opengl_state.scissor.x, 
			_opengl_state.scissor.y, 
			_opengl_state.scissor.w, 
			_opengl_state.scissor.h
		);
	else
			rsxSetScissor(context, 
			_opengl_state.viewport.x, 
			_opengl_state.viewport.y, 
			_opengl_state.viewport.w, 
			_opengl_state.viewport.h
		);

	// Load Current Texture
	_ps3gl_load_texture();

	// Resolve which programs to actually run this draw.
	// If the user has glUseProgram'd a custom program, then use the attached shader for each stage, otherwise fall back to the FFP for that stage.
	rsxVertexProgram *useVpo = vpo;
	void *useVpUcode = vp_ucode;
	rsxFragmentProgram *useFpo = fpo;
	u32 useFpOffset = fp_offset;
	bool ffpFp = true;
	bool ffpVp = true;

	if (_opengl_state.active_program != 0) {
		struct ps3gl_program *p = &_opengl_state.programs[_opengl_state.active_program];
		if (p->allocated && p->linked) {
			if (p->vertexShader != 0) {
				struct ps3gl_shader *vs = &_opengl_state.shaders[p->vertexShader];
				if (vs->allocated && vs->blob != NULL) {
					useVpo = (rsxVertexProgram*)vs->blob;
					u32 sz = 0;
					rsxVertexProgramGetUCode(useVpo, &useVpUcode, &sz);
					ffpVp = false;
				}
			}
			if (p->fragmentShader != 0) {
				struct ps3gl_shader *fs = &_opengl_state.shaders[p->fragmentShader];
				if (fs->allocated && fs->blob != NULL && fs->fpUcode != NULL) {
					useFpo      = (rsxFragmentProgram*)fs->blob;
					useFpOffset = fs->fpOffset;
					ffpFp       = false;
				}
			}
		}
	}

	rsxLoadVertexProgram(context, useVpo, useVpUcode);
	rsxLoadFragmentProgramLocation(context, useFpo, useFpOffset, GCM_LOCATION_RSX);

	if (ffpVp) {
		// FFP vertex program: push the matrix uniforms it expects.
		rsxSetVertexProgramParameter(context, useVpo, _opengl_state.prog_consts[PS3GL_Uniform_ModelViewMatrix],  (float*)&_opengl_state.modelview_matrix);
		rsxSetVertexProgramParameter(context, useVpo, _opengl_state.prog_consts[PS3GL_Uniform_ProjectionMatrix], (float*)&_opengl_state.projection_matrix);
	}

	if (ffpFp) {
		// FFP FP: push its baked uniforms.
		rsxSetFragmentProgramParameterBool(context, useFpo, _opengl_state.prog_consts[PS3GL_Uniform_TextureEnabled], _opengl_state.texture_unit_enabled[0], useFpOffset, GCM_LOCATION_RSX);
		rsxSetFragmentProgramParameterBool(context, useFpo, _opengl_state.prog_consts[PS3GL_Uniform_FogEnabled], _opengl_state.fog_enabled, useFpOffset, GCM_LOCATION_RSX);
		rsxSetFragmentProgramParameterF32(context, useFpo, _opengl_state.prog_consts[PS3GL_Uniform_TextureMode], _opengl_state.texEnvMode, useFpOffset, GCM_LOCATION_RSX);
		rsxSetFragmentProgramParameterF32Vec4(context, useFpo, _opengl_state.prog_consts[PS3GL_Uniform_FogColor],  _opengl_state.fog_color, useFpOffset, GCM_LOCATION_RSX);
	}
}

// TODO: This is a placeholder, replace with good api, closer to vitaGL
// Also move rsxutil.c functionality over to here
void ps3glInit(void)
{
	atexit(_program_exit_callback);
	void *host_addr = memalign(1024*1024,HOST_SIZE);
	init_screen(host_addr,HOST_SIZE);

	u32 vpsize = 0;
	rsxVertexProgramGetUCode(vpo, &vp_ucode, &vpsize);
	_opengl_state.prog_consts[PS3GL_Uniform_ModelViewMatrix] = rsxVertexProgramGetConst(vpo, "uModelViewMatrix");
	_opengl_state.prog_consts[PS3GL_Uniform_ProjectionMatrix] = rsxVertexProgramGetConst(vpo, "uProjectionMatrix");

	u32 fpsize = 0;
	rsxFragmentProgramGetUCode(fpo, &fp_ucode, &fpsize);
	_opengl_state.ffp_tex_unit = rsxFragmentProgramGetAttrib(fpo, "uTextureUnit0");
	_opengl_state.prog_consts[PS3GL_Uniform_TextureEnabled] = rsxFragmentProgramGetConst(fpo, "uTextureEnabled");
	_opengl_state.prog_consts[PS3GL_Uniform_TextureMode] = rsxFragmentProgramGetConst(fpo, "uTextureMode");

	_opengl_state.prog_consts[PS3GL_Uniform_FogEnabled] = rsxFragmentProgramGetConst(fpo, "uFogEnabled");
	_opengl_state.prog_consts[PS3GL_Uniform_FogColor] = rsxFragmentProgramGetConst(fpo, "uFogColor");

	fp_buffer = (u32*)rsxMemalign(64,fpsize);
	memcpy(fp_buffer,fp_ucode,fpsize);
	rsxAddressToOffset(fp_buffer,&fp_offset);

	// Set default state
	glColor3f(1.0f, 1.0f, 1.0f);
	glColorMask(true, true, true, true);

	glPointSize(1.0f);
	glShadeModel(GL_SMOOTH);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glFrontFace(GL_CCW);
	
	glDisable(GL_ALPHA_TEST); 
	glAlphaFunc(GL_ALWAYS, 0);

	glDisable(GL_BLEND); 
	glBlendColor(0.0f, 0.0f, 0.0f, 0.0f);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFunc(GL_ONE, GL_ZERO);

	glDisable(GL_CULL_FACE); 
	glCullFace(GL_BACK);

	glDisable(GL_COLOR_LOGIC_OP); 
	glLogicOp(GL_COPY);

	glDisable(GL_DEPTH_TEST); 
	glDepthFunc(GL_LESS);
	glDepthMask(true);
	glDepthRange(0, 1);

	glDisable(GL_FOG); 
	glDisable(GL_TEXTURE_2D);

	// Clear Values
	glClearColor(0, 0, 0, 0);
	glClearDepth(1.0f);
	glClearStencil(0);

	// Matrices
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();	
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	_opengl_state.cur_proj_mat = 0;
    _opengl_state.cur_modv_mat = 0;


	// Textures
	_opengl_state.nextTextureID = 1;
	_opengl_state.active_texture_unit = 0;
	for (int u = 0; MAX_TEX_UNITS > u; u++) {
		_opengl_state.bound_textures[u] = &_opengl_state.textures[0];
		_opengl_state.texture_unit_enabled[u] = false;
	}
	activeBoundTex()->id = 0;
	activeBoundTex()->allocated = true;
	activeBoundTex()->data = NULL;
	activeBoundTex()->minFilter = GCM_TEXTURE_NEAREST_MIPMAP_LINEAR;
	activeBoundTex()->magFilter = GCM_TEXTURE_LINEAR;
	activeBoundTex()->wrapS = GCM_TEXTURE_REPEAT;
	activeBoundTex()->wrapT = GCM_TEXTURE_REPEAT;
	activeBoundTex()->wrapR = GCM_TEXTURE_REPEAT;
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);


}

void ps3glSwapBuffers(void)
{
	flip();
}
