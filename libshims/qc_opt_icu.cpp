/*
 * Copyright (C) 2026 The Android Open Source Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Vendor libicuuc.so for libqc-opt.so, which imports four ICU 56 symbols
 * (LineageOS forwarded them to the platform ICU through an LD shim). The
 * platform ICU lives in the i18n APEX and is not visible to vendor processes.
 * u_digit and u_errorName are implemented; the UText functions report
 * U_UNSUPPORTED_ERROR.
 */

#include <stddef.h>
#include <stdint.h>

namespace {
constexpr int kUUnsupportedError = 16;  // U_UNSUPPORTED_ERROR
}

extern "C" int32_t u_digit_56(int32_t ch, int8_t radix) {
    int32_t value;
    if (ch >= '0' && ch <= '9') {
        value = ch - '0';
    } else if (ch >= 'a' && ch <= 'z') {
        value = ch - 'a' + 10;
    } else if (ch >= 'A' && ch <= 'Z') {
        value = ch - 'A' + 10;
    } else if (ch >= 0xFF10 && ch <= 0xFF19) {  // fullwidth digits
        value = ch - 0xFF10;
    } else {
        return -1;
    }
    return (radix >= 2 && radix <= 36 && value < radix) ? value : -1;
}

extern "C" const char* u_errorName_56(int code) {
    return code <= 0 ? "U_ZERO_ERROR" : "U_ERROR";
}

extern "C" void* utext_openUChars_56(void* /* ut */, const uint16_t* /* s */, int64_t /* length */,
                                     int* status) {
    if (status != nullptr && *status <= 0) {
        *status = kUUnsupportedError;
    }
    return nullptr;
}

extern "C" void* utext_close_56(void* /* ut */) {
    return nullptr;
}
