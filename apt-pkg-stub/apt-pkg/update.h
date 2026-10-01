/* apt-pkg-stub/apt-pkg/update.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg update helpers. The Synaptic UI uses
 * the free function ListUpdate() to refresh the package lists. Under
 * HAVE_XBPS the equivalent is xbps_rpool_sync(); the stub is a no-op
 * returning true (success).
 */
#pragma once

#include <apt-pkg/macros.h>

class pkgAcquireStatus;
class pkgSourceList;

APT_PUBLIC bool ListUpdate(pkgAcquireStatus & /*progress*/,
                          pkgSourceList & /*sources*/,
                          unsigned int /*pulseInterval*/ = 0);
