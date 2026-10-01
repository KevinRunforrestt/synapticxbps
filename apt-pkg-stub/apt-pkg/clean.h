/* apt-pkg-stub/apt-pkg/clean.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg pkgArchiveCleaner class. The Synaptic
 * UI uses pkgArchiveCleaner::Go to prune /var/cache/apt/archives. Under
 * HAVE_XBPS the equivalent is implemented in
 * RPackageLister::cleanPackageCache() (rpackagelister_xbps.cc); the
 * stub Go is a no-op returning true.
 */
#pragma once

#include <apt-pkg/macros.h>

class APT_PUBLIC pkgArchiveCleaner
{
 public:
   pkgArchiveCleaner() = default;
   ~pkgArchiveCleaner() = default;

   bool Go(std::string /*dir*/, class pkgDepCache & /*cache*/) { return true; }
};
