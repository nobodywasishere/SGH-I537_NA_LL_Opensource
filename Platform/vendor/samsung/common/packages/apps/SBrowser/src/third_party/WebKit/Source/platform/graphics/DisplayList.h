/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef DisplayList_h
#define DisplayList_h

#include "platform/geometry/FloatRect.h"

#include "wtf/FastAllocBase.h"
#include "wtf/RefCounted.h"
#include "wtf/RefPtr.h"

#if defined(SBROWSER_GPU_RASTERIZATION_ENABLE)
#include "third_party/skia/include/core/SkPictureRecorder.h"
#include "wtf/PassOwnPtr.h"
#endif

class SkPicture;

namespace WebCore {

class PLATFORM_EXPORT DisplayList FINAL : public WTF::RefCounted<DisplayList> {
    WTF_MAKE_FAST_ALLOCATED;
    WTF_MAKE_NONCOPYABLE(DisplayList);
public:
    DisplayList(const FloatRect&);
    ~DisplayList();

    const FloatRect& bounds() const;
    SkPicture* picture() const;

#if defined(SBROWSER_GPU_RASTERIZATION_ENABLE)
    SkCanvas* beginRecording(const IntSize&, uint32_t recordFlags = 0);
    bool isRecording() const { return m_recorder; }
    void endRecording();
#endif

private:
    FloatRect m_bounds;
    RefPtr<SkPicture> m_picture;
#if defined(SBROWSER_GPU_RASTERIZATION_ENABLE)	
    OwnPtr<SkPictureRecorder> m_recorder;
#endif
};

}

#endif // DisplayList_h