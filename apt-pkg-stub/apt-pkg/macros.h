/* apt-pkg-stub/apt-pkg/macros.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * The real apt-pkg/macros.h defines compiler attribute macros (APT_PURE,
 * APT_NONNULL, APT_PRINTF, APT_PUBLIC, etc.) used by the rest of the
 * apt-pkg headers. The stub headers under apt-pkg-stub/ use them too, so
 * this file must define them as no-ops for the HAVE_XBPS build.
 */
#pragma once

/* Empty attribute macros — none of the stubbed code is actually called
 * at runtime, so we don't need the compiler to enforce pure/nonnull/etc. */
#define APT_PURE
#define APT_NONNULL(...)
#define APT_PRINTF(i)
#define APT_PUBLIC
#define APT_PACKED
#define APT_COLD
#define APT_HOT
#define APT_IGNORE_DEPRECATED_PUSH
#define APT_IGNORE_DEPRECATED_POP
#define APT_DEPRECATED
#define APT_DEPRECATED_MSG(msg)
#define APT_CONST
#define APT_WEAK
#define APT_ALIGN(n)
#define APT_FALLTHROUGH
#define APT_NORETURN
#define APT_OVERRIDE
#define APT_FINAL
#define APT_BEGIN_PUBLIC_NAMESPACE
#define APT_END_PUBLIC_NAMESPACE

/* GCC __attribute__((format(printf)) stub. */
#ifdef __GNUC__
#  define APT_FORMAT_PRINTF(i, j) __attribute__((format(printf, i, j)))
#else
#  define APT_FORMAT_PRINTF(i, j)
#endif
