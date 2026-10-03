#pragma once
// One include for OpenGL across platforms:
//   Raspberry Pi / Linux : OpenGL ES 3.1 headers, linked directly
//   Windows (desktop)    : OpenGL 4.3 core through the glad loader (gl::loadFunctions() after context creation)
// Shaders are written as GLSL ES 3.00; gl::Shader rewrites the #version line for desktop GL.

#if defined(IT_DESKTOP_GL)
#include <glad/gl.h>
#else
#include <GLES3/gl31.h>
#include <GLES2/gl2ext.h>
#endif

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif
