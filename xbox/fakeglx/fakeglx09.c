/*
====================================================================================================================================

	NEW FAKE GL WRAPPER - MH 2009

	Uses Direct3D 8 for an (almost) seamless replacement of OpenGL.

	It is assumed that your OpenGL code is reasonably correct so far as OpenGL is concerned, and therefore a certain layer
	of error checking and validation that *could* be included is actually omitted.  It is recommended that you test any
	OpenGL code you write using native OpenGL calls before sending it through this wrapper in order to ensure this
	correctness.

	Wherever possible the OpenGL specifications at http://www.opengl.org were used for guidance and interpretation of
	OpenGL calls.  The Direct3D 8 SDK help files and several sample aplications and other sources were used for the D3D
	portion of the code.  Every effort has been made to ensure that my interpretation of things is conformant with
	the published specs of each API.  It has been known for published specs to have bugs, and it has also been known
	for my interpretation to be incorrect, however; so let me know if there is anything that needs fixing.

	Compilation should be relatively seamless, although there are some changes you will need to make to your application
	in order to get the best experience from it.  These include the following items (listed with a GLQuake-derived engine
	in mind):

	- Replace the SwapBuffers call in gl_vidnt.c with FakeSwapBuffers

	YOU WILL NEED THE DIRECTX 8 SDK TO COMPILE THIS.

	It's still available on the internet (but not on microsoft.com) so just search on Google.

	It should compile OK on Visual C++ 2008 (including express) but there are a few steps to complete first.

	- Add the DirectX 8 SDK headers and libs to your Visual C++ directories.  I suggest you place them at the very end of the
	  list, as if you add them towards the top some defines or other files may conflict with the version of the platform SDK
	  used for this IDE.

	- create a fake libci.lib static library project (it can just contain one empty function called what you want, compile it,
	  and place the resulting .lib in your source directory so that d3dx8 can link OK.

====================================================================================================================================
*/

// This is FakeGLx, a WIP XBOX port of FakeGL.

#ifdef _USEFAKEGLX_09

// TODO Fix this warning instead of disabling it
#pragma warning (disable: 4273)

// we get a few signed/unsigned mismatches here
#pragma warning (disable: 4018)
#pragma warning (disable: 4244)

#include <xtl.h>
#include <xgraphics.h>

#include "fakeglx09.h"

extern void SysMessage(const char *fmt, ...);
extern void SetGUID3DDevice(LPDIRECT3DDEVICE8 pD3DDevice, D3DPRESENT_PARAMETERS PresentParams);

// d3d basic stuff
LPDIRECT3D8 d3d_Object = NULL;
LPDIRECT3DDEVICE8 d3d_Device = NULL;

int g_iWidth = 640;
int g_iHeight = 480;
BOOL g_bHDEnabled = FALSE;

// mode definition
int d3d_BPP = -1;
//HWND d3d_Window;

BOOL d3d_RequestStencil = FALSE;

// the desktop and current display modes
D3DDISPLAYMODE d3d_DesktopMode;
D3DDISPLAYMODE d3d_CurrentMode;

// presentation parameters that the device was created with
D3DPRESENT_PARAMETERS d3d_PresentParams;

// capabilities
// these can exist for both the object and the device
D3DCAPS8 d3d_Caps;

// we're going to need this a lot so store it globally to save me having to redeclare it every time
HRESULT hr = S_OK;

// global state defines
DWORD d3d_ClearColor = 0x00000000;
BOOL d3d_DeviceLost = FALSE;
D3DVIEWPORT8 d3d_Viewport;

BOOL g_bScissorTest = FALSE;
D3DRECT g_ScissorRect;

int g_iCurrentTextureID = -1;

// cache float size
float g_fSize16 = sizeof (float) * 16;

//#define BYTE_CLAMP(i) (int) ((((i) > 255) ? 255 : (((i) < 0) ? 0 : (i)))) 
__forceinline byte BYTE_CLAMP(int n)
{
	if (n&(~0xFF))
		return (-n)>>31;

	return n;
}

// Go through the vtable rather than use the macros to make this generic
#define D3D_SAFE_RELEASE(p) {if (p) (p)->lpVtbl->Release (p);  (p) = NULL;}
//#define D3D_SAFE_RELEASE(p) {if (p) (p)->Release();            (p)=NULL;} 

DWORD GL_ColorToD3D (GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
	return D3DCOLOR_ARGB
	(
		BYTE_CLAMP (alpha * 255.0f),
		BYTE_CLAMP (red * 255.0f),
		BYTE_CLAMP (green * 255.0f),
		BYTE_CLAMP (blue * 255.0f)
	);
}

typedef struct d3d_texture_s
{
	// OpenGL number for glBindTexture/glGenTextures/glDeleteTextures/etc
	int glnum;

	// format the texture was uploaded as; 1 or 4 bytes.
	// glTexSubImage2D needs this.
	GLenum internalformat;

	// parameters
	DWORD addressu;
	DWORD addressv;
	DWORD anisotropy;
	DWORD magfilter;
	DWORD minfilter;
	DWORD mipfilter;

	// the texture image itself
	LPDIRECT3DTEXTURE8 teximg;

	struct d3d_texture_s *next;
} d3d_texture_t;

typedef struct SubImage_s
{
	int iTextureNum;
	struct SubImage_s *next;
	LPDIRECT3DSURFACE8 texture;
} SubImage_t;

SubImage_t *SubImageCache = NULL;

unsigned int d3d_TextureExtensionNumber = 1;

// opengl specified up to 32 TMUs, D3D only allows up to 8 stages
// XBOX: Only 4 stages available, max value is therefore 3 -> see docs IDirect3DDevice8::SetTextureStageState

#define D3D_MAX_TMUS	1//8
/*
typedef struct gl_combine_s
{
	DWORD colorop;
	DWORD colorarg1;
	DWORD colorarg2;
	DWORD alphaop;
	DWORD alphaarg1;
	DWORD alphaarg2;
	DWORD colorarg0;
	DWORD alphaarg0;
	DWORD resultarg;
	DWORD colorscale;
	DWORD alphascale;
} gl_combine_t;
*/
typedef struct gl_tmustate_s
{
	d3d_texture_t *boundtexture;
	BOOL enabled;
	// BOOL combine;

	// gl_combine_t combstate;

	// these come from glTexEnv and are properties of the TMU; other D3DTEXTURESTAGESTATETYPEs 
	// come from glTexParameter and are properties of the individual texture.
	DWORD colorop;
	DWORD colorarg1;
	DWORD colorarg2;
	DWORD alphaop;
	DWORD alphaarg1;
	DWORD alphaarg2;
	DWORD texcoordindex;

	BOOL texenvdirty;
	BOOL texparamdirty;
} gl_tmustate_t;

gl_tmustate_t d3d_TMUs[D3D_MAX_TMUS];

int d3d_CurrentTMU = 0;

// used in various places
int D3D_TMUForTexture (GLenum texture)
{
	switch (texture)
	{
	case GLD3D_TEXTURE0: return 0;
	case GLD3D_TEXTURE1: return 1;
	//case GLD3D_TEXTURE2: return 2;
	//case GLD3D_TEXTURE3: return 3;
	//case GLD3D_TEXTURE4: return 4;
	//case GLD3D_TEXTURE5: return 5;
	//case GLD3D_TEXTURE6: return 6;
	//case GLD3D_TEXTURE7: return 7;

	default:
		// ? how best to fail gracefully (if we should fail gracefully at all?)
		return 0;
	}
}

// backface culling
// (fix me - put all of these into a state struct)
GLboolean gl_CullEnable = GL_TRUE;
GLenum gl_CullMode = GL_BACK;
GLenum gl_FrontFace = GL_CCW;

/*
===================================================================================================================

			HINT MECHANISM

	D3D generally doesn't provide a hint mechanism, instead relying on the programmer to explicitly set
	parameters for stuff.  In cases where certain items are in doubt we add hint capability.

===================================================================================================================
*/

// default to per-pixel fog
GLenum d3d_Fog_Hint = GL_NICEST;

void glHint (GLenum target, GLenum mode)
{
	switch (target)
	{
	case GL_FOG_HINT:
		d3d_Fog_Hint = mode;
		break;

	default:
		// ignore other driver hints
		break;
	}
}


/*
===================================================================================================================

			MATRIXES

	Most matrix ops occur in software in D3D; the only part that may (or may not) occur in hardware (depending
	on whether or not we have a hardware vp device) is the final transformation of submitted verts.  Note that
	OpenGL is probably the same.

	This interface just models the most common OpenGL matrix ops with their D3D equivalents.  I don't use the
	IDirect3DXMatrixStack interface because of overhead and baggage associated with it.

	The critical differences are in glGetFloatv and glLoadMatrix; OpenGL uses column major ordering whereas
	D3D uses row major ordering, so we need to translate from one order to the next.

	Also be aware that D3D defines separate world and view matrixes whereas OpenGL concatenates them into a
	single modelview matrix.  To keep things clean and consistent we will always use an identity view matrix
	on a beginscene and will just work off the world matrix.

	Finally, D3D does weird and horrible things in the texture matrix which are better left unsaid for now.

===================================================================================================================
*/

// OpenGL only has a stack depth of 2 for projection, but no reason to not use the full depth
#define MAX_MATRIX_STACK	32

typedef struct d3d_matrix_s
{
	BOOL dirty;
	int stackdepth;
	D3DTRANSFORMSTATETYPE usage;
	D3DXMATRIX stack[MAX_MATRIX_STACK];
} d3d_matrix_t;

// init all matrixes to be dirty and at depth 0 (these will be re-inited each frame)
d3d_matrix_t d3d_ModelViewMatrix = {TRUE, 0, D3DTS_WORLD};
d3d_matrix_t d3d_ProjectionMatrix = {TRUE, 0, D3DTS_PROJECTION};

d3d_matrix_t *d3d_CurrentMatrix = &d3d_ModelViewMatrix;


void D3D_InitializeMatrix (d3d_matrix_t *m)
{
	// initializes a matrix to a known state prior to rendering
	m->dirty = TRUE;
	m->stackdepth = 0;
	D3DXMatrixIdentity (&m->stack[0]);
}


void D3D_CheckDirtyMatrix (d3d_matrix_t *m)
{
	if (m->dirty)
	{
		IDirect3DDevice8_SetTransform (d3d_Device, m->usage, &m->stack[m->stackdepth]);
		m->dirty = FALSE;
	}
}


void glMatrixMode (GLenum mode)
{
	switch (mode)
	{
	case GL_MODELVIEW:
		d3d_CurrentMatrix = &d3d_ModelViewMatrix;
		break;

	case GL_PROJECTION:
		d3d_CurrentMatrix = &d3d_ProjectionMatrix;
		break;

	default:
		SysMessage ("glMatrixMode: unimplemented mode");
		break;
	}
}


void glLoadIdentity (void)
{
	D3DXMatrixIdentity (&d3d_CurrentMatrix->stack[d3d_CurrentMatrix->stackdepth]);
	d3d_CurrentMatrix->dirty = TRUE;
}


void glLoadMatrixf (const GLfloat *m)
{
	memcpy (d3d_CurrentMatrix->stack[d3d_CurrentMatrix->stackdepth].m, m, g_fSize16);
	d3d_CurrentMatrix->dirty = TRUE;
}


void glFrustum (GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
	D3DXMATRIX tmp;

	// per spec, glFrustum multiplies the current matrix by the specified orthographic projection rather than replacing it
	D3DXMatrixPerspectiveOffCenterRH (&tmp, left, right, bottom, top, zNear, zFar);

	D3DXMatrixMultiply (&d3d_CurrentMatrix->stack[d3d_CurrentMatrix->stackdepth], &tmp, &d3d_CurrentMatrix->stack[d3d_CurrentMatrix->stackdepth]);
	d3d_CurrentMatrix->dirty = TRUE;
}


void glOrtho (GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
	D3DXMATRIX tmp;

	// per spec, glOrtho multiplies the current matrix by the specified orthographic projection rather than replacing it
	D3DXMatrixOrthoOffCenterRH (&tmp, left, right, bottom, top, zNear, zFar);

	D3DXMatrixMultiply (&d3d_CurrentMatrix->stack[d3d_CurrentMatrix->stackdepth], &tmp, &d3d_CurrentMatrix->stack[d3d_CurrentMatrix->stackdepth]);
	d3d_CurrentMatrix->dirty = TRUE;
}


void glPopMatrix (void)
{
	if (!d3d_CurrentMatrix->stackdepth)
	{
		// opengl silently allows this and so should we (witness TQ's R_DrawAliasModel, which pushes the
		// matrix once but pops it twice - on line 423 and line 468
		d3d_CurrentMatrix->dirty = TRUE;
		return;
	}

	// go to a new matrix
	d3d_CurrentMatrix->stackdepth--;

	// flag as dirty
	d3d_CurrentMatrix->dirty = TRUE;
}


void glPushMatrix (void)
{
	if (d3d_CurrentMatrix->stackdepth <= (MAX_MATRIX_STACK - 1))
	{
		// go to a new matrix (only push if there's room to push)
		d3d_CurrentMatrix->stackdepth++;

		// copy up the previous matrix (per spec)
		memcpy
		(
			&d3d_CurrentMatrix->stack[d3d_CurrentMatrix->stackdepth],
			&d3d_CurrentMatrix->stack[d3d_CurrentMatrix->stackdepth - 1],
			sizeof (D3DXMATRIX)
		);
	}

	// flag as dirty
	d3d_CurrentMatrix->dirty = TRUE;
}


void glRotatef (GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
	// replicates the OpenGL glRotatef with 3 components and angle in degrees
	D3DXVECTOR3 vec;
	D3DXMATRIX tmp;

	vec.x = x;
	vec.y = y;
	vec.z = z;

	D3DXMatrixRotationAxis (&tmp, &vec, D3DXToRadian (angle));
	D3DXMatrixMultiply (&d3d_CurrentMatrix->stack[d3d_CurrentMatrix->stackdepth], &tmp, &d3d_CurrentMatrix->stack[d3d_CurrentMatrix->stackdepth]);

	// dirty the matrix
	d3d_CurrentMatrix->dirty = TRUE;
}


void glScalef (GLfloat x, GLfloat y, GLfloat z)
{
	D3DXMATRIX tmp;
	D3DXMatrixScaling (&tmp, x, y, z);
	D3DXMatrixMultiply (&d3d_CurrentMatrix->stack[d3d_CurrentMatrix->stackdepth], &tmp, &d3d_CurrentMatrix->stack[d3d_CurrentMatrix->stackdepth]);

	// dirty the matrix
	d3d_CurrentMatrix->dirty = TRUE;
}


void glTranslatef (GLfloat x, GLfloat y, GLfloat z)
{
	D3DXMATRIX tmp;
	D3DXMatrixTranslation (&tmp, x, y, z);
	D3DXMatrixMultiply (&d3d_CurrentMatrix->stack[d3d_CurrentMatrix->stackdepth], &tmp, &d3d_CurrentMatrix->stack[d3d_CurrentMatrix->stackdepth]);

	// dirty the matrix
	d3d_CurrentMatrix->dirty = TRUE;
}


void glMultMatrixf (const GLfloat *m)
{
	D3DXMATRIX mat;

	// copy out
	mat._11 = m[0];
	mat._12 = m[1];
	mat._13 = m[2];
	mat._14 = m[3];

	mat._21 = m[4];
	mat._22 = m[5];
	mat._23 = m[6];
	mat._24 = m[7];

	mat._31 = m[8];
	mat._32 = m[9];
	mat._33 = m[10];
	mat._34 = m[11];

	mat._41 = m[12];
	mat._42 = m[13];
	mat._43 = m[14];
	mat._44 = m[15];

	D3DXMatrixMultiply (&d3d_CurrentMatrix->stack[d3d_CurrentMatrix->stackdepth], &mat, &d3d_CurrentMatrix->stack[d3d_CurrentMatrix->stackdepth]);

	// dirty the matrix
	d3d_CurrentMatrix->dirty = TRUE;
}


/*
===================================================================================================================

			STATE MANAGEMENT

	The D3D runtime will filter states for us anyway as we have selected a non-pure device (intentional even if
	a pure device is available as we need to support certain glGet functions); this just keeps the filtering local

===================================================================================================================
*/

LPDIRECT3DTEXTURE8 d3d_BoundTextures[D3D_MAX_TMUS];

void D3D_SetTexture (int stage, LPDIRECT3DTEXTURE8 texture)
{
	if (d3d_BoundTextures[stage] != texture)
	{
		d3d_BoundTextures[stage] = texture;
	
		IDirect3DDevice8_SetTexture (d3d_Device, stage, (IDirect3DBaseTexture8 *) texture);
	}
}


void D3D_DirtyAllStates (void)
{
	int i;

	// dirty TMUs
	for (i = 0; i < D3D_MAX_TMUS; i++)
	{
		d3d_TMUs[i].texenvdirty = TRUE;
		d3d_TMUs[i].texparamdirty = TRUE;
		d3d_BoundTextures[i] = NULL;
	}

	// and these as well
	d3d_ModelViewMatrix.dirty = TRUE;
	d3d_ProjectionMatrix.dirty = TRUE;
}


// d3d8 specifies 174 render states; here we just provide headroom
DWORD d3d_RenderStates[256];

// 28 stage states per stage
DWORD d3d_TextureStates[D3D_MAX_TMUS][32];

DWORD D3D_FloatToDWORD (float f)
{
	return ((DWORD *) &f)[0];
}


void D3D_SetRenderState (D3DRENDERSTATETYPE state, DWORD value)
{
	// filter state
	if (d3d_RenderStates[(int) state] == value) return;

	// set the state and cache it back
	IDirect3DDevice8_SetRenderState (d3d_Device, state, value);
	d3d_RenderStates[(int) state] = value;
}


void D3D_SetTextureState (DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value)
{
	// filter state
	if (d3d_TextureStates[Stage][(int) Type] == Value) return;

	// set the state and cache it back
	IDirect3DDevice8_SetTextureStageState (d3d_Device, Stage, Type, Value);
	d3d_TextureStates[Stage][(int) Type] = Value;
}


void D3D_SetVertexShader (DWORD shader)
{
	// init this to something invalid so that we know it'll never match
	static DWORD oldshader = D3DFVF_XYZ | D3DFVF_XYZRHW;

	// check for change
	if (shader == oldshader) return;

	// set and cache back
	IDirect3DDevice8_SetVertexShader (d3d_Device, shader);
	oldshader = shader;
}


void D3D_InitTexture (d3d_texture_t *tex)
{
	tex->glnum = 0;

	// note - we can't set the ->next pointer to NULL here as this texture may be a 
	// member of the linked list that has just become free...
	tex->addressu = D3DTADDRESS_WRAP;
	tex->addressv = D3DTADDRESS_WRAP;
	tex->magfilter = D3DTEXF_LINEAR;
	tex->minfilter = D3DTEXF_LINEAR;
	tex->mipfilter = D3DTEXF_LINEAR;
	tex->anisotropy = 1;

	//D3D_SAFE_RELEASE (tex->teximg); //FIXME
	if(tex->teximg)
	{
		IDirect3DTexture8_Release(tex->teximg);
		tex->teximg = NULL;
	}   
}


