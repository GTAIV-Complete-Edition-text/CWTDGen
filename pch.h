// pch.h: This is a precompiled header file.
// Files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing features.
// However, files listed here are ALL re-compiled if any one of them is updated between builds.
// Do not add files here that you will be updating frequently as this negates the performance advantage.

#ifndef PCH_H
#define PCH_H

// add headers that you want to pre-compile here
#include "framework.h"

// wil
#ifndef _DEBUG
#define RESULT_DIAGNOSTICS_LEVEL 1
#endif

#include <wil/resource.h>
#include <wil/com.h>

#include <DirectXTex.h>

#include <zlib.h>
using unique_z_stream_inflate = wil::unique_struct<z_stream, decltype(&inflateEnd), inflateEnd>;
using unique_z_stream_deflate = wil::unique_struct<z_stream, decltype(&deflateEnd), deflateEnd>;

#endif //PCH_H
