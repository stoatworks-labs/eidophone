/*
    The five GL entry points emscripten's WebGL2 library cannot take from the
    plugin as they are: glShaderSource, glEnable, glDisable, glIsEnabled and
    glTexImage2D. Every other GL call the plugin and the FFGL SDK make goes
    straight to emscripten's implementation, on the page's own WebGL2 context.
    (woofer's shim, which is mirrorball's, fountain's and polyhedral's, with
    eidophone's names, and bubble's glTexImage2D with eidophone's two formats.)

    glShaderSource
        The plugin hands GL desktop GLSL 4.10 (`#version 410 core`); WebGL2
        compiles GLSL ES 3.00. The page's `eidophoneShaderSource` first
        REQUIRES the text to be byte for byte one of the page's own assemblies
        of the plugin's shaders (demo/shaders.js, which
        demo/tools/check_shaders.py holds to source/Shaders.cpp, and its
        ASSEMBLY table to Assemble(), SplatVertex() and InitGL's quad vertex),
        then applies the demo kit's `port()`: the version line and the ES
        precision defaults, nothing else. A shader the plugin assembled
        differently from the page's copy is refused (an empty source, so the
        plugin's InitGL fails and logs which pass), never compiled quietly.

    glEnable / glDisable / glIsEnabled
        GL_PROGRAM_POINT_SIZE is desktop-only state (in ES a point is always
        sized by the vertex shader's gl_PointSize, and WebGL2 rejects the enum
        with INVALID_ENUM). The plugin enables it around the splat, the one
        pass that draws points, and GLState.h saves and restores it on every
        frame, as a plugin inside Resolume must. Here it reads as off and
        enabling it is ignored: the splat's vertex shader writes gl_PointSize,
        which is what ES sizes the point by anyway, so the points are the
        plugin's size. Every other capability is passed through unchanged.

    glTexImage2D
        The field lattice (RG32F) and the splat's density (RG16F) are render
        targets the FFGL SDK's FFGLFBO::Initialise (PassBuffer's base)
        allocates as glTexImage2D( ..., internal, ..., GL_RGBA, GL_FLOAT, NULL ).
        Desktop GL takes any client format with no data; OpenGL ES 3.0, and so
        WebGL2, takes only the pairs in its table (ES 3.0.6, table 3.2), and
        for RG32F and RG16F the format is GL_RG -- with GL_RGBA it answers
        INVALID_OPERATION, the texture is never allocated, the framebuffer is
        incomplete and the plugin reports that it could not allocate the
        buffers. So an allocation with NO data of one of those two is given
        GL_RG; with no data there is nothing for a client format to describe,
        and nothing about what is allocated changes. The grain state's RGBA32F
        allocation already passes ES's own pair (GL_RGBA, GL_FLOAT) and goes to
        emscripten's glTexImage2D as it is. The plugin uploads nothing through
        glTexImage2D (the mode shapes go up through glTexImage3D and
        glTexSubImage3D as R32F from GL_RED floats, a pair ES takes as it is).
        Anything else aborts, by design, so a change in the plugin cannot be
        absorbed here quietly.

    Nothing else needed one. Rendering into RG32F, RGBA32F and RG16F needs
    EXT_color_buffer_float, which the page asks for and stops without; the one
    blended pass, the splat, blends into RG16F, which that extension allows
    (32-bit float blending would need EXT_float_blend, and nothing asks for it).
    Every float texture is read by texelFetch, with nearest filtering, so no
    float filtering is needed, and the plugin reads nothing back (no glReadPixels:
    bubble's R32F-read-as-RGBA trap does not arise).
*/
#include <GLES3/gl3.h>
#include <emscripten/emscripten.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" void emscripten_glTexImage2D( GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border,
                                         GLenum format, GLenum type, const void* pixels );

namespace
{
constexpr GLenum kProgramPointSize = 0x8642;///< GL_PROGRAM_POINT_SIZE, desktop GL 3.2

// clang-format off
EM_JS( void, edShaderSource, ( GLuint shader, const char* source ), {
	const object = GL.shaders[ shader ];
	const stage  = GLctx.getShaderParameter( object, 0x8B4F /* GL_SHADER_TYPE */ ) === 0x8B31 ? 'vertex' : 'fragment';
	GLctx.shaderSource( object, Module[ 'eidophoneShaderSource' ]( stage, UTF8ToString( source ) ) );
} );
EM_JS( void, edEnable, ( GLenum cap ), { GLctx.enable( cap ); } );
EM_JS( void, edDisable, ( GLenum cap ), { GLctx.disable( cap ); } );
EM_JS( int, edIsEnabled, ( GLenum cap ), { return GLctx.isEnabled( cap ) ? 1 : 0; } );
// clang-format on

[[noreturn]] void refuse( const char* why, GLint internalformat, GLenum format, GLenum type )
{
	std::fprintf( stderr, "eidophone demo: glTexImage2D( internal 0x%x, format 0x%x, type 0x%x ) %s -- not a call this page's GL shim carries\n",
	              static_cast< unsigned >( internalformat ), format, type, why );
	std::abort();
}
} // namespace

extern "C"
{
void glShaderSource( GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length )
{
	std::string source;
	for( GLsizei i = 0; i < count; ++i )
	{
		if( string[ i ] == nullptr )
			continue;
		const size_t n = ( length != nullptr && length[ i ] >= 0 ) ? static_cast< size_t >( length[ i ] ) : std::strlen( string[ i ] );
		source.append( string[ i ], n );
	}
	edShaderSource( shader, source.c_str() );
}

void glEnable( GLenum cap )
{
	if( cap != kProgramPointSize )
		edEnable( cap );
}

void glDisable( GLenum cap )
{
	if( cap != kProgramPointSize )
		edDisable( cap );
}

GLboolean glIsEnabled( GLenum cap )
{
	if( cap == kProgramPointSize )
		return GL_FALSE;
	return edIsEnabled( cap ) ? GL_TRUE : GL_FALSE;
}

void glTexImage2D( GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format,
                   GLenum type, const void* pixels )
{
	if( target != GL_TEXTURE_2D || level != 0 )
		refuse( "is not a level-0 2D texture", internalformat, format, type );
	if( pixels != nullptr )
		refuse( "uploads data, which the plugin never does through glTexImage2D", internalformat, format, type );
	if( format != GL_RGBA || type != GL_FLOAT )
		refuse( "is not FFGLFBO's float allocation", internalformat, format, type );

	//FFGLFBO's allocations, by the plugin's three formats.
	switch( internalformat )
	{
	case GL_RG32F:  //the field lattice
	case GL_RG16F:  //the splat's density
		emscripten_glTexImage2D( target, level, internalformat, width, height, border, GL_RG, GL_FLOAT, nullptr );
		return;
	case GL_RGBA32F://the grain state, already an ES pair
		emscripten_glTexImage2D( target, level, internalformat, width, height, border, format, type, nullptr );
		return;
	default: refuse( "allocates a buffer in none of the plugin's three formats", internalformat, format, type );
	}
}
} // extern "C"