void D3D_GetRenderStates (void)
{
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_ZENABLE, &d3d_RenderStates[D3DRS_ZENABLE]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_FILLMODE, &d3d_RenderStates[D3DRS_FILLMODE]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_SHADEMODE, &d3d_RenderStates[D3DRS_SHADEMODE]);
//	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_LINEPATTERN, &d3d_RenderStates[D3DRS_LINEPATTERN]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_ZWRITEENABLE, &d3d_RenderStates[D3DRS_ZWRITEENABLE]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_ALPHATESTENABLE, &d3d_RenderStates[D3DRS_ALPHATESTENABLE]);
//	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_LASTPIXEL, &d3d_RenderStates[D3DRS_LASTPIXEL]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_SRCBLEND, &d3d_RenderStates[D3DRS_SRCBLEND]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_DESTBLEND, &d3d_RenderStates[D3DRS_DESTBLEND]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_CULLMODE, &d3d_RenderStates[D3DRS_CULLMODE]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_ZFUNC, &d3d_RenderStates[D3DRS_ZFUNC]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_ALPHAREF, &d3d_RenderStates[D3DRS_ALPHAREF]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_ALPHAFUNC, &d3d_RenderStates[D3DRS_ALPHAFUNC]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_DITHERENABLE, &d3d_RenderStates[D3DRS_DITHERENABLE]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_ALPHABLENDENABLE, &d3d_RenderStates[D3DRS_ALPHABLENDENABLE]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_FOGENABLE, &d3d_RenderStates[D3DRS_FOGENABLE]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_SPECULARENABLE, &d3d_RenderStates[D3DRS_SPECULARENABLE]);
//	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_ZVISIBLE, &d3d_RenderStates[D3DRS_ZVISIBLE]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_FOGCOLOR, &d3d_RenderStates[D3DRS_FOGCOLOR]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_FOGTABLEMODE, &d3d_RenderStates[D3DRS_FOGTABLEMODE]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_FOGSTART, &d3d_RenderStates[D3DRS_FOGSTART]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_FOGEND, &d3d_RenderStates[D3DRS_FOGEND]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_FOGDENSITY, &d3d_RenderStates[D3DRS_FOGDENSITY]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_EDGEANTIALIAS, &d3d_RenderStates[D3DRS_EDGEANTIALIAS]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_ZBIAS, &d3d_RenderStates[D3DRS_ZBIAS]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_RANGEFOGENABLE, &d3d_RenderStates[D3DRS_RANGEFOGENABLE]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_STENCILENABLE, &d3d_RenderStates[D3DRS_STENCILENABLE]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_STENCILFAIL, &d3d_RenderStates[D3DRS_STENCILFAIL]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_STENCILZFAIL, &d3d_RenderStates[D3DRS_STENCILZFAIL]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_STENCILPASS, &d3d_RenderStates[D3DRS_STENCILPASS]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_STENCILFUNC, &d3d_RenderStates[D3DRS_STENCILFUNC]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_STENCILREF, &d3d_RenderStates[D3DRS_STENCILREF]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_STENCILMASK, &d3d_RenderStates[D3DRS_STENCILMASK]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_STENCILWRITEMASK, &d3d_RenderStates[D3DRS_STENCILWRITEMASK]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_TEXTUREFACTOR, &d3d_RenderStates[D3DRS_TEXTUREFACTOR]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_WRAP0, &d3d_RenderStates[D3DRS_WRAP0]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_WRAP1, &d3d_RenderStates[D3DRS_WRAP1]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_WRAP2, &d3d_RenderStates[D3DRS_WRAP2]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_WRAP3, &d3d_RenderStates[D3DRS_WRAP3]);
//	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_WRAP4, &d3d_RenderStates[D3DRS_WRAP4]);
//	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_WRAP5, &d3d_RenderStates[D3DRS_WRAP5]);
//	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_WRAP6, &d3d_RenderStates[D3DRS_WRAP6]);
//	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_WRAP7, &d3d_RenderStates[D3DRS_WRAP7]);
//	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_CLIPPING, &d3d_RenderStates[D3DRS_CLIPPING]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_LIGHTING, &d3d_RenderStates[D3DRS_LIGHTING]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_AMBIENT, &d3d_RenderStates[D3DRS_AMBIENT]);
//	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_FOGVERTEXMODE, &d3d_RenderStates[D3DRS_FOGVERTEXMODE]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_COLORVERTEX, &d3d_RenderStates[D3DRS_COLORVERTEX]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_LOCALVIEWER, &d3d_RenderStates[D3DRS_LOCALVIEWER]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_NORMALIZENORMALS, &d3d_RenderStates[D3DRS_NORMALIZENORMALS]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_DIFFUSEMATERIALSOURCE, &d3d_RenderStates[D3DRS_DIFFUSEMATERIALSOURCE]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_SPECULARMATERIALSOURCE, &d3d_RenderStates[D3DRS_SPECULARMATERIALSOURCE]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_AMBIENTMATERIALSOURCE, &d3d_RenderStates[D3DRS_AMBIENTMATERIALSOURCE]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_EMISSIVEMATERIALSOURCE, &d3d_RenderStates[D3DRS_EMISSIVEMATERIALSOURCE]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_VERTEXBLEND, &d3d_RenderStates[D3DRS_VERTEXBLEND]);
//	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_CLIPPLANEENABLE, &d3d_RenderStates[D3DRS_CLIPPLANEENABLE]);
//	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_SOFTWAREVERTEXPROCESSING, &d3d_RenderStates[D3DRS_SOFTWAREVERTEXPROCESSING]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_POINTSIZE, &d3d_RenderStates[D3DRS_POINTSIZE]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_POINTSIZE_MIN, &d3d_RenderStates[D3DRS_POINTSIZE_MIN]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_POINTSPRITEENABLE, &d3d_RenderStates[D3DRS_POINTSPRITEENABLE]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_POINTSCALEENABLE, &d3d_RenderStates[D3DRS_POINTSCALEENABLE]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_POINTSCALE_A, &d3d_RenderStates[D3DRS_POINTSCALE_A]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_POINTSCALE_B, &d3d_RenderStates[D3DRS_POINTSCALE_B]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_POINTSCALE_C, &d3d_RenderStates[D3DRS_POINTSCALE_C]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_MULTISAMPLEANTIALIAS, &d3d_RenderStates[D3DRS_MULTISAMPLEANTIALIAS]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_MULTISAMPLEMASK, &d3d_RenderStates[D3DRS_MULTISAMPLEMASK]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_PATCHEDGESTYLE, &d3d_RenderStates[D3DRS_PATCHEDGESTYLE]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_PATCHSEGMENTS, &d3d_RenderStates[D3DRS_PATCHSEGMENTS]);
//	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_DEBUGMONITORTOKEN, &d3d_RenderStates[D3DRS_DEBUGMONITORTOKEN]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_POINTSIZE_MAX, &d3d_RenderStates[D3DRS_POINTSIZE_MAX]);
//	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_INDEXEDVERTEXBLENDENABLE, &d3d_RenderStates[D3DRS_INDEXEDVERTEXBLENDENABLE]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_COLORWRITEENABLE, &d3d_RenderStates[D3DRS_COLORWRITEENABLE]);
//	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_TWEENFACTOR, &d3d_RenderStates[D3DRS_TWEENFACTOR]);
	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_BLENDOP, &d3d_RenderStates[D3DRS_BLENDOP]);
//	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_POSITIONORDER, &d3d_RenderStates[D3DRS_POSITIONORDER]);
//	IDirect3DDevice8_GetRenderState (d3d_Device, D3DRS_NORMALORDER, &d3d_RenderStates[D3DRS_NORMALORDER]);
}


void D3D_SetRenderStates (void)
{
	// this forces a set of all render states will the current cached values rather than filtering, to
	// ensure that they are properly updated
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_ZENABLE, d3d_RenderStates[D3DRS_ZENABLE]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_FILLMODE, d3d_RenderStates[D3DRS_FILLMODE]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_SHADEMODE, d3d_RenderStates[D3DRS_SHADEMODE]);
//	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_LINEPATTERN, d3d_RenderStates[D3DRS_LINEPATTERN]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_ZWRITEENABLE, d3d_RenderStates[D3DRS_ZWRITEENABLE]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_ALPHATESTENABLE, d3d_RenderStates[D3DRS_ALPHATESTENABLE]);
//	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_LASTPIXEL, d3d_RenderStates[D3DRS_LASTPIXEL]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_SRCBLEND, d3d_RenderStates[D3DRS_SRCBLEND]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_DESTBLEND, d3d_RenderStates[D3DRS_DESTBLEND]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_CULLMODE, d3d_RenderStates[D3DRS_CULLMODE]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_ZFUNC, d3d_RenderStates[D3DRS_ZFUNC]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_ALPHAREF, d3d_RenderStates[D3DRS_ALPHAREF]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_ALPHAFUNC, d3d_RenderStates[D3DRS_ALPHAFUNC]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_DITHERENABLE, d3d_RenderStates[D3DRS_DITHERENABLE]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_ALPHABLENDENABLE, d3d_RenderStates[D3DRS_ALPHABLENDENABLE]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_FOGENABLE, d3d_RenderStates[D3DRS_FOGENABLE]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_SPECULARENABLE, d3d_RenderStates[D3DRS_SPECULARENABLE]);
//	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_ZVISIBLE, d3d_RenderStates[D3DRS_ZVISIBLE]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_FOGCOLOR, d3d_RenderStates[D3DRS_FOGCOLOR]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_FOGTABLEMODE, d3d_RenderStates[D3DRS_FOGTABLEMODE]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_FOGSTART, d3d_RenderStates[D3DRS_FOGSTART]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_FOGEND, d3d_RenderStates[D3DRS_FOGEND]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_FOGDENSITY, d3d_RenderStates[D3DRS_FOGDENSITY]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_EDGEANTIALIAS, d3d_RenderStates[D3DRS_EDGEANTIALIAS]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_ZBIAS, d3d_RenderStates[D3DRS_ZBIAS]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_RANGEFOGENABLE, d3d_RenderStates[D3DRS_RANGEFOGENABLE]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_STENCILENABLE, d3d_RenderStates[D3DRS_STENCILENABLE]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_STENCILFAIL, d3d_RenderStates[D3DRS_STENCILFAIL]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_STENCILZFAIL, d3d_RenderStates[D3DRS_STENCILZFAIL]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_STENCILPASS, d3d_RenderStates[D3DRS_STENCILPASS]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_STENCILFUNC, d3d_RenderStates[D3DRS_STENCILFUNC]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_STENCILREF, d3d_RenderStates[D3DRS_STENCILREF]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_STENCILMASK, d3d_RenderStates[D3DRS_STENCILMASK]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_STENCILWRITEMASK, d3d_RenderStates[D3DRS_STENCILWRITEMASK]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_TEXTUREFACTOR, d3d_RenderStates[D3DRS_TEXTUREFACTOR]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_WRAP0, d3d_RenderStates[D3DRS_WRAP0]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_WRAP1, d3d_RenderStates[D3DRS_WRAP1]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_WRAP2, d3d_RenderStates[D3DRS_WRAP2]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_WRAP3, d3d_RenderStates[D3DRS_WRAP3]);
//	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_WRAP4, d3d_RenderStates[D3DRS_WRAP4]);
//	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_WRAP5, d3d_RenderStates[D3DRS_WRAP5]);
//	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_WRAP6, d3d_RenderStates[D3DRS_WRAP6]);
//	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_WRAP7, d3d_RenderStates[D3DRS_WRAP7]);
//	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_CLIPPING, d3d_RenderStates[D3DRS_CLIPPING]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_LIGHTING, d3d_RenderStates[D3DRS_LIGHTING]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_AMBIENT, d3d_RenderStates[D3DRS_AMBIENT]);
//	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_FOGVERTEXMODE, d3d_RenderStates[D3DRS_FOGVERTEXMODE]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_COLORVERTEX, d3d_RenderStates[D3DRS_COLORVERTEX]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_LOCALVIEWER, d3d_RenderStates[D3DRS_LOCALVIEWER]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_NORMALIZENORMALS, d3d_RenderStates[D3DRS_NORMALIZENORMALS]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_DIFFUSEMATERIALSOURCE, d3d_RenderStates[D3DRS_DIFFUSEMATERIALSOURCE]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_SPECULARMATERIALSOURCE, d3d_RenderStates[D3DRS_SPECULARMATERIALSOURCE]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_AMBIENTMATERIALSOURCE, d3d_RenderStates[D3DRS_AMBIENTMATERIALSOURCE]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_EMISSIVEMATERIALSOURCE, d3d_RenderStates[D3DRS_EMISSIVEMATERIALSOURCE]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_VERTEXBLEND, d3d_RenderStates[D3DRS_VERTEXBLEND]);
//	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_CLIPPLANEENABLE, d3d_RenderStates[D3DRS_CLIPPLANEENABLE]);
//	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_SOFTWAREVERTEXPROCESSING, d3d_RenderStates[D3DRS_SOFTWAREVERTEXPROCESSING]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_POINTSIZE, d3d_RenderStates[D3DRS_POINTSIZE]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_POINTSIZE_MIN, d3d_RenderStates[D3DRS_POINTSIZE_MIN]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_POINTSPRITEENABLE, d3d_RenderStates[D3DRS_POINTSPRITEENABLE]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_POINTSCALEENABLE, d3d_RenderStates[D3DRS_POINTSCALEENABLE]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_POINTSCALE_A, d3d_RenderStates[D3DRS_POINTSCALE_A]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_POINTSCALE_B, d3d_RenderStates[D3DRS_POINTSCALE_B]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_POINTSCALE_C, d3d_RenderStates[D3DRS_POINTSCALE_C]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_MULTISAMPLEANTIALIAS, d3d_RenderStates[D3DRS_MULTISAMPLEANTIALIAS]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_MULTISAMPLEMASK, d3d_RenderStates[D3DRS_MULTISAMPLEMASK]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_PATCHEDGESTYLE, d3d_RenderStates[D3DRS_PATCHEDGESTYLE]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_PATCHSEGMENTS, d3d_RenderStates[D3DRS_PATCHSEGMENTS]);
//	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_DEBUGMONITORTOKEN, d3d_RenderStates[D3DRS_DEBUGMONITORTOKEN]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_POINTSIZE_MAX, d3d_RenderStates[D3DRS_POINTSIZE_MAX]);
//	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_INDEXEDVERTEXBLENDENABLE, d3d_RenderStates[D3DRS_INDEXEDVERTEXBLENDENABLE]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_COLORWRITEENABLE, d3d_RenderStates[D3DRS_COLORWRITEENABLE]);
//	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_TWEENFACTOR, d3d_RenderStates[D3DRS_TWEENFACTOR]);
	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_BLENDOP, d3d_RenderStates[D3DRS_BLENDOP]);
//	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_POSITIONORDER, d3d_RenderStates[D3DRS_POSITIONORDER]);
//	IDirect3DDevice8_SetRenderState (d3d_Device, D3DRS_NORMALORDER, d3d_RenderStates[D3DRS_NORMALORDER]);
}


void D3D_GetTextureStates (void)
{
	int i;

	for (i = 0; i < D3D_MAX_TMUS; i++)
	{
		IDirect3DDevice8_GetTextureStageState (d3d_Device, i, D3DTSS_COLOROP, &d3d_TextureStates[i][D3DTSS_COLOROP]);
		IDirect3DDevice8_GetTextureStageState (d3d_Device, i, D3DTSS_COLORARG1, &d3d_TextureStates[i][D3DTSS_COLORARG1]);
		IDirect3DDevice8_GetTextureStageState (d3d_Device, i, D3DTSS_COLORARG2, &d3d_TextureStates[i][D3DTSS_COLORARG2]);
		IDirect3DDevice8_GetTextureStageState (d3d_Device, i, D3DTSS_ALPHAOP, &d3d_TextureStates[i][D3DTSS_ALPHAOP]);
		IDirect3DDevice8_GetTextureStageState (d3d_Device, i, D3DTSS_ALPHAARG1, &d3d_TextureStates[i][D3DTSS_ALPHAARG1]);
		IDirect3DDevice8_GetTextureStageState (d3d_Device, i, D3DTSS_ALPHAARG2, &d3d_TextureStates[i][D3DTSS_ALPHAARG2]);
		IDirect3DDevice8_GetTextureStageState (d3d_Device, i, D3DTSS_BUMPENVMAT00, &d3d_TextureStates[i][D3DTSS_BUMPENVMAT00]);
		IDirect3DDevice8_GetTextureStageState (d3d_Device, i, D3DTSS_BUMPENVMAT01, &d3d_TextureStates[i][D3DTSS_BUMPENVMAT01]);
		IDirect3DDevice8_GetTextureStageState (d3d_Device, i, D3DTSS_BUMPENVMAT10, &d3d_TextureStates[i][D3DTSS_BUMPENVMAT10]);
		IDirect3DDevice8_GetTextureStageState (d3d_Device, i, D3DTSS_BUMPENVMAT11, &d3d_TextureStates[i][D3DTSS_BUMPENVMAT11]);
		IDirect3DDevice8_GetTextureStageState (d3d_Device, i, D3DTSS_TEXCOORDINDEX, &d3d_TextureStates[i][D3DTSS_TEXCOORDINDEX]);
		IDirect3DDevice8_GetTextureStageState (d3d_Device, i, D3DTSS_ADDRESSU, &d3d_TextureStates[i][D3DTSS_ADDRESSU]);
		IDirect3DDevice8_GetTextureStageState (d3d_Device, i, D3DTSS_ADDRESSV, &d3d_TextureStates[i][D3DTSS_ADDRESSV]);
		IDirect3DDevice8_GetTextureStageState (d3d_Device, i, D3DTSS_BORDERCOLOR, &d3d_TextureStates[i][D3DTSS_BORDERCOLOR]);
		IDirect3DDevice8_GetTextureStageState (d3d_Device, i, D3DTSS_MAGFILTER, &d3d_TextureStates[i][D3DTSS_MAGFILTER]);
		IDirect3DDevice8_GetTextureStageState (d3d_Device, i, D3DTSS_MINFILTER, &d3d_TextureStates[i][D3DTSS_MINFILTER]);
		IDirect3DDevice8_GetTextureStageState (d3d_Device, i, D3DTSS_MIPFILTER, &d3d_TextureStates[i][D3DTSS_MIPFILTER]);
		IDirect3DDevice8_GetTextureStageState (d3d_Device, i, D3DTSS_MIPMAPLODBIAS, &d3d_TextureStates[i][D3DTSS_MIPMAPLODBIAS]);
		IDirect3DDevice8_GetTextureStageState (d3d_Device, i, D3DTSS_MAXMIPLEVEL, &d3d_TextureStates[i][D3DTSS_MAXMIPLEVEL]);
		IDirect3DDevice8_GetTextureStageState (d3d_Device, i, D3DTSS_MAXANISOTROPY, &d3d_TextureStates[i][D3DTSS_MAXANISOTROPY]);
		IDirect3DDevice8_GetTextureStageState (d3d_Device, i, D3DTSS_BUMPENVLSCALE, &d3d_TextureStates[i][D3DTSS_BUMPENVLSCALE]);
		IDirect3DDevice8_GetTextureStageState (d3d_Device, i, D3DTSS_BUMPENVLOFFSET, &d3d_TextureStates[i][D3DTSS_BUMPENVLOFFSET]);
		IDirect3DDevice8_GetTextureStageState (d3d_Device, i, D3DTSS_TEXTURETRANSFORMFLAGS, &d3d_TextureStates[i][D3DTSS_TEXTURETRANSFORMFLAGS]);
		IDirect3DDevice8_GetTextureStageState (d3d_Device, i, D3DTSS_ADDRESSW, &d3d_TextureStates[i][D3DTSS_ADDRESSW]);
		IDirect3DDevice8_GetTextureStageState (d3d_Device, i, D3DTSS_COLORARG0, &d3d_TextureStates[i][D3DTSS_COLORARG0]);
		IDirect3DDevice8_GetTextureStageState (d3d_Device, i, D3DTSS_ALPHAARG0, &d3d_TextureStates[i][D3DTSS_ALPHAARG0]);
		IDirect3DDevice8_GetTextureStageState (d3d_Device, i, D3DTSS_RESULTARG, &d3d_TextureStates[i][D3DTSS_RESULTARG]);
	}
}


