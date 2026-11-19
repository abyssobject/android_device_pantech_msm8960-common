/*
 * Copyright (C) 2026 The Android Open Source Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Vendor libskia.so for libqc-opt.so (the mm-camera frame processing library,
 * dlopen()ed by mm-qcamera-daemon). It imports only these two Skia symbols
 * (rectangle drawing, presumably an overlay); the canvas and paint objects come
 * from its callers. A vendor process cannot load the platform Skia, so nothing
 * is drawn; without this library libqc-opt.so fails to load and qcamsvr_start
 * aborts (no camera at all).
 */

extern "C" void SkPaint_setColor(void* /* paint */, unsigned /* color */)
        __asm__("_ZN7SkPaint8setColorEj");
extern "C" void SkPaint_setColor(void*, unsigned) {}

extern "C" void SkCanvas_drawRectCoords(void* /* canvas */, float /* left */, float /* top */,
                                        float /* right */, float /* bottom */,
                                        const void* /* paint */)
        __asm__("_ZN8SkCanvas14drawRectCoordsEffffRK7SkPaint");
extern "C" void SkCanvas_drawRectCoords(void*, float, float, float, float, const void*) {}
