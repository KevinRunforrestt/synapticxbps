/* rpackagecache_xbps.cc - no-op RPackageCache implementation for the
 *                         popen-based XBPS backend.
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * The popen-based XBPS backend does not link against libxbps. There
 * is no struct xbps_handle, no pkgdb dictionary, and no repository
 * pool to manage in memory. Every method here is therefore a no-op
 * or returns an empty result.
 */

#include "config.h"

#ifdef HAVE_XBPS

#include "rpackagecache_xbps.h"
#include "rprogress.h"
#include "i18n.h"

#include <string>
#include <vector>

int
RPackageCacheXbps::open(RProgress *prog,
                        const std::string &rootdir,
                        const std::string &confdir,
                        bool lock)
{
   (void)rootdir; (void)confdir; (void)lock;
   if (prog) {
      prog->Update(_("Reading XBPS package database"), "", 100);
      prog->Done();
   }
   return 0;
}

void
RPackageCacheXbps::close()
{
   /* No-op — nothing to release. */
}

#endif /* HAVE_XBPS */
