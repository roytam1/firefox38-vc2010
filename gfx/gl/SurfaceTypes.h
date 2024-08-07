/* -*- Mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 40; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SURFACE_TYPES_H_
#define SURFACE_TYPES_H_

#include "mozilla/RefPtr.h"
#include "mozilla/Attributes.h"
#include "mozilla/TypedEnum.h"
#include <stdint.h>

namespace mozilla {
namespace layers {
class ISurfaceAllocator;
}

namespace gl {

struct SurfaceCaps final
{
    bool any;
    bool color, alpha;
    bool bpp16;
    bool depth, stencil;
    bool antialias;
    bool premultAlpha;
    bool preserve;

    // The surface allocator that we want to create this
    // for.  May be null.
    RefPtr<layers::ISurfaceAllocator> surfaceAllocator;

    SurfaceCaps();
    SurfaceCaps(const SurfaceCaps& other);
    ~SurfaceCaps();

    void Clear();

    SurfaceCaps& operator=(const SurfaceCaps& other);

    // We can't use just 'RGB' here, since it's an ancient Windows macro.
    static SurfaceCaps ForRGB() {
        SurfaceCaps caps;

        caps.color = true;

        return caps;
    }

    static SurfaceCaps ForRGBA() {
        SurfaceCaps caps;

        caps.color = true;
        caps.alpha = true;

        return caps;
    }

    static SurfaceCaps Any() {
        SurfaceCaps caps;

        caps.any = true;

        return caps;
    }
};

MOZ_BEGIN_ENUM_CLASS(SharedSurfaceType, uint8_t)
    Unknown = 0,

    Basic,
    GLTextureShare,
    EGLImageShare,
    EGLSurfaceANGLE,
    DXGLInterop,
    DXGLInterop2,
    Gralloc,
    IOSurface,

    Max
MOZ_END_ENUM_CLASS(SharedSurfaceType)

MOZ_BEGIN_ENUM_CLASS(AttachmentType, uint8_t)
    Screen = 0,

    GLTexture,
    GLRenderbuffer,

    Max
MOZ_END_ENUM_CLASS(AttachmentType)

} /* namespace gfx */
} /* namespace mozilla */

#endif /* SURFACE_TYPES_H_ */
