/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GLCONTEXT_TYPES_H_
#define GLCONTEXT_TYPES_H_

#include "GLTypes.h"
#include "mozilla/TypedEnum.h"

namespace mozilla {
namespace gl {

class GLContext;

MOZ_BEGIN_ENUM_CLASS(GLContextType)
    Unknown,
    WGL,
    CGL,
    GLX,
    EGL
MOZ_END_ENUM_CLASS(GLContextType)

MOZ_BEGIN_ENUM_CLASS(OriginPos, uint8_t)
  TopLeft,
  BottomLeft
MOZ_END_ENUM_CLASS(OriginPos)

struct GLFormats
{
    // Constructs a zeroed object:
    GLFormats();

    GLenum color_texInternalFormat;
    GLenum color_texFormat;
    GLenum color_texType;
    GLenum color_rbFormat;

    GLenum depthStencil;
    GLenum depth;
    GLenum stencil;

    GLsizei samples;
};

} /* namespace gl */
} /* namespace mozilla */

#endif /* GLCONTEXT_TYPES_H_ */