void D3D_SetTextureStates (void)
{
	int i;

	for (i = 0; i < D3D_MAX_TMUS; i++)
	{
		IDirect3DDevice8_SetTextureStageState (d3d_Device, i, D3DTSS_COLOROP, d3d_TextureStates[i][D3DTSS_COLOROP]);
		IDirect3DDevice8_SetTextureStageState (d3d_Device, i, D3DTSS_COLORARG1, d3d_TextureStates[i][D3DTSS_COLORARG1]);
		IDirect3DDevice8_SetTextureStageState (d3d_Device, i, D3DTSS_COLORARG2, d3d_TextureStates[i][D3DTSS_COLORARG2]);
		IDirect3DDevice8_SetTextureStageState (d3d_Device, i, D3DTSS_ALPHAOP, d3d_TextureStates[i][D3DTSS_ALPHAOP]);
		IDirect3DDevice8_SetTextureStageState (d3d_Device, i, D3DTSS_ALPHAARG1, d3d_TextureStates[i][D3DTSS_ALPHAARG1]);
		IDirect3DDevice8_SetTextureStageState (d3d_Device, i, D3DTSS_ALPHAARG2, d3d_TextureStates[i][D3DTSS_ALPHAARG2]);
		IDirect3DDevice8_SetTextureStageState (d3d_Device, i, D3DTSS_BUMPENVMAT00, d3d_TextureStates[i][D3DTSS_BUMPENVMAT00]);
		IDirect3DDevice8_SetTextureStageState (d3d_Device, i, D3DTSS_BUMPENVMAT01, d3d_TextureStates[i][D3DTSS_BUMPENVMAT01]);
		IDirect3DDevice8_SetTextureStageState (d3d_Device, i, D3DTSS_BUMPENVMAT10, d3d_TextureStates[i][D3DTSS_BUMPENVMAT10]);
		IDirect3DDevice8_SetTextureStageState (d3d_Device, i, D3DTSS_BUMPENVMAT11, d3d_TextureStates[i][D3DTSS_BUMPENVMAT11]);
		IDirect3DDevice8_SetTextureStageState (d3d_Device, i, D3DTSS_TEXCOORDINDEX, d3d_TextureStates[i][D3DTSS_TEXCOORDINDEX]);
		IDirect3DDevice8_SetTextureStageState (d3d_Device, i, D3DTSS_ADDRESSU, d3d_TextureStates[i][D3DTSS_ADDRESSU]);
		IDirect3DDevice8_SetTextureStageState (d3d_Device, i, D3DTSS_ADDRESSV, d3d_TextureStates[i][D3DTSS_ADDRESSV]);
		IDirect3DDevice8_SetTextureStageState (d3d_Device, i, D3DTSS_BORDERCOLOR, d3d_TextureStates[i][D3DTSS_BORDERCOLOR]);
		IDirect3DDevice8_SetTextureStageState (d3d_Device, i, D3DTSS_MAGFILTER, d3d_TextureStates[i][D3DTSS_MAGFILTER]);
		IDirect3DDevice8_SetTextureStageState (d3d_Device, i, D3DTSS_MINFILTER, d3d_TextureStates[i][D3DTSS_MINFILTER]);
		IDirect3DDevice8_SetTextureStageState (d3d_Device, i, D3DTSS_MIPFILTER, d3d_TextureStates[i][D3DTSS_MIPFILTER]);
		IDirect3DDevice8_SetTextureStageState (d3d_Device, i, D3DTSS_MIPMAPLODBIAS, d3d_TextureStates[i][D3DTSS_MIPMAPLODBIAS]);
		IDirect3DDevice8_SetTextureStageState (d3d_Device, i, D3DTSS_MAXMIPLEVEL, d3d_TextureStates[i][D3DTSS_MAXMIPLEVEL]);
		IDirect3DDevice8_SetTextureStageState (d3d_Device, i, D3DTSS_MAXANISOTROPY, d3d_TextureStates[i][D3DTSS_MAXANISOTROPY]);
		IDirect3DDevice8_SetTextureStageState (d3d_Device, i, D3DTSS_BUMPENVLSCALE, d3d_TextureStates[i][D3DTSS_BUMPENVLSCALE]);
		IDirect3DDevice8_SetTextureStageState (d3d_Device, i, D3DTSS_BUMPENVLOFFSET, d3d_TextureStates[i][D3DTSS_BUMPENVLOFFSET]);
		IDirect3DDevice8_SetTextureStageState (d3d_Device, i, D3DTSS_TEXTURETRANSFORMFLAGS, d3d_TextureStates[i][D3DTSS_TEXTURETRANSFORMFLAGS]);
		IDirect3DDevice8_SetTextureStageState (d3d_Device, i, D3DTSS_ADDRESSW, d3d_TextureStates[i][D3DTSS_ADDRESSW]);
		IDirect3DDevice8_SetTextureStageState (d3d_Device, i, D3DTSS_COLORARG0, d3d_TextureStates[i][D3DTSS_COLORARG0]);
		IDirect3DDevice8_SetTextureStageState (d3d_Device, i, D3DTSS_ALPHAARG0, d3d_TextureStates[i][D3DTSS_ALPHAARG0]);
		IDirect3DDevice8_SetTextureStageState (d3d_Device, i, D3DTSS_RESULTARG, d3d_TextureStates[i][D3DTSS_RESULTARG]);
	}
}


void D3D_InitStates (void)
{
	int i;

	// init tmus
	for (i = 0; i < D3D_MAX_TMUS; i++)
	{
		d3d_TMUs[i].boundtexture = NULL;
		d3d_TMUs[i].enabled = FALSE;
		d3d_TMUs[i].texcoordindex = i;
	}

	// store out all states
	D3D_GetRenderStates ();
	D3D_GetTextureStates ();

	// force all states to dirty on entry
	D3D_DirtyAllStates ();
}


D3DTEXTUREOP D3D_DecodeOp (D3DTEXTUREOP opin, DWORD scale)
{
	if (scale == 1)
		return opin;
	else if (scale == 2)
	{
		if (opin == D3DTOP_MODULATE)
			return D3DTOP_MODULATE2X;
		else if (opin == D3DTOP_ADDSIGNED)
			return D3DTOP_ADDSIGNED2X;
		else return opin;
	}
	else
	{
		if (opin == D3DTOP_MODULATE)
			return D3DTOP_MODULATE4X;
		else return opin;
	}
}


void D3D_CheckDirtyTextureStates (int tmu)
{
	if (d3d_TMUs[tmu].boundtexture)
	{
		if (d3d_TMUs[d3d_CurrentTMU].texparamdirty)
		{
			// setup texture states - these ones are specific to the texture and come from glTexParameter
			D3D_SetTextureState (tmu, D3DTSS_ADDRESSU, d3d_TMUs[tmu].boundtexture->addressu);
			D3D_SetTextureState (tmu, D3DTSS_ADDRESSV, d3d_TMUs[tmu].boundtexture->addressv);
			D3D_SetTextureState (tmu, D3DTSS_MAXANISOTROPY, d3d_TMUs[tmu].boundtexture->anisotropy);

			// minfilter and magfilter need to switch to anisotropic
			if (d3d_TMUs[tmu].boundtexture->anisotropy > 1)
			{
				D3D_SetTextureState (tmu, D3DTSS_MAGFILTER, D3DTEXF_ANISOTROPIC);
				D3D_SetTextureState (tmu, D3DTSS_MINFILTER, D3DTEXF_ANISOTROPIC);
				D3D_SetTextureState (tmu, D3DTSS_MIPFILTER, d3d_TMUs[tmu].boundtexture->mipfilter);
			}
			else
			{
				D3D_SetTextureState (tmu, D3DTSS_MAGFILTER, d3d_TMUs[tmu].boundtexture->magfilter);
				D3D_SetTextureState (tmu, D3DTSS_MINFILTER, d3d_TMUs[tmu].boundtexture->minfilter);
				D3D_SetTextureState (tmu, D3DTSS_MIPFILTER, d3d_TMUs[tmu].boundtexture->mipfilter);
			}

			d3d_TMUs[tmu].texparamdirty = FALSE;
		}
	}

	if (d3d_TMUs[tmu].texenvdirty)
	{
		/*
		if (d3d_TMUs[tmu].combine)
		{
			d3d_TMUs[tmu].colorop = D3D_DecodeOp (d3d_TMUs[tmu].combstate.colorop, d3d_TMUs[tmu].combstate.colorscale);
			d3d_TMUs[tmu].colorarg1 = d3d_TMUs[tmu].combstate.colorarg1;
			d3d_TMUs[tmu].colorarg2 = d3d_TMUs[tmu].combstate.colorarg2;

			d3d_TMUs[tmu].alphaop = D3D_DecodeOp (d3d_TMUs[tmu].combstate.alphaop, d3d_TMUs[tmu].combstate.alphascale);
			d3d_TMUs[tmu].alphaarg1 = d3d_TMUs[tmu].combstate.alphaarg1;
			d3d_TMUs[tmu].alphaarg2 = d3d_TMUs[tmu].combstate.alphaarg2;

			D3D_SetTextureState (tmu, D3DTSS_COLORARG0, d3d_TMUs[tmu].combstate.colorarg0);
			D3D_SetTextureState (tmu, D3DTSS_ALPHAARG0, d3d_TMUs[tmu].combstate.alphaarg0);
			D3D_SetTextureState (tmu, D3DTSS_RESULTARG, d3d_TMUs[tmu].combstate.resultarg);
		}
		*/

		// these ones are specific to the TMU and come from glTexEnv
		D3D_SetTextureState (tmu, D3DTSS_COLOROP, d3d_TMUs[tmu].colorop);
		D3D_SetTextureState (tmu, D3DTSS_COLORARG1, d3d_TMUs[tmu].colorarg1);
		D3D_SetTextureState (tmu, D3DTSS_COLORARG2, d3d_TMUs[tmu].colorarg2);
		D3D_SetTextureState (tmu, D3DTSS_ALPHAOP, d3d_TMUs[tmu].alphaop);
		D3D_SetTextureState (tmu, D3DTSS_ALPHAARG1, d3d_TMUs[tmu].alphaarg1);
		D3D_SetTextureState (tmu, D3DTSS_ALPHAARG2, d3d_TMUs[tmu].alphaarg2);
		D3D_SetTextureState (tmu, D3DTSS_TEXCOORDINDEX, d3d_TMUs[tmu].texcoordindex);

		d3d_TMUs[tmu].texenvdirty = FALSE;
	}
}


/*
===================================================================================================================

			POLYGON OFFSET

	Who said D3D didn't have polygon offset?

	It's implementation is quite a bit simpler than OpenGL's however; just a pretty basic static bias
	(it's better in D3D9)

===================================================================================================================
*/

BOOL d3d_PolyOffsetEnabled = FALSE;
BOOL d3d_PolyOffsetSwitched = FALSE;
float d3d_PolyOffsetFactor = 8;

void glPolygonOffset (GLfloat factor, GLfloat units)
{
	// need to switch polygon offset
	d3d_PolyOffsetSwitched = FALSE;

	// just use the units here as we're going to fudge it using D3DRS_ZBIAS
	// 0 is furthest; 16 is nearest; d3d default is 0 so our default is an intermediate value (8)
	// so that we can do both push back and pull forward
	// negative values come nearer; positive values go further, so invert the sense
//	d3d_PolyOffsetFactor = 8 - units;
	d3d_PolyOffsetFactor = 4 + units; //HACK This is a nasty hack for Xash3D only - MARTY
									  //     Polygons look very glitchy if we dont do this!		
	// clamp to d3d scale
	if (d3d_PolyOffsetFactor < 0) d3d_PolyOffsetFactor = 0;
	if (d3d_PolyOffsetFactor > 16) d3d_PolyOffsetFactor = 16;
}


/*
===================================================================================================================

			VERTEX SUBMISSION

	Per the spec for glVertex (http://www.opengl.org/sdk/docs/man/xhtml/glVertex.xml) glColor, glNormal and
	glTexCoord just specify a *current* color, normal and texcoord, whereas glVertex actually creates a vertex
	that inherits the current color, normal and texcoord.

	Real D3D code looks *nothing* like this.

===================================================================================================================
*/

int d3d_PrimitiveMode = 0;
int d3d_NumVerts = 0;

// this should be a multiple of 12 to support both GL_QUADS and GL_TRIANGLES
// it should also be large enough to hold the biggest tristrip or fan in use in the engine
// individual quads or tris can be submitted in batches
#define D3D_MAX_VERTEXES	12//600

typedef struct gl_texcoord_s
{
	float s;
	float t;
} gl_texcoord_t;

typedef struct gl_xyz_s
{
	float x;
	float y;
	float z;
} gl_xyz_t;

// defaults that are picked up by each glVertex call
D3DCOLOR d3d_CurrentColor = 0xffffffff;
gl_texcoord_t d3d_CurrentTexCoord[D3D_MAX_TMUS] = {{0, 0}/*, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}*/};
//gl_xyz_t d3d_CurrentNormal = {0, 1, 0};

// this may be a little wasteful as it's a full sized vertex for 8 TMUs
// we'll fix it if it becomes a problem (it's not like Quake stresses the GPU too much anyway)
typedef struct gl_vertex_s
{
	gl_xyz_t position;
	//gl_xyz_t normal;
	D3DCOLOR c;
	gl_texcoord_t st[D3D_MAX_TMUS];
} gl_vertex_t;

gl_vertex_t d3d_Vertexes[D3D_MAX_VERTEXES];

BOOL d3d_SceneBegun = FALSE;

void GL_SubmitVertexes (void)
{
	int i;

	DWORD d3d_VertexShader;
	DWORD d3d_TexCoordSizes;
	DWORD d3d_TMUShader[] =
	{
		D3DFVF_TEX0,
		D3DFVF_TEX1,
//		D3DFVF_TEX2,
//		D3DFVF_TEX3,
//		D3DFVF_TEX4
//		D3DFVF_TEX5,
//		D3DFVF_TEX6,
//		D3DFVF_TEX7,
//		D3DFVF_TEX8
	};

	if (!d3d_Device) return;

	// check for a beginscene
	if (!d3d_SceneBegun)
	{
		// D3D has a separate view matrix which is concatenated with the world in OpenGL.  We maintain
		// a compatible interface by just setting the D3D view matrix to identity and doing all modelview
		// transforms via the world matrix.
		D3DXMATRIX d3d_ViewMatrix;

#ifndef _XBOX // Not used on Xbox
		// issue a beginscene (geometry needs this
		IDirect3DDevice8_BeginScene (d3d_Device);
#endif
		// force an invalid vertex shader so that the first time will change it
		D3D_SetVertexShader (D3DFVF_XYZ | D3DFVF_XYZRHW);

		// clear down bound textures
		for (i = 0; i < D3D_MAX_TMUS; i++) d3d_BoundTextures[i] = NULL;

		// we're in a scene now
		d3d_SceneBegun = TRUE;

		// now set our identity view matrix
		D3DXMatrixIdentity (&d3d_ViewMatrix);
		IDirect3DDevice8_SetTransform (d3d_Device, D3DTS_VIEW, &d3d_ViewMatrix);
	}

	// Checked for scissor test
	if(g_bScissorTest)
		IDirect3DDevice8_SetScissors(d3d_Device, 1, FALSE, &g_ScissorRect);	

#ifndef _XBOX // Not used in PCSX-R
	// check polygon offset
	if (!d3d_PolyOffsetSwitched)
	{
		if (d3d_PolyOffsetEnabled)
		{
			// setup polygon offset
			D3D_SetRenderState (D3DRS_ZBIAS, d3d_PolyOffsetFactor);
		}
		else
		{
			// no polygon offset - back to normal z bias
//			D3D_SetRenderState (D3DRS_ZBIAS, 8);
			D3D_SetRenderState (D3DRS_ZBIAS, 0);
		}										 

		// we've switched polygon offset now
		d3d_PolyOffsetSwitched = TRUE;
	}
#endif

	// check for dirty matrixes
	D3D_CheckDirtyMatrix (&d3d_ModelViewMatrix);
	D3D_CheckDirtyMatrix (&d3d_ProjectionMatrix);

	// initial vertex shader (will be added to as TMUs accumulate)
	d3d_VertexShader = D3DFVF_XYZ /*| D3DFVF_NORMAL */| D3DFVF_DIFFUSE;
	d3d_TexCoordSizes = 0;

	// set up textures
	for (i = 0; i < D3D_MAX_TMUS; i++)
	{
		// end of TMUs
		if (!d3d_TMUs[i].enabled || !d3d_TMUs[i].boundtexture)
		{
			// explicitly disable alpha and color ops in this and subsequent stages
			D3D_SetTextureState (i, D3DTSS_COLOROP, D3DTOP_DISABLE);
			D3D_SetTextureState (i, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
			D3D_SetTexture (i, NULL);
			// done
			break;
		}

		// set up texcoord sizes
		d3d_TexCoordSizes |= D3DFVF_TEXCOORDSIZE2 (i);

		// check for dirty states (this is needed as OpenGL sets state per-texture as well as per-stage)
		D3D_CheckDirtyTextureStates (i);

		// bind the texture (now with added filtering!)
		D3D_SetTexture (i, d3d_TMUs[i].boundtexture->teximg);
	}

	// set the correct vertex shader for the number of enabled TMUs
	D3D_SetVertexShader (d3d_VertexShader | d3d_TMUShader[i] | d3d_TexCoordSizes);

	// draw the verts - these are the only modes we support for Quake
	// per the spec, GL_FRONT_AND_BACK still allows lines and points through
	switch (d3d_PrimitiveMode)
	{
	case GL_TRIANGLES:
		//OutputDebugString("GL_TRIANGLES\n");
		// D3DPT_TRIANGLELIST models GL_TRIANGLES when used for either a single triangle or multiple triangles
//		if (gl_CullMode != GL_FRONT_AND_BACK && d3d_NumVerts % 3 == 0 && d3d_NumVerts != 0) IDirect3DDevice8_DrawPrimitiveUP (d3d_Device, D3DPT_TRIANGLELIST, d3d_NumVerts / 3, d3d_Vertexes, sizeof (gl_vertex_t));
		if (gl_CullMode != GL_FRONT_AND_BACK && d3d_NumVerts != 0) IDirect3DDevice8_DrawVerticesUP (d3d_Device, D3DPT_TRIANGLELIST, d3d_NumVerts, d3d_Vertexes, sizeof (gl_vertex_t));
		break;

	case GL_TRIANGLE_STRIP:
		//OutputDebugString("GL_TRIANGLE_STRIP\n");
		// regular tristrip
//		if (gl_CullMode != GL_FRONT_AND_BACK) IDirect3DDevice8_DrawPrimitiveUP (d3d_Device, D3DPT_TRIANGLESTRIP, d3d_NumVerts - 2, d3d_Vertexes, sizeof (gl_vertex_t));
		if (gl_CullMode != GL_FRONT_AND_BACK) IDirect3DDevice8_DrawVerticesUP (d3d_Device, D3DPT_TRIANGLESTRIP, d3d_NumVerts, d3d_Vertexes, sizeof (gl_vertex_t));
		break;

	case GL_POLYGON:
		//OutputDebugString("GL_POLYGON\n");
		// a GL_POLYGON has the same vertex layout and order as a trifan, and can be used interchangably in OpenGL
	case GL_TRIANGLE_FAN:
		//OutputDebugString("GL_TRIANGLE_FAN\n");
		// regular trifan
//		if (gl_CullMode != GL_FRONT_AND_BACK) IDirect3DDevice8_DrawPrimitiveUP (d3d_Device, D3DPT_TRIANGLEFAN, d3d_NumVerts - 2, d3d_Vertexes, sizeof (gl_vertex_t));
		if (gl_CullMode != GL_FRONT_AND_BACK) IDirect3DDevice8_DrawVerticesUP (d3d_Device, D3DPT_TRIANGLEFAN, d3d_NumVerts, d3d_Vertexes, sizeof (gl_vertex_t));
		break;

	case GL_QUADS:
		//OutputDebugString("GL_QUADS\n");

		if (gl_CullMode == GL_FRONT_AND_BACK) break;

#ifdef _XBOX // Use special D3DPT_QUADLIST Xbox extension for speed
		IDirect3DDevice8_DrawVerticesUP(d3d_Device, D3DPT_QUADLIST, d3d_NumVerts, d3d_Vertexes, sizeof (gl_vertex_t));
#else
		// quads are a special case of trifans where each quad (numverts / 4) represents a trifan with 2 prims in it
		for (i = 0; i < d3d_NumVerts; i += 4)
			IDirect3DDevice8_DrawPrimitiveUP (d3d_Device, D3DPT_TRIANGLEFAN, 2, &d3d_Vertexes[i], sizeof (gl_vertex_t));
#endif
		break;

	case GL_LINES:
//		IDirect3DDevice8_DrawPrimitiveUP (d3d_Device, D3DPT_LINELIST, d3d_NumVerts, d3d_Vertexes, sizeof (gl_vertex_t));
		IDirect3DDevice8_DrawVerticesUP (d3d_Device, D3DPT_LINELIST, d3d_NumVerts, d3d_Vertexes, sizeof (gl_vertex_t));
		break;

	case GL_QUAD_STRIP:
#ifdef _XBOX // Use special D3DPT_QUADSTRIP Xbox extension for speed
		IDirect3DDevice8_DrawVerticesUP(d3d_Device, D3DPT_QUADSTRIP, d3d_NumVerts, d3d_Vertexes, sizeof (gl_vertex_t));
#else
		// not as optimal as it could be, so hopefully it won't be used too often!!!
		if (gl_CullMode == GL_FRONT_AND_BACK) break;

		for (i = 0; ; i += 2)
		{
			short quadindexes[4];

			if (i > (d3d_NumVerts - 3)) break;

			quadindexes[0] = i;
			quadindexes[1] = i + 1;
			quadindexes[2] = i + 3;
			quadindexes[3] = i + 2;

			IDirect3DDevice8_DrawIndexedPrimitiveUP
			(
				d3d_Device,
				D3DPT_TRIANGLEFAN,
				quadindexes[0],
				4,
				2,
				quadindexes,
				D3DFMT_INDEX16,
				d3d_Vertexes,
				sizeof (gl_vertex_t)
			);
		}
#endif
		break;

	default:
		// unsupported mode
		break;
	}

	// begin a new primitive
	d3d_NumVerts = 0;
}


void glVertex2fv (const GLfloat *v)
{
	glVertex3f (v[0], v[1], 0);
}


void glVertex2f (GLfloat x, GLfloat y)
{
	glVertex3f (x, y, 0);
}


void glVertex3fv (const GLfloat *v)
{
	glVertex3f (v[0], v[1], v[2]);
}


void glVertex3f (GLfloat x, GLfloat y, GLfloat z)
{
	int i;

	// add a new vertex to the list with the specified xyz and inheriting the current normal, color and texcoords
	// (per spec at http://www.opengl.org/sdk/docs/man/xhtml/glVertex.xml)
	d3d_Vertexes[d3d_NumVerts].position.x = x;
	d3d_Vertexes[d3d_NumVerts].position.y = y;
	d3d_Vertexes[d3d_NumVerts].position.z = z;

//	d3d_Vertexes[d3d_NumVerts].normal.x = d3d_CurrentNormal.x;
//	d3d_Vertexes[d3d_NumVerts].normal.y = d3d_CurrentNormal.y;
//	d3d_Vertexes[d3d_NumVerts].normal.z = d3d_CurrentNormal.z;

	d3d_Vertexes[d3d_NumVerts].c = d3d_CurrentColor;

	for (i = 0; i < D3D_MAX_TMUS; i++)
	{
		d3d_Vertexes[d3d_NumVerts].st[i].s = d3d_CurrentTexCoord[i].s;
		d3d_Vertexes[d3d_NumVerts].st[i].t = d3d_CurrentTexCoord[i].t;
	}

	// go to a new vertex
	d3d_NumVerts++;

	// check for end of vertexes
	if (d3d_NumVerts == D3D_MAX_VERTEXES)
	{
		if (d3d_PrimitiveMode == GL_TRIANGLES || d3d_PrimitiveMode == GL_QUADS)
		{
			// triangles and quads are discrete primitives and as such can begin a new batch
			GL_SubmitVertexes ();
		}
		else
		{
			// other primitives need to extend the vertex storage
		}

		d3d_NumVerts = 0;
	}
}


void glTexCoord2f (GLfloat s, GLfloat t)
{
	d3d_CurrentTexCoord[0].s = s;
	d3d_CurrentTexCoord[0].t = t;
}


void glTexCoord2fv (const GLfloat *v)
{
	d3d_CurrentTexCoord[0].s = v[0];
	d3d_CurrentTexCoord[0].t = v[1];
}


void GL_SetColor (int red, int green, int blue, int alpha)
{
	// overwrite color incase verts set it
	d3d_CurrentColor = D3DCOLOR_ARGB
	(
		BYTE_CLAMP (alpha),
		BYTE_CLAMP (red),
		BYTE_CLAMP (green),
		BYTE_CLAMP (blue)
	);
}


void glColor3f (GLfloat red, GLfloat green, GLfloat blue)
{
	GL_SetColor (red * 255, green * 255, blue * 255, 255);
}


void glColor3fv (const GLfloat *v)
{
	GL_SetColor (v[0] * 255, v[1] * 255, v[2] * 255, 255);
}


void glColor3ubv (const GLubyte *v)
{
	GL_SetColor (v[0], v[1], v[2], 255);
}


void glColor4f (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
	GL_SetColor (red * 255, green * 255, blue * 255, alpha * 255);
}


void glColor4fv (const GLfloat *v)
{
	GL_SetColor (v[0] * 255, v[1] * 255, v[2] * 255, v[3] * 255);
}


void glColor4ubv (const GLubyte *v)
{
	GL_SetColor (v[0], v[1], v[2], v[3]);
}


void glColor4ub (GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha)
{
	GL_SetColor (red, green, blue, alpha);
}


void glNormal3f (GLfloat nx, GLfloat ny, GLfloat nz)
{
//	d3d_CurrentNormal.x = nx;
//	d3d_CurrentNormal.y = ny;
//	d3d_CurrentNormal.z = nz;
}


void glBegin (GLenum mode)
{
	// just store out the mode, all heavy lifting is done in glEnd
	d3d_PrimitiveMode = mode;

	// begin a new primitive
	//d3d_NumVerts = 0;
}


void glEnd (void)
{
	// submit, bitch
	GL_SubmitVertexes ();
}


/*
===================================================================================================================

			VERTEX ARRAYS

===================================================================================================================
*/

typedef struct gl_varray_pointer_s
{
	GLint size;
	GLenum type;
	GLsizei stride;
	GLvoid *pointer;
} gl_varray_pointer_t;

gl_varray_pointer_t d3d_VertexPointer;
gl_varray_pointer_t d3d_ColorPointer;
gl_varray_pointer_t d3d_TexCoordPointer[D3D_MAX_TMUS];
int d3d_VArray_TMU = 0;

void WINAPI GL_ClientActiveTexture (GLenum texture)
{
	d3d_VArray_TMU = D3D_TMUForTexture (texture);
}


void glEnableClientState (GLenum array)
{
	switch (array)
	{
	case GL_VERTEX_ARRAY:
	case GL_COLOR_ARRAY:
	case GL_TEXTURE_COORD_ARRAY:
		// doesn't need to do anything
		break;

	default:
		SysMessage ("Invalid Vertex Array Spec...!");
	}
}


void glDrawArrays (GLenum mode, GLint first, GLsizei count)
{
	int i;
	int v;
	int tmu;
	byte *vp;
	byte *stp[D3D_MAX_TMUS];

	// required by the spec
	if (!d3d_VertexPointer.pointer) return;

	vp = ((byte *) d3d_VertexPointer.pointer + first);

	for (tmu = 0; tmu < D3D_MAX_TMUS; tmu++)
	{
		if (d3d_TexCoordPointer[tmu].pointer)
			stp[tmu] = ((byte *) d3d_TexCoordPointer[tmu].pointer + first);
		else stp[tmu] = NULL;
	}

	// send through standard begin/end processing
	glBegin (mode);

	for (i = 0, v = first; i < count; i++, v++)
	{
		for (tmu = 0; tmu < D3D_MAX_TMUS; tmu++)
		{
			if (stp[tmu])
			{
				d3d_CurrentTexCoord[tmu].s = ((float *) stp[tmu])[0];
				d3d_CurrentTexCoord[tmu].t = ((float *) stp[tmu])[1];

				stp[tmu] += d3d_TexCoordPointer[tmu].stride;
			}
		}

		if (d3d_VertexPointer.size == 2)
			glVertex2fv ((float *) vp);
		else if (d3d_VertexPointer.size == 3)
			glVertex3fv ((float *) vp);

		vp += d3d_VertexPointer.stride;
	}

	glEnd ();
}


void glVertexPointer (GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
	if (type != GL_FLOAT) SysMessage ("Unimplemented vertex pointer type");

	d3d_VertexPointer.size = size;
	d3d_VertexPointer.type = type;
	d3d_VertexPointer.stride = stride;
	d3d_VertexPointer.pointer = (GLvoid *) pointer;
}


void glColorPointer (GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
	if (type != GL_FLOAT) SysMessage ("Unimplemented color pointer type");

	d3d_ColorPointer.size = size;
	d3d_ColorPointer.type = type;
	d3d_ColorPointer.stride = stride;
	d3d_ColorPointer.pointer = (GLvoid *) pointer;
}


void glTexCoordPointer (GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
	if (type != GL_FLOAT) SysMessage ("Unimplemented texcoord pointer type");

	d3d_TexCoordPointer[d3d_VArray_TMU].size = size;
	d3d_TexCoordPointer[d3d_VArray_TMU].type = type;
	d3d_TexCoordPointer[d3d_VArray_TMU].stride = stride;
	d3d_TexCoordPointer[d3d_VArray_TMU].pointer = (GLvoid *) pointer;
}


void glDisableClientState (GLenum array)
{
	// switch the pointer to NULL
	switch (array)
	{
	case GL_VERTEX_ARRAY:
		d3d_VertexPointer.pointer = NULL;
		break;

	case GL_COLOR_ARRAY:
		d3d_ColorPointer.pointer = NULL;
		break;

	case GL_TEXTURE_COORD_ARRAY:
		d3d_TexCoordPointer[d3d_VArray_TMU].pointer = NULL;
		break;

	default:
		SysMessage ("Invalid Vertex Array Spec...!");
	}
}

/*
===================================================================================================================

			TEXTURE HASHMAP

===================================================================================================================
*/

#define HASHMAP_SIZE 500

struct sNode
{
	int iKey;
	d3d_texture_t* pValue;
	struct sNode *pNext;
};

struct sTable
{
	int iSize;
	struct sNode **pList;
};

struct sTable *g_pTable = NULL;

void HMCreateTable()
{
	int i;

	g_pTable = (struct sTable*)malloc(sizeof(struct sTable));
	g_pTable->iSize = HASHMAP_SIZE;
	
	g_pTable->pList = (struct sNode**)malloc(sizeof(struct node*)*HASHMAP_SIZE);
    
	for(i = 0; i < HASHMAP_SIZE; i++)
		g_pTable->pList[i] = NULL;
}

void HMDeleteTable()
{
	if(g_pTable) free(g_pTable);
}

int HMHashFunc(int iKey)
{
	if(iKey < 0)
		return -(iKey % g_pTable->iSize);
        
	return iKey % g_pTable->iSize;
}

void HMInsertTexture(int iKey, d3d_texture_t* pTexture)
{
	int iPos = HMHashFunc(iKey);
   
	struct sNode *pList = g_pTable->pList[iPos];
	struct sNode *pNewNode = NULL;
	struct sNode *pTemp = pList;
   
	while(pTemp)
	{
		if(pTemp->iKey == iKey)
		{
			pTemp->pValue = pTexture;
			return;
		}
		pTemp = pTemp->pNext;
	}

	pNewNode = (struct sNode*)malloc(sizeof(struct sNode));
	pNewNode->iKey = iKey;
	pNewNode->pValue = pTexture;
	pNewNode->pNext = pList;

	g_pTable->pList[iPos] = pNewNode;
}

d3d_texture_t* HMRemoveTexture(int iKey)
{
	int iPos = HMHashFunc(iKey);
	d3d_texture_t* pTexture = NULL;
	
	if (g_pTable->pList[iPos] != NULL)
	{       
		struct sNode *pPrev = NULL;
		struct sNode *pCurr = g_pTable->pList[iPos];

		while(pCurr->pNext != NULL && pCurr->iKey != iKey)
		{
			pPrev = pCurr;
			pCurr = pCurr->pNext;
		}

		if (pCurr->iKey == iKey)
		{
			struct sNode *pNextEntry = pCurr->pNext;
			
			if (pPrev)
				pPrev->pNext = pNextEntry;
			else g_pTable->pList[iPos] = pNextEntry;

			pTexture = pCurr->pValue;
			
			if(pCurr)
				free(pCurr);

			return pTexture;
		}
		else if (pCurr->pNext != NULL)
			return NULL; // Not found!
	}

	return NULL;
}

extern void DeleteSubImageCache(int iNum);

void HMRemoveAllTextures()
{
	int i;

	for(i = 0; i < g_pTable->iSize; i++)
	{
		struct sNode *pList = g_pTable->pList[i];
		struct sNode *pTemp = pList;
		
		while(pTemp)
		{
			struct sNode *pCurr = pTemp;
			pTemp = pTemp->pNext;

			if(pCurr)
			{
				DeleteSubImageCache(pCurr->pValue->glnum);

				// Release the texture
				if(pCurr->pValue->teximg)
				{
					IDirect3DTexture8_Release(pCurr->pValue->teximg);
					pCurr->pValue->teximg = NULL;
				} 

				free(pCurr);
				pCurr = NULL;
			}
		}
	}
}

d3d_texture_t* HMLookupTexture(int iKey)
{
	int iPos = HMHashFunc(iKey);
    
	struct sNode *pList = g_pTable->pList[iPos];
	struct sNode *pTemp = pList;
    
	while(pTemp)
	{
		if(pTemp->iKey == iKey)
			return pTemp->pValue;
        
		pTemp = pTemp->pNext;
	}

	return NULL;
}

/*
===================================================================================================================

			TEXTURES

===================================================================================================================
*/

d3d_texture_t *D3D_AllocTexture (int iTextureID)
{
	d3d_texture_t *pTex;

	// Clear to 0 is required so that D3D_SAFE_RELEASE is valid
	pTex = (d3d_texture_t *) malloc(sizeof(d3d_texture_t));
	memset(pTex, 0, sizeof (d3d_texture_t));

	D3D_InitTexture(pTex);

	// Add to our hashmap
	HMInsertTexture(iTextureID, pTex);

	return pTex;
}


/*
======================
D3D_CheckTextureFormat

Ensures that a given texture format will be available
======================
*/
BOOL D3D_CheckTextureFormat (D3DFORMAT TextureFormat, D3DFORMAT AdapterFormat)
{
	hr = IDirect3D8_CheckDeviceFormat
	(
		d3d_Object,
		D3DADAPTER_DEFAULT,
		D3DDEVTYPE_HAL,
		AdapterFormat,
		0,
		D3DRTYPE_TEXTURE,
		TextureFormat
	);

	return SUCCEEDED (hr);
}


void glTexEnvf (GLenum target, GLenum pname, GLfloat param)
{
	if (target != GL_TEXTURE_ENV) SysMessage ("glTexEnvf: unimplemented target");

	d3d_TMUs[d3d_CurrentTMU].texenvdirty = TRUE;

	switch (pname)
	{
	case GL_TEXTURE_ENV_MODE:
		// this is the default mode
		switch ((GLint) param)
		{
		case GLD3D_COMBINE:
			// flag a combine
			//d3d_TMUs[d3d_CurrentTMU].combine = TRUE;
			break;

		case GL_ADD:
			d3d_TMUs[d3d_CurrentTMU].colorop = D3DTOP_ADD;
			d3d_TMUs[d3d_CurrentTMU].colorarg1 = D3DTA_TEXTURE;
			d3d_TMUs[d3d_CurrentTMU].colorarg2 = (d3d_CurrentTMU == 0 ? D3DTA_DIFFUSE : D3DTA_CURRENT);

			d3d_TMUs[d3d_CurrentTMU].alphaop = D3DTOP_ADD;
			d3d_TMUs[d3d_CurrentTMU].alphaarg1 = D3DTA_TEXTURE;
			d3d_TMUs[d3d_CurrentTMU].alphaarg2 = D3DTA_DIFFUSE;

			//d3d_TMUs[d3d_CurrentTMU].combine = FALSE;
			break;

		case GL_REPLACE:
			// there was a reason why i changed this to modulate but i can't remember it :(
			// anyway, it needs to be D3DTOP_SELECTARG1 for Quake, so D3DTOP_SELECTARG1 it is...
			d3d_TMUs[d3d_CurrentTMU].colorop = D3DTOP_SELECTARG1;
			d3d_TMUs[d3d_CurrentTMU].colorarg1 = D3DTA_TEXTURE;
			d3d_TMUs[d3d_CurrentTMU].colorarg2 = D3DTA_DIFFUSE;

			d3d_TMUs[d3d_CurrentTMU].alphaop = D3DTOP_SELECTARG1;
			d3d_TMUs[d3d_CurrentTMU].alphaarg1 = D3DTA_TEXTURE;
			d3d_TMUs[d3d_CurrentTMU].alphaarg2 = D3DTA_DIFFUSE;

			//d3d_TMUs[d3d_CurrentTMU].combine = FALSE;
			break;

		case GL_MODULATE:
			d3d_TMUs[d3d_CurrentTMU].colorop = D3DTOP_MODULATE;
			d3d_TMUs[d3d_CurrentTMU].colorarg1 = D3DTA_TEXTURE;
			d3d_TMUs[d3d_CurrentTMU].colorarg2 = (d3d_CurrentTMU == 0 ? D3DTA_DIFFUSE : D3DTA_CURRENT);

			d3d_TMUs[d3d_CurrentTMU].alphaop = D3DTOP_MODULATE;
			d3d_TMUs[d3d_CurrentTMU].alphaarg1 = D3DTA_TEXTURE;
			d3d_TMUs[d3d_CurrentTMU].alphaarg2 = D3DTA_DIFFUSE;

			//d3d_TMUs[d3d_CurrentTMU].combine = FALSE;
			break;

		case GL_DECAL:
			d3d_TMUs[d3d_CurrentTMU].colorop = D3DTOP_BLENDTEXTUREALPHA;
			d3d_TMUs[d3d_CurrentTMU].colorarg1 = D3DTA_TEXTURE;
			d3d_TMUs[d3d_CurrentTMU].colorarg2 = (d3d_CurrentTMU == 0 ? D3DTA_DIFFUSE : D3DTA_CURRENT);

			d3d_TMUs[d3d_CurrentTMU].alphaop = D3DTOP_SELECTARG1;
			d3d_TMUs[d3d_CurrentTMU].alphaarg1 = D3DTA_TEXTURE;
			d3d_TMUs[d3d_CurrentTMU].alphaarg2 = D3DTA_DIFFUSE;

			//d3d_TMUs[d3d_CurrentTMU].combine = FALSE;
			break;

		case GL_BLEND:
			d3d_TMUs[d3d_CurrentTMU].colorop = D3DTOP_MODULATE;
			d3d_TMUs[d3d_CurrentTMU].colorarg1 = D3DTA_TEXTURE | D3DTA_COMPLEMENT;
			d3d_TMUs[d3d_CurrentTMU].colorarg2 = (d3d_CurrentTMU == 0 ? D3DTA_DIFFUSE : D3DTA_CURRENT);

			d3d_TMUs[d3d_CurrentTMU].alphaop = D3DTOP_SELECTARG1;
			d3d_TMUs[d3d_CurrentTMU].alphaarg1 = D3DTA_TEXTURE;
			d3d_TMUs[d3d_CurrentTMU].alphaarg2 = D3DTA_DIFFUSE;

			//d3d_TMUs[d3d_CurrentTMU].combine = FALSE;
			break;

		default:
			SysMessage ("glTexEnvf: unimplemented param");
			break;
		}

		break;

	case GLD3D_COMBINE_RGB:
		//if ((int) param == GL_MODULATE)
		//	d3d_TMUs[d3d_CurrentTMU].combstate.colorop = D3DTOP_MODULATE;
		//else SysMessage ("glTexEnvf: unimplemented param");
		break;

	case GLD3D_SOURCE0_RGB:
		//if ((int) param == GL_TEXTURE)
		//	d3d_TMUs[d3d_CurrentTMU].combstate.colorarg1 = D3DTA_TEXTURE;
		//else if ((int) param == GLD3D_PREVIOUS)
		//	d3d_TMUs[d3d_CurrentTMU].combstate.colorarg1 = D3DTA_CURRENT;
		//else SysMessage ("glTexEnvf: unimplemented param");
		break;

	case GLD3D_SOURCE1_RGB:
		//if ((int) param == GL_TEXTURE)
		//	d3d_TMUs[d3d_CurrentTMU].combstate.colorarg2 = D3DTA_TEXTURE;
		//else if ((int) param == GLD3D_PRIMARY_COLOR)
		//	d3d_TMUs[d3d_CurrentTMU].combstate.colorarg2 = D3DTA_DIFFUSE;
		//else SysMessage ("glTexEnvf: unimplemented param");
		break;

	case GLD3D_RGB_SCALE:
		// d3d only allows 1/2/4 x scale and in practice so do most OpenGL implementations
		// (this is actually required by the spec: see http://www.opengl.org/sdk/docs/man/xhtml/glTexEnv.xml)
		//if (param > 2)
		//	d3d_TMUs[d3d_CurrentTMU].combstate.colorscale = 4;
		//else if (param > 1)
		//	d3d_TMUs[d3d_CurrentTMU].combstate.colorscale = 2;
		//else d3d_TMUs[d3d_CurrentTMU].combstate.colorscale = 1;
		break;

	default:
		SysMessage ("glTexEnvf: unimplemented pname");
		break;
	}
}


void glTexEnvi (GLenum target, GLenum pname, GLint param)
{
	if (target != GL_TEXTURE_ENV) return;

	glTexEnvf (target, pname, param);
}

#if 0
void D3D_FillTextureLevel (LPDIRECT3DTEXTURE8 texture, int level, GLint internalformat, int width, int height, GLint format, const void *pixels)
{
	int i;
	int srcbytes = 0;
	int dstbytes = 0;
	byte *srcdata;
	byte *dstdata;
	D3DLOCKED_RECT lockrect;

	if (format == 1 || format == GL_LUMINANCE)
		srcbytes = 1;
	else if (format == 3 || format == GL_RGB)
		srcbytes = 3;
	else if (format == 4 || format == GL_RGBA)
		srcbytes = 4;
	else SysMessage ("D3D_FillTextureLevel: illegal format");

	// d3d doesn't have an internal RGB only format
	// (neither do most OpenGL implementations, they just let you specify it as RGB but expand internally to 4 component)
	if (internalformat == 1 || internalformat == GL_LUMINANCE)
		dstbytes = 1;
	else if (internalformat == 3 || internalformat == GL_RGB)
		dstbytes = 4;
	else if (internalformat == 4 || internalformat == GL_RGBA)
		dstbytes = 4;
	else SysMessage ("D3D_FillTextureLevel: illegal internalformat");

	IDirect3DTexture8_LockRect (texture, level, &lockrect, NULL, 0);

	srcdata = (byte *) pixels;
	dstdata = lockrect.pBits;

	for (i = 0; i < width * height; i++)
	{
		if (srcbytes == 1)
		{
			if (dstbytes == 1)
				dstdata[0] = srcdata[0];
			else if (dstbytes == 4)
			{
				dstdata[0] = srcdata[0];
				dstdata[1] = srcdata[0];
				dstdata[2] = srcdata[0];
				dstdata[3] = srcdata[0];
			}
		}
		else if (srcbytes == 3)
		{
			if (dstbytes == 1)
				dstdata[0] = ((int) srcdata[0] + (int) srcdata[1] + (int) srcdata[2]) / 3;
			else if (dstbytes == 4)
			{
				dstdata[0] = srcdata[2];
				dstdata[1] = srcdata[1];
				dstdata[2] = srcdata[0];
				dstdata[3] = 255;
			}
		}
		else if (srcbytes == 4)
		{
			if (dstbytes == 1)
				dstdata[0] = ((int) srcdata[0] + (int) srcdata[1] + (int) srcdata[2]) / 3;
			else if (dstbytes == 4)
			{
				dstdata[0] = srcdata[2];
				dstdata[1] = srcdata[1];
				dstdata[2] = srcdata[0];
				dstdata[3] = srcdata[3];
			}
		}

		// advance
		srcdata += srcbytes;
		dstdata += dstbytes;
	}

	IDirect3DTexture8_UnlockRect (texture, level);
}
#endif

#if 1 // Swizzled
void D3D_FillTextureLevel (LPDIRECT3DTEXTURE8 texture, int level, GLint internalformat, int width, int height, GLint format, const void *pixels)
{
	int i;
	int srcbytes = 0;
	int dstbytes = 0;
	byte *srcdata;
	byte *dstdata;
	D3DLOCKED_RECT lockrect;

	D3DLOCKED_RECT lr, lr2;
	D3DSURFACE_DESC desc;
	LPDIRECT3DSURFACE8 surface;
	LPDIRECT3DSURFACE8 surfaceTemp;

	if (format == 1 || format == GL_LUMINANCE || format == GL_LUMINANCE8 )
		srcbytes = 1;
	else if (format == 3 || format == GL_RGB)
		srcbytes = 3;
	else if (format == 4 || format == GL_RGBA)
		srcbytes = 4;
	else SysMessage ("D3D_FillTextureLevel: illegal format");

	// d3d doesn't have an internal RGB only format
	// (neither do most OpenGL implementations, they just let you specify it as RGB but expand internally to 4 component)
	if (internalformat == 1 || internalformat == GL_LUMINANCE || internalformat == GL_LUMINANCE8 )
		dstbytes = 1;
	else if (internalformat == 3 || internalformat == GL_RGB)
		dstbytes = 4;
	else if (internalformat == 4 || internalformat == GL_RGBA)
		dstbytes = 4;
	else SysMessage ("D3D_FillTextureLevel: illegal internalformat");

	IDirect3DTexture8_LockRect (texture, level, &lockrect, NULL, 0);

	srcdata = (byte *) pixels;
	dstdata = (byte *) lockrect.pBits;

	for (i = 0; i < width * height; i++)
	{
		if (srcbytes == 1)
		{
			if (dstbytes == 1)
				dstdata[0] = srcdata[0];
			else if (dstbytes == 4)
			{
				dstdata[0] = srcdata[0];
				dstdata[1] = srcdata[0];
				dstdata[2] = srcdata[0];
				dstdata[3] = srcdata[0];
			}
		}
		else if (srcbytes == 3)
		{
			if (dstbytes == 1)
				dstdata[0] = ((int) srcdata[0] + (int) srcdata[1] + (int) srcdata[2]) / 3;
			else if (dstbytes == 4)
			{
				dstdata[0] = srcdata[2];
				dstdata[1] = srcdata[1];
				dstdata[2] = srcdata[0];
				dstdata[3] = 255;
			}
		}
		else if (srcbytes == 4)
		{
			if (dstbytes == 1)
				dstdata[0] = ((int) srcdata[0] + (int) srcdata[1] + (int) srcdata[2]) / 3;
			else if (dstbytes == 4)
			{
				dstdata[0] = srcdata[2];
				dstdata[1] = srcdata[1];
				dstdata[2] = srcdata[0];
				dstdata[3] = srcdata[3];
			}
		}

		// advance
		srcdata += srcbytes;
		dstdata += dstbytes;
	}

	IDirect3DTexture8_GetLevelDesc(texture, level, &desc);

	//Create temporary surface
	IDirect3DDevice8_CreateImageSurface (d3d_Device, desc.Width, desc.Height, desc.Format, &surfaceTemp);

	//Lock the texture
	IDirect3DTexture8_LockRect (texture, 0, &lr, NULL, 0);

	//go down to surface level
	IDirect3DTexture8_GetSurfaceLevel(texture, level, &surface); 

	//copy surf to temp surf
	D3DXLoadSurfaceFromSurface(surfaceTemp, NULL, NULL, surface, NULL, NULL, D3DX_FILTER_NONE, 0);

	IDirect3DSurface8_LockRect (surfaceTemp, &lr2, NULL, 0);

    // Xbox textures need to be swizzled
    XGSwizzleRect(
                  lr2.pBits,      // pSource, 
		          lr2.Pitch,      // Pitch,
                  NULL,           // pRect,
                  lr.pBits,       // pDest,
                  desc.Width,     // Width,
                  desc.Height,    // Height,
                  NULL,           // pPoint,
                  dstbytes );     // BytesPerPixel

	IDirect3DSurface8_UnlockRect (surfaceTemp);
	IDirect3DSurface8_Release(surfaceTemp);

	IDirect3DSurface8_Release(surface);
	IDirect3DTexture8_UnlockRect (texture, level);
}
#endif

void D3D_CopyTextureLevel (LPDIRECT3DTEXTURE8 srctex, int srclevel, LPDIRECT3DTEXTURE8 dsttex, int dstlevel)
{
	LPDIRECT3DSURFACE8 srcsurf;
	LPDIRECT3DSURFACE8 dstsurf;

	IDirect3DTexture8_GetSurfaceLevel (srctex, srclevel, &srcsurf);
	IDirect3DTexture8_GetSurfaceLevel (dsttex, dstlevel, &dstsurf);

	D3DXLoadSurfaceFromSurface (dstsurf, NULL, NULL, srcsurf, NULL, NULL, D3DX_FILTER_POINT, 0);

//	D3D_SAFE_RELEASE (srcsurf); 
    IDirect3DSurface8_Release(srcsurf);
//	D3D_SAFE_RELEASE (dstsurf);
    IDirect3DSurface8_Release(dstsurf);
}


void glGetTexImage (GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels)
{
	int i;
	int dstbytes = 0;
	unsigned char *srcdata;
	unsigned char *dstdata;
	LPDIRECT3DSURFACE8 texsurf;
	LPDIRECT3DSURFACE8 locksurf;
	D3DLOCKED_RECT lockrect;
	D3DSURFACE_DESC desc;

	if (target != GL_TEXTURE_2D) return;
	if (type != GL_UNSIGNED_BYTE) return;

	if (format == GL_RGB)
		dstbytes = 3;
	else if (format == GL_RGBA)
		dstbytes = 4;
	else SysMessage ("glGetTexImage: invalid format");

	hr = IDirect3DTexture8_GetSurfaceLevel (d3d_TMUs[d3d_CurrentTMU].boundtexture->teximg, level, &texsurf);

	if (FAILED (hr) || !texsurf)
	{
		SysMessage ("glGetTexImage: failed to access back buffer\n");
		return;
	}

	// because textures can be different formats we create it as one big enough to hold them all
	hr = IDirect3DSurface8_GetDesc (texsurf, &desc);
	hr = IDirect3DDevice8_CreateImageSurface (d3d_Device, desc.Width, desc.Height, D3DFMT_A8R8G8B8, &locksurf);
	hr = D3DXLoadSurfaceFromSurface (locksurf, NULL, NULL, texsurf, NULL, NULL, D3DX_FILTER_NONE, 0);

	// now we have a surface we can lock
	hr = IDirect3DSurface8_LockRect (locksurf, &lockrect, NULL, D3DLOCK_READONLY);
	srcdata = (unsigned char *) lockrect.pBits;
	dstdata = (unsigned char *) pixels;

	for (i = 0; i < desc.Width * desc.Height; i++)
	{
		// swap back
		dstdata[0] = srcdata[2];
		dstdata[1] = srcdata[1];
		dstdata[2] = srcdata[0];

		if (dstbytes == 4) dstdata[3] = srcdata[3];

		srcdata += 4;
		dstdata += dstbytes;
	}

	// done
	IDirect3DSurface8_UnlockRect (locksurf);
	//D3D_SAFE_RELEASE (locksurf);
    IDirect3DSurface8_Release(locksurf);
	//D3D_SAFE_RELEASE (texsurf);
    IDirect3DSurface8_Release(texsurf);
}


void glTexImage2D (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
	D3DFORMAT texformat = D3DFMT_X8R8G8B8;

	// validate format
	switch (internalformat)
	{
	case 1:
	case GL_LUMINANCE:
	case GL_LUMINANCE8: //MARTY
		texformat = D3DFMT_L8;
		break;

	case 3:
	case GL_RGB:
		texformat = D3DFMT_X8R8G8B8;
		break;

	case 4:
	case GL_RGBA:
		texformat = D3DFMT_A8R8G8B8;
		break;

	default:
		SysMessage ("invalid texture internal format");
	}

	if (type != GL_UNSIGNED_BYTE) SysMessage ("glTexImage2D: Unrecognised pixel format");

	// ensure that it's valid to create textures
	if (target != GL_TEXTURE_2D) return;
	if (!d3d_TMUs[d3d_CurrentTMU].boundtexture) return;

	if (level == 0)
	{
		// in THEORY an OpenGL texture can have different formats and internal formats for each miplevel
		// in practice I don't think anyone uses it...
		d3d_TMUs[d3d_CurrentTMU].boundtexture->internalformat = internalformat;

		// overwrite an existing texture - just release it so that we can recreate
		//D3D_SAFE_RELEASE (d3d_TMUs[d3d_CurrentTMU].boundtexture->teximg);

		if(d3d_TMUs[d3d_CurrentTMU].boundtexture->teximg)
		{
			IDirect3DTexture8_Release(d3d_TMUs[d3d_CurrentTMU].boundtexture->teximg);
			d3d_TMUs[d3d_CurrentTMU].boundtexture->teximg = NULL;
		}   

		// create the texture from the data
		// initially just create a single mipmap level (assume that the texture isn't mipped)
		hr = IDirect3DDevice8_CreateTexture
		(
			d3d_Device,
			width,
			height,
			1,
			0,
			texformat,
			D3DPOOL_MANAGED,
			&d3d_TMUs[d3d_CurrentTMU].boundtexture->teximg
		);

		if (FAILED (hr)) SysMessage ("glTexImage2D: unable to create a texture");

		D3D_FillTextureLevel (d3d_TMUs[d3d_CurrentTMU].boundtexture->teximg, 0, internalformat, width, height, format, pixels);
	}
	else if (level == 1)
	{
		// we're creating subsequent miplevels so we need to recreate this texture
		LPDIRECT3DTEXTURE8 newtexture;

		// this is miplevel 1 and we're recreating level 0, so double width and height
		hr = IDirect3DDevice8_CreateTexture
		(
			d3d_Device,
			width * 2,
			height * 2,
			0,
			0,
			texformat,
			D3DPOOL_MANAGED,
			&newtexture
		);

		if (FAILED (hr)) SysMessage ("glTexImage2D: unable to create a texture");

		// copy level 0 across to the new texture
		D3D_CopyTextureLevel (d3d_TMUs[d3d_CurrentTMU].boundtexture->teximg, 0, newtexture, 0);

		// release the former level 0 texture and reset to the new one

		//D3D_SAFE_RELEASE (d3d_TMUs[d3d_CurrentTMU].boundtexture->teximg);

		if(d3d_TMUs[d3d_CurrentTMU].boundtexture->teximg)
		{
			IDirect3DTexture8_Release(d3d_TMUs[d3d_CurrentTMU].boundtexture->teximg);
			d3d_TMUs[d3d_CurrentTMU].boundtexture->teximg = NULL;
		}   
		
		d3d_TMUs[d3d_CurrentTMU].boundtexture->teximg = newtexture;

		D3D_FillTextureLevel (d3d_TMUs[d3d_CurrentTMU].boundtexture->teximg, 1, internalformat, width, height, format, pixels);
	}
	else
	{
		// the texture has already been created so no need to do any more
		D3D_FillTextureLevel (d3d_TMUs[d3d_CurrentTMU].boundtexture->teximg, level, internalformat, width, height, format, pixels);
	}
}


void glGetTexParameterfv (GLenum target, GLenum pname, GLfloat *params)
{
	if (!d3d_TMUs[d3d_CurrentTMU].boundtexture) return;
	if (target != GL_TEXTURE_2D) return;

	switch (pname)
	{
	case GLD3D_TEXTURE_MAX_ANISOTROPY_EXT:
		params[0] = d3d_TMUs[d3d_CurrentTMU].boundtexture->anisotropy;
		break;

	default:
		break;
	}
}

void glTexParameterf (GLenum target, GLenum pname, GLfloat param)
{
	if (!d3d_TMUs[d3d_CurrentTMU].boundtexture) return;
	if (target != GL_TEXTURE_2D) return;

	d3d_TMUs[d3d_CurrentTMU].texparamdirty = TRUE;

	switch (pname)
	{
	case GL_TEXTURE_MIN_FILTER:
		if ((int) param == GL_NEAREST_MIPMAP_NEAREST)
		{
			d3d_TMUs[d3d_CurrentTMU].boundtexture->minfilter = D3DTEXF_POINT;
			d3d_TMUs[d3d_CurrentTMU].boundtexture->mipfilter = D3DTEXF_POINT;
		}
		else if ((int) param == GL_LINEAR_MIPMAP_NEAREST)
		{
			d3d_TMUs[d3d_CurrentTMU].boundtexture->minfilter = D3DTEXF_LINEAR;
			d3d_TMUs[d3d_CurrentTMU].boundtexture->mipfilter = D3DTEXF_POINT;
		}
		else if ((int) param == GL_NEAREST_MIPMAP_LINEAR)
		{
			d3d_TMUs[d3d_CurrentTMU].boundtexture->minfilter = D3DTEXF_POINT;
			d3d_TMUs[d3d_CurrentTMU].boundtexture->mipfilter = D3DTEXF_LINEAR;
		}
		else if ((int) param == GL_LINEAR_MIPMAP_LINEAR)
		{
			d3d_TMUs[d3d_CurrentTMU].boundtexture->minfilter = D3DTEXF_LINEAR;
			d3d_TMUs[d3d_CurrentTMU].boundtexture->mipfilter = D3DTEXF_LINEAR;
		}
		else if ((int) param == GL_LINEAR)
		{
			d3d_TMUs[d3d_CurrentTMU].boundtexture->minfilter = D3DTEXF_LINEAR;
			d3d_TMUs[d3d_CurrentTMU].boundtexture->mipfilter = D3DTEXF_NONE;
		}
		else
		{
			// GL_NEAREST
			d3d_TMUs[d3d_CurrentTMU].boundtexture->minfilter = D3DTEXF_POINT;
			d3d_TMUs[d3d_CurrentTMU].boundtexture->mipfilter = D3DTEXF_NONE;
		}
		break;

	case GL_TEXTURE_MAG_FILTER:
		if ((int) param == GL_LINEAR)
			d3d_TMUs[d3d_CurrentTMU].boundtexture->magfilter = D3DTEXF_LINEAR;
		else d3d_TMUs[d3d_CurrentTMU].boundtexture->magfilter = D3DTEXF_POINT;
		break;

	case GL_TEXTURE_WRAP_S:
		if ((int) param == GL_CLAMP)
			d3d_TMUs[d3d_CurrentTMU].boundtexture->addressu = D3DTADDRESS_CLAMP;
		else d3d_TMUs[d3d_CurrentTMU].boundtexture->addressu = D3DTADDRESS_WRAP;
		break;

	case GL_TEXTURE_WRAP_T:
		if ((int) param == GL_CLAMP)
			d3d_TMUs[d3d_CurrentTMU].boundtexture->addressv = D3DTADDRESS_CLAMP;
		else d3d_TMUs[d3d_CurrentTMU].boundtexture->addressv = D3DTADDRESS_WRAP;
		break;

	case GLD3D_TEXTURE_MAX_ANISOTROPY_EXT:
		// this is a texparam in OpenGL
		if (d3d_Caps.MaxAnisotropy > 1)
			d3d_TMUs[d3d_CurrentTMU].boundtexture->anisotropy = (int) param;
		else d3d_TMUs[d3d_CurrentTMU].boundtexture->anisotropy = 1;
		break;

	default:
		break;
	}
}

void glTexParameteri (GLenum target, GLenum pname, GLint param)
{
	if(target != GL_TEXTURE_2D) return;
	if(!d3d_TMUs[d3d_CurrentTMU].boundtexture) return;

	glTexParameterf(target, pname, param);
}

SubImage_t* GetSubImageCache(int iTexNum)
{
	SubImage_t *tex;

	// find a free texture
	for (tex = SubImageCache; tex; tex = tex->next)
	{
		if(tex->iTextureNum == iTexNum)
		return tex;
	}

	tex = (SubImage_t *) malloc (sizeof (SubImage_t));
	memset (tex, 0, sizeof (SubImage_t));

	tex->iTextureNum = iTexNum;

	IDirect3DDevice8_CreateImageSurface (d3d_Device, 256, 256, D3DFMT_A8R8G8B8, &tex->texture);

	// link in
	tex->next = SubImageCache;
	SubImageCache = tex;

	// return the new one
	return tex;
}

void DeleteSubImageCache(int iNum)
{
	SubImage_t *tex;

	while(SubImageCache != NULL)
	{	
		tex = SubImageCache;

		SubImageCache = SubImageCache->next;

		tex->iTextureNum = 0;
		IDirect3DSurface8_Release(tex->texture);
		free(tex);
		tex = NULL;
	}
}

#if 0
void glTexSubImage2D (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
	int x, y;
	int srcbytes = 0;
	int dstbytes = 0;
	byte *srcdata;
	byte *dstdata;
	D3DLOCKED_RECT lockrect;

	if (format == 1 || format == GL_LUMINANCE)
		srcbytes = 1;
	else if (format == 3 || format == GL_RGB)
		srcbytes = 3;
	else if (format == 4 || format == GL_RGBA)
		srcbytes = 4;
	else SysMessage ("D3D_FillTextureLevel: illegal format");

	// d3d doesn't have an internal RGB only format
	// (neither do most OpenGL implementations, they just let you specify it as RGB but expand internally to 4 component)
	if (d3d_TMUs[d3d_CurrentTMU].boundtexture->internalformat == 1 || d3d_TMUs[d3d_CurrentTMU].boundtexture->internalformat == GL_LUMINANCE)
		dstbytes = 1;
	else if (d3d_TMUs[d3d_CurrentTMU].boundtexture->internalformat == 3 || d3d_TMUs[d3d_CurrentTMU].boundtexture->internalformat == GL_RGB)
		dstbytes = 4;
	else if (d3d_TMUs[d3d_CurrentTMU].boundtexture->internalformat == 4 || d3d_TMUs[d3d_CurrentTMU].boundtexture->internalformat == GL_RGBA)
		dstbytes = 4;
	else SysMessage ("D3D_FillTextureLevel: illegal internalformat");

	IDirect3DTexture8_LockRect (d3d_TMUs[d3d_CurrentTMU].boundtexture->teximg, level, &lockrect, NULL, 0);

	srcdata = (byte *) pixels;
	dstdata = lockrect.pBits;
	dstdata += (yoffset * width + xoffset) * dstbytes;

	for (y = yoffset; y < (yoffset + height); y++)
	{
		for (x = xoffset; x < (xoffset + width); x++)
		{
			if (srcbytes == 1)
			{
				if (dstbytes == 1)
					dstdata[0] = srcdata[0];
				else if (dstbytes == 4)
				{
					dstdata[0] = srcdata[0];
					dstdata[1] = srcdata[0];
					dstdata[2] = srcdata[0];
					dstdata[3] = srcdata[0];
				}
			}
			else if (srcbytes == 3)
			{
				if (dstbytes == 1)
					dstdata[0] = ((int) srcdata[0] + (int) srcdata[1] + (int) srcdata[2]) / 3;
				else if (dstbytes == 4)
				{
					dstdata[0] = srcdata[2];
					dstdata[1] = srcdata[1];
					dstdata[2] = srcdata[0];
					dstdata[3] = 255;
				}
			}
			else if (srcbytes == 4)
			{
				if (dstbytes == 1)
					dstdata[0] = ((int) srcdata[0] + (int) srcdata[1] + (int) srcdata[2]) / 3;
				else if (dstbytes == 4)
				{
					dstdata[0] = srcdata[2];
					dstdata[1] = srcdata[1];
					dstdata[2] = srcdata[0];
					dstdata[3] = srcdata[3];
				}
			}

			// advance
			srcdata += srcbytes;
			dstdata += dstbytes;
		}
	}

	IDirect3DTexture8_UnlockRect (d3d_TMUs[d3d_CurrentTMU].boundtexture->teximg, level);
}
#endif

#if 1 // Swizzled
void glTexSubImage2D (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
	int x, y;
	int srcbytes = 4;
	int dstbytes = 4;
	byte *srcdata;
	byte *dstdata;

	D3DLOCKED_RECT lr;
//	D3DSURFACE_DESC desc;

	POINT point;
	RECT rect;

	SubImage_t* texSelect;
	D3DLOCKED_RECT lrunswiz;

	int iElement; // For saving time in the loop!

	rect.left = point.x = xoffset;
	rect.top = point.y = yoffset;
	rect.right = xoffset + width;
	rect.bottom = yoffset + height;

/*	if (format == 1 || format == GL_LUMINANCE) // FIXME: Dont assume the scrbytes, just hardcoded for speed atm
		srcbytes = 1;
	else if (format == 3 || format == GL_RGB)
		srcbytes = 3;
	else if (format == 4 || format == GL_RGBA)
		srcbytes = 4;
	else SysMessage ("D3D_FillTextureLevel: illegal format");

	// d3d doesn't have an internal RGB only format
	// (neither do most OpenGL implementations, they just let you specify it as RGB but expand internally to 4 component)
	if (d3d_TMUs[d3d_CurrentTMU].boundtexture->internalformat == 1 || d3d_TMUs[d3d_CurrentTMU].boundtexture->internalformat == GL_LUMINANCE)
		dstbytes = 1;
	else if (d3d_TMUs[d3d_CurrentTMU].boundtexture->internalformat == 3 || d3d_TMUs[d3d_CurrentTMU].boundtexture->internalformat == GL_RGB)
		dstbytes = 4;
	else if (d3d_TMUs[d3d_CurrentTMU].boundtexture->internalformat == 4 || d3d_TMUs[d3d_CurrentTMU].boundtexture->internalformat == GL_RGBA)
		dstbytes = 4;
	else SysMessage ("D3D_FillTextureLevel: illegal internalformat");
*/
	texSelect = GetSubImageCache(d3d_TMUs[d3d_CurrentTMU].boundtexture->glnum);

	IDirect3DSurface8_LockRect (texSelect->texture, &lrunswiz, NULL, 0);	

	srcdata = (byte *) pixels;
	dstdata = (byte *) lrunswiz.pBits;

	// Saves doing addition in the loop
	height += yoffset;
	width += xoffset;

	for (y = yoffset; y < height; y++)
	{
		for (x = xoffset; x < width; x++)
		{
			iElement = lrunswiz.Pitch * y + dstbytes * x;

			dstdata[iElement] = srcdata[2];
			dstdata[iElement + 1] = srcdata[1];
			dstdata[iElement + 2] = srcdata[0];
			dstdata[iElement + 3] = srcdata[3];

			srcdata += srcbytes;
		}
	}

	IDirect3DSurface8_UnlockRect (texSelect->texture);

//	IDirect3DTexture8_GetLevelDesc(d3d_TMUs[d3d_CurrentTMU].boundtexture->teximg, level, &desc);

	// Lock the texture
	IDirect3DTexture8_LockRect (d3d_TMUs[d3d_CurrentTMU].boundtexture->teximg, level, &lr, NULL, 0);

    // XBox textures need to be swizzled
    XGSwizzleRect(
                  lrunswiz.pBits,		  // pSource, 
		          lrunswiz.Pitch,		  // Pitch,
                  &rect,				  // pRect,
                  lr.pBits,				  // pDest,
				  /*desc.Width*/256,	  // Width,			// FIXME: Don't hardcode these sizes!
				  /*desc.Height*/256,	  // Height,		//		  It's ok for now tho as all PSX 
                  &point,				  // pPoint,		//		  textures are 256 and we need the speed!
                  dstbytes );             // BytesPerPixel

	IDirect3DTexture8_UnlockRect (d3d_TMUs[d3d_CurrentTMU].boundtexture->teximg, level);
}
#endif

void glBindTexture (GLenum target, GLuint texture)
{
	d3d_texture_t *tex;

	if(g_iCurrentTextureID == texture) // Saves speed not binding the same texture
		return;

	if (target != GL_TEXTURE_2D)
		return;

	// Use no texture
	if (texture == 0)
	{
		d3d_TMUs[d3d_CurrentTMU].boundtexture = NULL;
		return;
	}

	// Find the texture in the hashmap
	tex = HMLookupTexture(texture);

	if (tex->glnum == texture)
	{
		d3d_TMUs[d3d_CurrentTMU].boundtexture = tex;
		g_iCurrentTextureID = texture;
	}
	
	// Did we find it?
	if (!d3d_TMUs[d3d_CurrentTMU].boundtexture)
	{
		// Nope, so fill in a new one (this will make it work with texture_extension_number)
		// (i don't know if the spec formally allows this but id seem to have gotten away with it...)
		d3d_TMUs[d3d_CurrentTMU].boundtexture = D3D_AllocTexture(texture);

		// Reserve this slot
		d3d_TMUs[d3d_CurrentTMU].boundtexture->glnum = texture;

		// Ensure that it won't be reused
		if (texture > d3d_TextureExtensionNumber) d3d_TextureExtensionNumber = texture;
	}

	// This should never happen
	if (!d3d_TMUs[d3d_CurrentTMU].boundtexture) SysMessage ("glBindTexture: out of textures!!!");

	// Dirty the params
	d3d_TMUs[d3d_CurrentTMU].texparamdirty = TRUE;
}


void glGenTextures (GLsizei n, GLuint *textures)
{
	int i;

	for (i = 0; i < n; i++)
	{
		// either take a free slot or alloc a new one
		d3d_texture_t *tex = D3D_AllocTexture(d3d_TextureExtensionNumber);
		tex->glnum = textures[i] = d3d_TextureExtensionNumber;

		d3d_TextureExtensionNumber++;
	}
}


void glDeleteTextures (GLsizei n, const GLuint *textures)
{
	int i;
	d3d_texture_t *pTex;

	for (i = 0; i < n; i++)
	{
		pTex = HMLookupTexture(textures[i]);
		
		if(pTex)
		{
			if (pTex->glnum == textures[i])
			{
				DeleteSubImageCache(pTex->glnum);

				// Release the texture
				if(pTex->teximg)
				{
					IDirect3DTexture8_Release(pTex->teximg);
					pTex->teximg = NULL;
				}  
				
				// Remove from the hashmap
				HMRemoveTexture(pTex->glnum);
				
				// Free it now
				free(pTex);
			}
		}
	}
}


/*
===================================================================================================================

			VIDEO SETUP

===================================================================================================================
*/

/*
==================
D3D_GetDepthFormat

Gets a valid depth format for a given adapter format
==================
*/
D3DFORMAT D3D_GetDepthFormat (D3DFORMAT AdapterFormat)
{
	// valid depth formats
	int i;
	D3DFORMAT d3d_DepthFormats[] = {/*D3DFMT_D32, D3DFMT_D24X8,*/ D3DFMT_D24S8, /*D3DFMT_D24X4S4,*/ D3DFMT_D16, D3DFMT_UNKNOWN};

	if (d3d_RequestStencil)
	{
		// switch to formats with a stencil buffer available at the head of the list
		d3d_DepthFormats[0] = D3DFMT_D24S8;
//		d3d_DepthFormats[1] = D3DFMT_D24X4S4;
//		d3d_DepthFormats[2] = D3DFMT_D32;
//		d3d_DepthFormats[3] = D3DFMT_D24X8;
		d3d_DepthFormats[4] = D3DFMT_D16;
		d3d_DepthFormats[5] = D3DFMT_UNKNOWN;
	}

	for (i = 0; ; i++)
	{
		if (d3d_DepthFormats[i] == D3DFMT_UNKNOWN)
			break;

		hr = IDirect3D8_CheckDeviceFormat
		(
			d3d_Object,
			D3DADAPTER_DEFAULT,
			D3DDEVTYPE_HAL,
			AdapterFormat,
			D3DUSAGE_DEPTHSTENCIL,
			D3DRTYPE_SURFACE,
			d3d_DepthFormats[i]
		);

		// return the first format that succeeds
		if (SUCCEEDED (hr)) return d3d_DepthFormats[i];
	}

	// didn't get one
	return D3DFMT_UNKNOWN;
}


/*
========================
D3D_GetAdapterModeFormat

returns a usable adapter mode for the given width, height and bpp
========================
*/
D3DFORMAT D3D_GetAdapterModeFormat (int width, int height, int bpp)
{
	int i;

	// fill these in depending on bpp
	D3DFORMAT d3d_Formats[4];

	// these are the orders in which we prefer our formats
	if (bpp == -1)
	{
		// unspecified bpp uses the desktop mode format
		d3d_Formats[0] = d3d_DesktopMode.Format;
		d3d_Formats[1] = D3DFMT_UNKNOWN;
	}
	else if (bpp == 16)
	{
		d3d_Formats[0] = D3DFMT_R5G6B5;
		d3d_Formats[1] = D3DFMT_X1R5G5B5;
		d3d_Formats[2] = D3DFMT_A1R5G5B5;
		d3d_Formats[3] = D3DFMT_UNKNOWN;
	}
	else
	{
		d3d_Formats[0] = D3DFMT_X8R8G8B8;
		d3d_Formats[1] = D3DFMT_A8R8G8B8;
		d3d_Formats[2] = D3DFMT_UNKNOWN;
	}

	for (i = 0; ; i++)
	{
		UINT modecount;
		UINT m;

		// no more modes
		if (d3d_Formats[i] == D3DFMT_UNKNOWN) break;

		// get and validate the number of modes for this format; we expect that this will succeed first time
		modecount = IDirect3D8_GetAdapterModeCount (d3d_Object, D3DADAPTER_DEFAULT);
		if (!modecount) continue;

		// check each mode in turn to find a match
		for (m = 0; m < modecount; m++)
		{
			// get this mode
			D3DDISPLAYMODE mode;
			hr = IDirect3D8_EnumAdapterModes (d3d_Object, D3DADAPTER_DEFAULT, m, &mode);

			// should never happen
			if (FAILED (hr)) continue;

			// d3d8 doesn't specify a format when enumerating so we need to restrict this to the correct format
			if (mode.Format != d3d_Formats[i]);

			// ensure that we can get a depth buffer
			if (D3D_GetDepthFormat (d3d_Formats[i]) == D3DFMT_UNKNOWN) continue;

			// ensure that the texture formats we want to create exist
			if (!D3D_CheckTextureFormat (D3DFMT_L8, d3d_Formats[i])) continue;
			if (!D3D_CheckTextureFormat (D3DFMT_X8R8G8B8, d3d_Formats[i])) continue;
			if (!D3D_CheckTextureFormat (D3DFMT_A8R8G8B8, d3d_Formats[i])) continue;

			// check it against the requested mode
			if (mode.Width == width && mode.Height == height)
			{
				// copy it out and return the mode we got
				memcpy (&d3d_CurrentMode, &mode, sizeof (D3DDISPLAYMODE));
				return mode.Format;
			}
		}
	}

	// didn't find a format
	return D3DFMT_UNKNOWN;
}


BOOL WINAPI SetPixelFormat (HDC hdc, int format, CONST /*PIXELFORMATDESCRIPTOR*/int * ppfd)
{
   /*
	if (ppfd->cStencilBits)
		d3d_RequestStencil = TRUE;
	else*/
		d3d_RequestStencil = FALSE;

	// just silently pass the PFD through unmodified
	return TRUE;
}


void D3D_SetupPresentParams (int width, int height, int bpp, BOOL windowed)
{
	DWORD videoFlags = XGetVideoFlags();

	D3DFORMAT fmt;
	// clear present params to NULL
	memset (&d3d_PresentParams, 0, sizeof (D3DPRESENT_PARAMETERS));

#ifndef _XBOX
	// popup windows are fullscreen always
	if (windowed)
	{
		// defaults for windowed mode - also need to store out clientrect.right and clientrect.bottom
		// (d3d_BPP is only used for fullscreen modes and is retrieved from our CDS override)
		d3d_CurrentMode.Format = d3d_DesktopMode.Format;
		d3d_CurrentMode.Width = width;
		d3d_CurrentMode.Height = height;
		d3d_CurrentMode.RefreshRate = 0;
	}
	else
#endif
	{
		// also fills in d3d_CurrentMode
		fmt = d3d_CurrentMode.Format = D3DFMT_A8R8G8B8;//D3D_GetAdapterModeFormat (width, height, d3d_BPP);

		// ensure that we got a good format
		if (fmt == D3DFMT_UNKNOWN)
			SysMessage ("failed to get fullscreen mode");
	}

	// fill in mode-dependent stuff
	d3d_PresentParams.BackBufferFormat = fmt;//d3d_CurrentMode.Format;
	d3d_PresentParams.FullScreen_RefreshRateInHz = d3d_CurrentMode.RefreshRate = 60;//d3d_CurrentMode.RefreshRate;
	d3d_PresentParams.Windowed = windowed;

	// request 1 backbuffer
	d3d_PresentParams.BackBufferCount = 1;
	d3d_PresentParams.BackBufferWidth = d3d_CurrentMode.Width = width;
	d3d_PresentParams.BackBufferHeight = d3d_CurrentMode.Height = height;

	d3d_PresentParams.EnableAutoDepthStencil = TRUE;
	d3d_PresentParams.AutoDepthStencilFormat = D3DFMT_D16;//D3D_GetDepthFormat (d3d_CurrentMode.Format);
	d3d_PresentParams.MultiSampleType = D3DMULTISAMPLE_4_SAMPLES_MULTISAMPLE_LINEAR;//D3DMULTISAMPLE_NONE;
	d3d_PresentParams.SwapEffect = D3DSWAPEFFECT_DISCARD;
	//d3d_PresentParams.hDeviceWindow = d3d_Window;

	// Now check if player is a PAL 60 user
	if(XGetVideoStandard() == XC_VIDEO_STANDARD_PAL_I)
	{
		if(videoFlags & XC_VIDEO_FLAGS_PAL_60Hz) // PAL 60 user
			d3d_PresentParams.FullScreen_RefreshRateInHz = 60;
		else
			d3d_PresentParams.FullScreen_RefreshRateInHz = 50;
	}
	
	// Use progressive mode if possible
	if(g_bHDEnabled)
	{
		if(videoFlags & XC_VIDEO_FLAGS_HDTV_1080i && width == 1920 && height == 1080)
		{
			// Out of memory very likely!
			d3d_PresentParams.Flags = D3DPRESENTFLAG_INTERLACED | D3DPRESENTFLAG_WIDESCREEN;
		} 
		else if(videoFlags & XC_VIDEO_FLAGS_HDTV_720p && width == 1280 && height == 720)
		{
			d3d_PresentParams.Flags = D3DPRESENTFLAG_PROGRESSIVE | D3DPRESENTFLAG_WIDESCREEN;
		}
		else if(videoFlags & XC_VIDEO_FLAGS_HDTV_480p && width == 640 && height == 480)
		{
			d3d_PresentParams.Flags = D3DPRESENTFLAG_PROGRESSIVE;
		}
		else if(videoFlags & XC_VIDEO_FLAGS_HDTV_480p)
		{
			// Force valid resolution and at least try progressive mode
			width = 640;
			height = 480;
			d3d_PresentParams.Flags = D3DPRESENTFLAG_PROGRESSIVE;
		}
	}
	else // No component cables detected
	{
		if(videoFlags & XC_VIDEO_FLAGS_HDTV_480p)
			d3d_PresentParams.Flags = D3DPRESENTFLAG_PROGRESSIVE;
		else
			d3d_PresentParams.Flags = D3DPRESENTFLAG_INTERLACED;

		d3d_PresentParams.BackBufferWidth = width = 640;
		d3d_PresentParams.BackBufferHeight = height = 480;
	}
}


BOOL WINAPI wglMakeCurrent (HDC hdc, HGLRC hglrc)
{
#ifndef _XBOX
	RECT clientrect;
	LONG winstyle;

	// if we're making NULL current we just return TRUE

	if (!hdc || !hglrc)
	{
		// delete the device, the object and all other objects
		// this is required because engines may decide to destroy the window which is not good for D3D
		wglDeleteContext (hglrc);
		return TRUE;
	}
#endif   

	// if there is no D3D Object we must return FALSE
	// d3d_Object is created in wglCreateContext so this behaviour will enforce wglCreateContext to be called before wglMakeCurrent
	// note that this will break in sitauions where there is more than one active context and we're switching between them...
	if (!d3d_Object) return FALSE;

	// if a device was previously set up, force a subsequent request to MakeCurrent to fail
	if (d3d_Device) return FALSE;

#ifndef _XBOX
	// we can't extern mainwindow as it may be called something else depending on the codebase used
	if (!(d3d_Window = WindowFromDC (hdc))) SysMessage ("wglCreateContext: could not determine application window");

	// get the dimensions of the window
	GetClientRect (d3d_Window, &clientrect);

	// see are we fullscreen
	winstyle = GetWindowLong (d3d_Window, GWL_STYLE);

	// get the format for the desktop mode
	if (FAILED (hr = IDirect3D8_GetAdapterDisplayMode (d3d_Object, D3DADAPTER_DEFAULT, &d3d_DesktopMode)))
		SysMessage ("Failed to get desktop mode");
#endif

	if (FAILED (hr = IDirect3D8_GetDeviceCaps (d3d_Object, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &d3d_Caps)))
		SysMessage ("failed to get object caps");

	// setup our present parameters (popup windows are fullscreen always)
	//D3D_SetupPresentParams (clientrect.right, clientrect.bottom, d3d_BPP, !(winstyle & WS_POPUP));
	D3D_SetupPresentParams(g_iWidth, g_iHeight, 32, FALSE);

	// here we use D3DCREATE_FPU_PRESERVE to maintain the resolution of Quake's timers (this is a serious problem)
	// and D3DCREATE_DISABLE_DRIVER_MANAGEMENT to protect us from rogue drivers (call it honest paranoia).  first
	// we attempt to create a hardware vp device.
	// --------------------------------------------------------------------------------------------------------
	// NOTE re pure devices: we intentionally DON'T request a pure device, EVEN if one is available, as we need
	// to support certain glGet functions that would be broken if we used one.
	// --------------------------------------------------------------------------------------------------------
	// NOTE re D3DCREATE_FPU_PRESERVE - this can be avoided if we use a timer that's not subject to FPU drift,
	// such as timeGetTime (with timeBeginTime (1)); by default Quake's times *ARE* prone to FPU drift as they
	// use doubles for storing the last time, which gradually creeps up to be nearer to the current time each
	// frame.  Not using doubles for the stored times (i.e. switching them all to floats) would also help here.
	hr = IDirect3D8_CreateDevice
	(
		d3d_Object,
		D3DADAPTER_DEFAULT,
		D3DDEVTYPE_HAL,
		NULL,//d3d_Window,
		D3DCREATE_HARDWARE_VERTEXPROCESSING /*| D3DCREATE_FPU_PRESERVE | D3DCREATE_DISABLE_DRIVER_MANAGEMENT*/,
		&d3d_PresentParams,
		&d3d_Device
	);

	if (FAILED (hr))
	{
		// it's OK, we may not have hardware vp available, so create a software vp device
		hr = IDirect3D8_CreateDevice
		(
			d3d_Object,
			D3DADAPTER_DEFAULT,
			D3DDEVTYPE_HAL,
			NULL,//d3d_Window,
			D3DCREATE_SOFTWARE_VERTEXPROCESSING /*| D3DCREATE_FPU_PRESERVE | D3DCREATE_DISABLE_DRIVER_MANAGEMENT*/,
			&d3d_PresentParams,
			&d3d_Device
		);

		// oh shit
		if (hr == D3DERR_INVALIDCALL)
			SysMessage ("IDirect3D8_CreateDevice with D3DERR_INVALIDCALL");
		else if (hr == D3DERR_NOTAVAILABLE)
			SysMessage ("IDirect3D8_CreateDevice with D3DERR_NOTAVAILABLE");
		else if (hr == D3DERR_OUTOFVIDEOMEMORY)
			SysMessage ("IDirect3D8_CreateDevice with D3DERR_INVALIDCALL");
		else if (FAILED (hr)) SysMessage ("failed to create Direct3D device");
	}

	if (d3d_Device == NULL) SysMessage ("created NULL Direct3D device");

	SetGUID3DDevice(d3d_Device, d3d_PresentParams);

	g_bScissorTest = FALSE;

	HMRemoveAllTextures();

	// initialize state
	D3D_InitStates ();

	// disable lighting
	D3D_SetRenderState (D3DRS_LIGHTING, FALSE);

	// default zbias
	// D3D uses values of 0 to 16, with 0 being the default and higher values coming nearer
	// because we want to push out further too, we pick an intermediate value as our new default
	D3D_SetRenderState (D3DRS_ZBIAS, 0);

	// fd perf test
	_controlfp(_PC_24,_MCW_PC);
	D3D_SetRenderState(D3DRS_SWATHWIDTH, D3DSWATH_8);

	// Apply visual improvements
	IDirect3DDevice8_SetFlickerFilter(d3d_Device, 1);
	IDirect3DDevice8_SetSoftDisplayFilter(d3d_Device, TRUE);

	// set projection and world to dirty, beginning of stack and identity
	D3D_InitializeMatrix (&d3d_ModelViewMatrix);
	D3D_InitializeMatrix (&d3d_ProjectionMatrix);

	// clear the device to black on creation
	IDirect3DDevice8_Clear(d3d_Device, 0, NULL, D3DCLEAR_TARGET, 0x00000000, 1, 1);

	return TRUE;
}


HGLRC WINAPI wglCreateContext (HDC hdc)
{
	// initialize direct3d and get object capabilities
	if (!(d3d_Object = Direct3DCreate8 (D3D_SDK_VERSION)))
	{
		SysMessage ("Failed to create Direct3D Object");
		return (HGLRC)0;
	}

	HMCreateTable();

	return (HGLRC)1;
}


BOOL WINAPI wglDeleteContext (HGLRC hglrc)
{
	// Release all textures
	HMRemoveAllTextures();

	HMDeleteTable();

	// Release device and object
	if(d3d_Device)
		IDirect3DDevice8_Release(d3d_Device);

	if(d3d_Object)
		IDirect3D8_Release(d3d_Object);

	// Success
	return TRUE;
}


HGLRC WINAPI wglGetCurrentContext (VOID)
{
	return (HGLRC)1;
}


HDC WINAPI wglGetCurrentDC (VOID)
{
	return 0;
}


/*
===================================================================================================================

			COMPARISON TEST AND FUNCTION

	Just DepthFunc and AlphaFunc really

===================================================================================================================
*/

void GL_SetCompFunc (DWORD mode, GLenum func)
{
	switch (func)
	{
	case GL_NEVER:
		D3D_SetRenderState ((D3DRENDERSTATETYPE)mode, D3DCMP_NEVER);
		break;

	case GL_LESS:
		D3D_SetRenderState ((D3DRENDERSTATETYPE)mode, D3DCMP_LESS);
		break;

	case GL_LEQUAL:
		D3D_SetRenderState ((D3DRENDERSTATETYPE)mode, D3DCMP_LESSEQUAL);
		break;

	case GL_EQUAL:
		D3D_SetRenderState ((D3DRENDERSTATETYPE)mode, D3DCMP_EQUAL);
		break;

	case GL_GREATER:
		D3D_SetRenderState ((D3DRENDERSTATETYPE)mode, D3DCMP_GREATER);
		break;

	case GL_NOTEQUAL:
		D3D_SetRenderState ((D3DRENDERSTATETYPE)mode, D3DCMP_NOTEQUAL);
		break;

	case GL_GEQUAL:
		D3D_SetRenderState ((D3DRENDERSTATETYPE)mode, D3DCMP_GREATEREQUAL);
		break;

	case GL_ALWAYS:
	default:
		D3D_SetRenderState ((D3DRENDERSTATETYPE)mode, D3DCMP_ALWAYS);
		break;
	}
}


void glDepthFunc (GLenum func)
{
	GL_SetCompFunc (D3DRS_ZFUNC, func);
}


void glAlphaFunc (GLenum func, GLclampf ref)
{
	GL_SetCompFunc (D3DRS_ALPHAFUNC, func);
	D3D_SetRenderState (D3DRS_ALPHAREF, BYTE_CLAMP ((int) (ref * 255)));
}


/*
===================================================================================================================

			FRAMEBUFFER

===================================================================================================================
*/


void glColorMask (GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
	DWORD mask = 0;

	if (red) mask |= D3DCOLORWRITEENABLE_RED;
	if (green) mask |= D3DCOLORWRITEENABLE_GREEN;
	if (blue) mask |= D3DCOLORWRITEENABLE_BLUE;
	if (alpha) mask |= D3DCOLORWRITEENABLE_ALPHA;

	D3D_SetRenderState (D3DRS_COLORWRITEENABLE, mask);
}


void glDepthRange (GLclampd zNear, GLclampd zFar)
{
	// update the viewport
	d3d_Viewport.MinZ = zNear;
	d3d_Viewport.MaxZ = zFar;

	IDirect3DDevice8_SetViewport (d3d_Device, &d3d_Viewport);
	D3D_DirtyAllStates ();
}


void glDepthMask (GLboolean flag)
{
	// if only they were all so easy...
	D3D_SetRenderState (D3DRS_ZWRITEENABLE, flag == GL_TRUE ? TRUE : FALSE);
}


void GL_Blend (D3DRENDERSTATETYPE rs, GLenum factor)
{
	D3DBLEND blend = D3DBLEND_ONE;

	switch (factor)
	{
	case GL_ZERO:
		blend = D3DBLEND_ZERO;
		break;

	case GL_ONE:
		blend = D3DBLEND_ONE;
		break;

	case GL_SRC_COLOR:
		blend = D3DBLEND_SRCCOLOR;
		break;

	case GL_ONE_MINUS_SRC_COLOR:
		blend = D3DBLEND_INVSRCCOLOR;
		break;

	case GL_DST_COLOR:
		blend = D3DBLEND_DESTCOLOR;
		break;

	case GL_ONE_MINUS_DST_COLOR:
		blend = D3DBLEND_INVDESTCOLOR;
		break;

	case GL_SRC_ALPHA:
		blend = D3DBLEND_SRCALPHA;
		break;

	case GL_ONE_MINUS_SRC_ALPHA:
		blend = D3DBLEND_INVSRCALPHA;
		break;

	case GL_DST_ALPHA:
		blend = D3DBLEND_DESTALPHA;
		break;

	case GL_ONE_MINUS_DST_ALPHA:
		blend = D3DBLEND_INVDESTALPHA;
		break;

	case GL_SRC_ALPHA_SATURATE:
		blend = D3DBLEND_SRCALPHASAT;
		break;

	default:
		SysMessage ("glBlendFunc: unknown factor");
	}

	D3D_SetRenderState (rs, blend);
}


void glBlendFunc (GLenum sfactor, GLenum dfactor)
{
	GL_Blend (D3DRS_SRCBLEND, sfactor);
	GL_Blend (D3DRS_DESTBLEND, dfactor);
}


void glClear (GLbitfield mask)
{
	DWORD clearflags = 0;

	// no accumulation buffer in d3d
	if (mask & GL_COLOR_BUFFER_BIT) clearflags |= D3DCLEAR_TARGET;
	if (mask & GL_DEPTH_BUFFER_BIT) clearflags |= D3DCLEAR_ZBUFFER;
	if (mask & GL_STENCIL_BUFFER_BIT) clearflags |= D3DCLEAR_STENCIL;

	// back to normal operation
	IDirect3DDevice8_Clear (d3d_Device, 0, NULL, clearflags, d3d_ClearColor, 1, 1);
}


void glClearColor (GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
	d3d_ClearColor = GL_ColorToD3D (red, green, blue, alpha);
}


void glFinish (void)
{
	// we force a Present in our SwapBuffers function so this is unneeded
}


void glReadBuffer (GLenum mode)
{
	// d3d doesn't like us messing with the front buffer, which is all glquake uses this for, so here
	// we just silently ignore requests to change the draw buffer
}


void glReadPixels (GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels)
{
	int row;
	int col;
	unsigned char *srcdata;
	unsigned char *dstdata = (unsigned char *) pixels;
	LPDIRECT3DSURFACE8 bbsurf;
	LPDIRECT3DSURFACE8 locksurf;
	D3DLOCKED_RECT lockrect;
	D3DSURFACE_DESC desc;

	if (format != GL_RGB || type != GL_UNSIGNED_BYTE)
	{
		SysMessage ("glReadPixels: invalid format or type\n");
		return;
	}

	hr = IDirect3DDevice8_GetBackBuffer (d3d_Device, 0, D3DBACKBUFFER_TYPE_MONO, &bbsurf);

	if (FAILED (hr) || !bbsurf)
	{
		SysMessage ("glReadPixels: failed to access back buffer\n");
		return;
	}

	// because we don't have a lockable backbuffer we instead copy it off to an image surface
	// this will also handle translation between different backbuffer formats
	hr = IDirect3DSurface8_GetDesc (bbsurf, &desc);
	hr = IDirect3DDevice8_CreateImageSurface (d3d_Device, desc.Width, desc.Height, /*D3DFMT_R8G8B8*/D3DFMT_LIN_X8R8G8B8, &locksurf); //CHECKME: Linear/Compressed format ?
	hr = D3DXLoadSurfaceFromSurface (locksurf, NULL, NULL, bbsurf, NULL, NULL, D3DX_FILTER_NONE, 0);

	// now we have a surface we can lock
	hr = IDirect3DSurface8_LockRect (locksurf, &lockrect, NULL, D3DLOCK_READONLY);
	srcdata = (unsigned char *) lockrect.pBits;

	for (row = y; row < (y + height); row++)
	{
		for (col = x; col < (x + width); col++)
		{
			int srcpos = row * width + col;

			dstdata[0] = srcdata[srcpos * 4 + 0];
			dstdata[1] = srcdata[srcpos * 4 + 1];
			dstdata[2] = srcdata[srcpos * 4 + 2];

			dstdata += 3;
		}
	}

	hr = IDirect3DSurface8_UnlockRect (locksurf);
//	D3D_SAFE_RELEASE (locksurf);
    IDirect3DSurface8_Release(locksurf);
//	D3D_SAFE_RELEASE (bbsurf);
    IDirect3DSurface8_Release(bbsurf);
}


void glViewport (GLint x, GLint y, GLsizei width, GLsizei height)
{
	// translate from OpenGL bottom-left to D3D top-left
	y = d3d_CurrentMode.Height - (height + y);

	// always take the full depth range
	d3d_Viewport.X = x;
	d3d_Viewport.Y = y;
	d3d_Viewport.Width = width;
	d3d_Viewport.Height = height;
	d3d_Viewport.MinZ = 0;
	d3d_Viewport.MaxZ = 1;

	IDirect3DDevice8_SetViewport (d3d_Device, &d3d_Viewport);

	// dirty all states
	D3D_DirtyAllStates ();
}


void D3D_PostResetRestore (void)
{
	// set projection and world to dirty, beginning of stack and identity
	D3D_InitializeMatrix (&d3d_ModelViewMatrix);
	D3D_InitializeMatrix (&d3d_ProjectionMatrix);

	// force all states back to the way they were
	D3D_SetRenderStates ();
	D3D_SetTextureStates ();

	// set viewport to invalid
	d3d_Viewport.Width = -1;
	d3d_Viewport.Height = -1;
}

void D3D_SetMode(int iWidth, int iHeight, BOOL bHDEnabled)
{
	g_iWidth = iWidth;
	g_iHeight = iHeight;
	g_bHDEnabled = bHDEnabled;
}

void D3D_ResetMode (int width, int height, int bpp, BOOL windowed)
{
#ifndef _XBOX   
	RECT winrect;
	int winstyle;
	int winexstyle;

	while (1)
	{
		// here we get the current status of the device
		hr = IDirect3DDevice8_TestCooperativeLevel (d3d_Device);

		if (hr == D3D_OK) break;

		Sleep (10);
	}

	// reset present params
	D3D_SetupPresentParams (width, height, bpp, windowed);

	// reset device
	IDirect3DDevice8_Reset (d3d_Device, &d3d_PresentParams);

	while (1)
	{
		// here we get the current status of the device
		hr = IDirect3DDevice8_TestCooperativeLevel (d3d_Device);

		if (hr == D3D_OK) break;

		Sleep (10);
	}

	winrect.left = 0;
	winrect.right = width;
	winrect.top = 0;
	winrect.bottom = height;

	winexstyle = 0;
	winstyle = windowed ? WS_OVERLAPPED | WS_BORDER | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX : WS_POPUP;

	// reset stuff
	SetWindowLong (d3d_PresentParams.hDeviceWindow, GWL_EXSTYLE, winexstyle);
	SetWindowLong (d3d_PresentParams.hDeviceWindow, GWL_STYLE, winstyle);
	AdjustWindowRectEx (&winrect, winstyle, FALSE, 0);

	// repaint all windows
	InvalidateRect (NULL, NULL, TRUE);

	// resize the window; also reposition it as we don't know if it's going to go down below the taskbar area
	// or not, so we just err on the side of caution here.
	SetWindowPos
	(
		d3d_PresentParams.hDeviceWindow,
		NULL,
		windowed ? (d3d_DesktopMode.Width - (winrect.right - winrect.left)) / 2 : 0,
		windowed ? (d3d_DesktopMode.Height - (winrect.bottom - winrect.top)) / 2 : 0,
		winrect.right - winrect.left,
		winrect.bottom - winrect.top,
		SWP_NOOWNERZORDER | SWP_NOREPOSITION | SWP_NOZORDER | SWP_SHOWWINDOW
	);

	// ensure
	SetForegroundWindow (d3d_PresentParams.hDeviceWindow);

	// clean up states/etc
	D3D_PostResetRestore ();

	// because a lot of things are now bouncing around the system we take a breather
	Sleep (500);

	// give some user feedback here
	SysMessage ("Set %s mode %ix%ix%i\n", windowed ? "windowed" : "fullscreen", width, height, bpp);
#endif   
}


int FakeSwapBuffers (void)
{
#ifndef _XBOX // We don't need this on XBox
	// if we lost the device (e.g. on a mode switch, alt-tab, etc) we must try to recover it
	if (d3d_DeviceLost)
	{
		// here we get the current status of the device
		hr = IDirect3DDevice8_TestCooperativeLevel (d3d_Device);

		switch (hr)
		{
		case D3D_OK:
			// device is recovered
			d3d_DeviceLost = FALSE;
			D3D_PostResetRestore ();
			break;

		case D3DERR_DEVICELOST:
			// device is still lost
			break;

		case D3DERR_DEVICENOTRESET:
			// device is ready to be reset
			IDirect3DDevice8_Reset (d3d_Device, &d3d_PresentParams);
			break;

		default:
			break;
		}

		// yield the CPU a little
		Sleep (10);

		// don't bother this frame
		return;
	}
#endif
	if (d3d_SceneBegun)
	{
#if 0 // Disabled atm to try save as much CPU time as possible
		// see do we need to reset the viewport as D3D restricts present to the client viewport
		// this is needed as some engines (hello FitzQuake) set multiple smaller viewports for
		// different on-screen elements (e.g. menus)
		BOOL vpreset = FALSE;

		// test dimensions for a change; we don't just blindly reset it as this may not happen every frame
		if (d3d_Viewport.X != 0) vpreset = TRUE;
		if (d3d_Viewport.Y != 0) vpreset = TRUE;
		if (d3d_Viewport.Width != d3d_CurrentMode.Width) vpreset = TRUE;
		if (d3d_Viewport.Height != d3d_CurrentMode.Height) vpreset = TRUE;

		if (vpreset)
		{
			// now reset it to full window dimensions so that the full window will present
			d3d_Viewport.X = 0;
			d3d_Viewport.Y = 0;
			d3d_Viewport.Width = d3d_CurrentMode.Width;
			d3d_Viewport.Height = d3d_CurrentMode.Height;
			IDirect3DDevice8_SetViewport (d3d_Device, &d3d_Viewport);
		}
#endif
		// endscene and present are only required if a scene was begun (i.e. if something was actually drawn)
#ifndef _XBOX // Not used on Xbox
		IDirect3DDevice8_EndScene (d3d_Device);
#endif
		d3d_SceneBegun = FALSE;

		// present the display
		/*hr = */IDirect3DDevice8_Present (d3d_Device, NULL, NULL, NULL, NULL);

#ifndef _XBOX
		if (hr == D3DERR_DEVICELOST)
		{
			// flag a lost device
			d3d_DeviceLost = TRUE;
		}
		else if (FAILED (hr))
		{
			// something else bad happened
			SysMessage ("FAILED (hr) on IDirect3DDevice8_Present");
			return FALSE;
		}
#endif
		return TRUE;
	}
	return FALSE;
}


/*
===================================================================================================================

			OTHER

===================================================================================================================
*/

void GL_UpdateCull (void)
{
	if (gl_CullEnable == GL_FALSE)
	{
		// disable culling
		D3D_SetRenderState (D3DRS_CULLMODE, D3DCULL_NONE);
		return;
	}

	if (gl_FrontFace == GL_CCW)
	{
		if (gl_CullMode == GL_BACK)
			D3D_SetRenderState (D3DRS_CULLMODE, D3DCULL_CW);
		else if (gl_CullMode == GL_FRONT)
			D3D_SetRenderState (D3DRS_CULLMODE, D3DCULL_CCW);
		else if (gl_CullMode == GL_FRONT_AND_BACK)
			; // do nothing; we cull in software in GL_SubmitVertexes instead
		else SysMessage ("GL_UpdateCull: illegal glCullFace");
	}
	else if (gl_FrontFace == GL_CW)
	{
		if (gl_CullMode == GL_BACK)
			D3D_SetRenderState (D3DRS_CULLMODE, D3DCULL_CCW);
		else if (gl_CullMode == GL_FRONT)
			D3D_SetRenderState (D3DRS_CULLMODE, D3DCULL_CW);
		else if (gl_CullMode == GL_FRONT_AND_BACK)
			; // do nothing; we cull in software in GL_SubmitVertexes instead
		else SysMessage ("GL_UpdateCull: illegal glCullFace");
	}
	else SysMessage ("GL_UpdateCull: illegal glFrontFace");
}


void glFrontFace (GLenum mode)
{
	gl_FrontFace = mode;
	GL_UpdateCull ();
}


void glCullFace (GLenum mode)
{
	gl_CullMode = mode;
	GL_UpdateCull ();
}


void glScissor (GLint x, GLint y, GLsizei width, GLsizei height)
{
	// Translate from OpenGL bottom-left to D3D top-left
	y = d3d_CurrentMode.Height - (height + y);
	
	g_ScissorRect.x1 = x;
	g_ScissorRect.y1 = y;

	g_ScissorRect.x2 = x + width;
	g_ScissorRect.y2 = y + height;
}


void GL_EnableDisable (GLenum cap, BOOL enabled)
{
	DWORD d3d_RS;

	switch (cap)
	{
	case GL_SCISSOR_TEST:
		g_bScissorTest = enabled;
		return;

	case GL_BLEND:
		d3d_RS = D3DRS_ALPHABLENDENABLE;
		break;

	case GL_ALPHA_TEST:
		d3d_RS = D3DRS_ALPHATESTENABLE;
		break;

	case GL_TEXTURE_2D:
		// this is a texture stage state rather than a render state
		if (enabled)
			d3d_TMUs[d3d_CurrentTMU].enabled = TRUE;
		else d3d_TMUs[d3d_CurrentTMU].enabled = FALSE;

		d3d_TMUs[d3d_CurrentTMU].texenvdirty = TRUE;
		d3d_TMUs[d3d_CurrentTMU].texparamdirty = TRUE;

		// we're not setting state yet here...
		return;

	case GL_CULL_FACE:
		gl_CullEnable = enabled ? GL_TRUE : GL_FALSE;
		GL_UpdateCull ();

		// we're not setting state yet here...
		return;

	case GL_FOG:
		d3d_RS = D3DRS_FOGENABLE;
		break;

	case GL_DEPTH_TEST:
		d3d_RS = D3DRS_ZENABLE;
		if (enabled) enabled = D3DZB_TRUE; else enabled = D3DZB_FALSE;
		break;

	case GL_POLYGON_OFFSET_FILL:
	case GL_POLYGON_OFFSET_LINE:
		d3d_PolyOffsetEnabled = enabled;
		d3d_PolyOffsetSwitched = FALSE;

		// we're not setting state yet here...
		return;

	default:
		return;
	}

	D3D_SetRenderState ((D3DRENDERSTATETYPE)d3d_RS, enabled);
}


void glDisable (GLenum cap)
{
	GL_EnableDisable (cap, FALSE);
}


void glEnable (GLenum cap)
{
	GL_EnableDisable (cap, TRUE);
}


void glGetFloatv (GLenum pname, GLfloat *params)
{
	switch (pname)
	{
	case GL_MODELVIEW_MATRIX:
		memcpy (params, d3d_ModelViewMatrix.stack[d3d_ModelViewMatrix.stackdepth].m, g_fSize16);
		break;

	case GLD3D_MAX_TEXTURE_MAX_ANISOTROPY_EXT:
		params[0] = (float) d3d_Caps.MaxAnisotropy;
		break;

	default:
		break;
	}
}


void glPolygonMode (GLenum face, GLenum mode)
{
	// we don't have the ability to specify which side of the poly is filled, so just do it
	// the way that it's specified and hope for the best!
	if (mode == GL_LINE)
		D3D_SetRenderState (D3DRS_FILLMODE, D3DFILL_WIREFRAME);
	else if (mode == GL_POINT)
		D3D_SetRenderState (D3DRS_FILLMODE, D3DFILL_POINT);
	else D3D_SetRenderState (D3DRS_FILLMODE, D3DFILL_SOLID);
}


void glShadeModel (GLenum mode)
{
	// easy peasy
	D3D_SetRenderState (D3DRS_SHADEMODE, mode == GL_FLAT ? D3DSHADE_FLAT : D3DSHADE_GOURAUD);
}


void glGetIntegerv (GLenum pname, GLint *params)
{
	// here we only bother getting the values that glquake uses
	switch (pname)
	{
	case GL_MAX_TEXTURE_SIZE:
		// D3D allows both to be different so return the lowest
		params[0] = (d3d_Caps.MaxTextureWidth > d3d_Caps.MaxTextureHeight ? d3d_Caps.MaxTextureHeight : d3d_Caps.MaxTextureWidth);
		break;

	case GL_VIEWPORT:
		params[0] = d3d_Viewport.X;
		params[1] = d3d_Viewport.Y;
		params[2] = d3d_Viewport.Width;
		params[3] = d3d_Viewport.Height;
		break;

	case GL_STENCIL_BITS:
		if (d3d_PresentParams.AutoDepthStencilFormat == D3DFMT_D24S8)
			params[0] = 8;
/*		else if (d3d_PresentParams.AutoDepthStencilFormat == D3DFMT_D24X4S4)
			params[0] = 4;*/
		else params[0] = 0;

		break;

	default:
		params[0] = 0;
		return;
	}
}

GLenum glGetError (void) //MARTY - Dummy function
{
	return GL_NO_ERROR;
}

LONG ChangeDisplaySettings (/*LPDEVMODE*/int lpDevMode, DWORD dwflags)
{
#ifndef _XBOX
	if (dwflags & CDS_TEST)
	{
		// if we're testing we want to do it for real
		return ChangeDisplaySettingsA (lpDevMode, dwflags);
	}
	else if (!lpDevMode)
	{
		// this signals a return to the desktop display mode

		// always signal success
		return DISP_CHANGE_SUCCESSFUL;
	}
	else
	{
		// this code path is only used for fullscreen modes that are not the same as the desktop mode
		d3d_BPP = lpDevMode->dmBitsPerPel;

		// always signal success
		return DISP_CHANGE_SUCCESSFUL;
	}
#endif
   return 0;
}


/*
===================================================================================================================

			FOG

===================================================================================================================
*/

void glFogf (GLenum pname, GLfloat param)
{
	switch (pname)
	{
	case GL_FOG_MODE:
		glFogi (pname, param);
		break;

	case GL_FOG_DENSITY:
		D3D_SetRenderState (D3DRS_FOGDENSITY, D3D_FloatToDWORD (param));
		break;

	case GL_FOG_START:
		D3D_SetRenderState (D3DRS_FOGSTART, D3D_FloatToDWORD (param));
		break;

	case GL_FOG_END:
		D3D_SetRenderState (D3DRS_FOGEND, D3D_FloatToDWORD (param));
		break;

	case GL_FOG_COLOR:
		break;

	default:
		break;
	}
}


void glFogfv (GLenum pname, const GLfloat *params)
{
	switch (pname)
	{
	case GL_FOG_COLOR:
		D3D_SetRenderState (D3DRS_FOGCOLOR, GL_ColorToD3D (params[0], params[1], params[2], params[3]));
		break;

	default:
		glFogf (pname, params[0]);
		break;
	}
}


void glFogi (GLenum pname, GLint param)
{
	switch (pname)
	{
	case GL_FOG_MODE:
		// fixme - make these dependent on a glHint...
		if (param == GL_LINEAR)
		{
			if (d3d_Fog_Hint == GL_NICEST)
			{
				D3D_SetRenderState (D3DRS_FOGTABLEMODE, D3DFOG_LINEAR);
//				D3D_SetRenderState (D3DRS_FOGVERTEXMODE, D3DFOG_NONE);
			}
			else
			{
				D3D_SetRenderState (D3DRS_FOGTABLEMODE, D3DFOG_NONE);
//				D3D_SetRenderState (D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
			}
		}
		else if (param == GL_EXP)
		{
			if (d3d_Fog_Hint == GL_NICEST)
			{
				D3D_SetRenderState (D3DRS_FOGTABLEMODE, D3DFOG_EXP);
//				D3D_SetRenderState (D3DRS_FOGVERTEXMODE, D3DFOG_NONE);
			}
			else
			{
				D3D_SetRenderState (D3DRS_FOGTABLEMODE, D3DFOG_NONE);
//				D3D_SetRenderState (D3DRS_FOGVERTEXMODE, D3DFOG_EXP);
			}
		}
		else
		{
			if (d3d_Fog_Hint == GL_NICEST)
			{
				D3D_SetRenderState (D3DRS_FOGTABLEMODE, D3DFOG_EXP2);
//				D3D_SetRenderState (D3DRS_FOGVERTEXMODE, D3DFOG_NONE);
			}
			else
			{
				D3D_SetRenderState (D3DRS_FOGTABLEMODE, D3DFOG_NONE);
//				D3D_SetRenderState (D3DRS_FOGVERTEXMODE, D3DFOG_EXP2);
			}
		}
		break;

	default:
		glFogf (pname, param);
		break;
	}
}


void glFogiv (GLenum pname, const GLint *params)
{
	switch (pname)
	{
	case GL_FOG_COLOR:
		// the spec isn't too clear on how these map to the fp values... oh well...
		D3D_SetRenderState (D3DRS_FOGCOLOR, D3DCOLOR_ARGB (BYTE_CLAMP (params[3]), BYTE_CLAMP (params[0]), BYTE_CLAMP (params[1]), BYTE_CLAMP (params[2])));
		break;

	default:
		glFogi (pname, params[0]);
		break;
	}
}


/*
===================================================================================================================

			MULTITEXTURE

===================================================================================================================
*/

void WINAPI GL_ActiveTexture (GLenum texture)
{
	d3d_CurrentTMU = D3D_TMUForTexture (texture);
}


void WINAPI GL_MultiTexCoord2f (GLenum target, GLfloat s, GLfloat t)
{
	int TMU = D3D_TMUForTexture (target);

	d3d_CurrentTexCoord[TMU].s = s;
	d3d_CurrentTexCoord[TMU].t = t;
}


void WINAPI GL_MultiTexCoord1f (GLenum target, GLfloat s)
{
	int TMU = D3D_TMUForTexture (target);

	d3d_CurrentTexCoord[TMU].s = s;
	d3d_CurrentTexCoord[TMU].t = 0;
}

/*
===================================================================================================================

			Blend Equation Extension

===================================================================================================================
*/

void WINAPI glBlendEquationExtFgl(GLenum mode)
{
	switch(mode)
	{
	case GL_FUNC_ADD_EXT:
 		D3D_SetRenderState (D3DRS_BLENDOP, D3DBLENDOP_ADD);
		break;

	case GL_FUNC_SUBTRACT_EXT:
		D3D_SetRenderState (D3DRS_BLENDOP, D3DBLENDOP_SUBTRACT);
		break;

	case D3DBLENDOP_REVSUBTRACT:
		D3D_SetRenderState (D3DRS_BLENDOP, D3DBLENDOP_REVSUBTRACT);
		break;

	case GL_MIN_EXT:
		D3D_SetRenderState (D3DRS_BLENDOP, D3DBLENDOP_MIN);
		break;

	case GL_MAX_EXT:
		D3D_SetRenderState (D3DRS_BLENDOP, D3DBLENDOP_MAX);
		break;

	default:
		SysMessage ("glBlendEquationExt: unknown mode");
	}
}

/*
===================================================================================================================

			EXTENSIONS

===================================================================================================================
*/

// we advertise ourselves as version 1.1 so that as few assumptions about capability as possible are made
static char *GL_VersionString = "1.1";

// these are the extensions we want to export
// right now GL_ARB_texture_env_combine is implemented but causes substantial perf hitches
// note: we advertise GL_EXT_texture_filter_anisotropic but we also do a double-check with the D3D caps when setting it for real
static char *GL_ExtensionString =
"GL_ARB_multitexture \
GL_ARB_texture_env_add \
GL_EXT_blend_subtract \
GL_EXT_texture_filter_anisotropic ";

D3DADAPTER_IDENTIFIER8 d3d_AdapterID;

const GLubyte *glGetString (GLenum name)
{
	IDirect3D8_GetAdapterIdentifier (d3d_Object, 0, D3DENUM_NO_WHQL_LEVEL, &d3d_AdapterID);

	switch (name)
	{
	case GL_VENDOR:
		return (GLubyte*)d3d_AdapterID.Description;
	case GL_RENDERER:
		return (GLubyte*)d3d_AdapterID.Driver;
	case GL_VERSION:
		return (const GLubyte*)GL_VersionString;
	case GL_EXTENSIONS:

	default:
		return (const GLubyte*)GL_ExtensionString;
	}
}


// type cast unsafe conversion from
#pragma warning (push)
#pragma warning (disable: 4191)

typedef struct gld3d_entrypoint_s
{
	char *funcname;
	PROC funcproc;
} gld3d_entrypoint_t;

gld3d_entrypoint_t d3d_EntryPoints[] =
{
	{"glActiveTexture", (PROC) GL_ActiveTexture},
	{"glActiveTextureARB", (PROC) GL_ActiveTexture},
	{"glBindTexture", (PROC) glBindTexture},
	{"glMultiTexCoord1f", (PROC) GL_MultiTexCoord1f},
	{"glMultiTexCoord1fARB", (PROC) GL_MultiTexCoord1f},
	{"glMultiTexCoord2f", (PROC) GL_MultiTexCoord2f},
	{"glMultiTexCoord2fARB", (PROC) GL_MultiTexCoord2f},
	{"glClientActiveTexture", (PROC) GL_ClientActiveTexture},
	{"glClientActiveTextureARB", (PROC) GL_ClientActiveTexture},
	{"glBlendEquationEXT", (PROC) glBlendEquationExtFgl},
	{NULL, NULL}
};


PROC APIENTRY wglGetProcAddress (LPCSTR strExtension)
{
	int i;

	// check all of our entrypoints
	for (i = 0; ; i++)
	{
		// no more entrypoints
		if (!d3d_EntryPoints[i].funcname) break;

		if (!strcmp (strExtension, d3d_EntryPoints[i].funcname))
		{
			return d3d_EntryPoints[i].funcproc;
		}
	}

	// LocalDebugBreak();
	return NULL;
}

#pragma warning (pop)

#endif //_USEFAKEGLX_09