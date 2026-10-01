/* apt-pkg-stub/apt-pkg/cachefile.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg pkgCacheFile class. The Synaptic UI
 * uses pkgCacheFile::GetDepCache, GetSourceList, Open, Close. Under
 * HAVE_XBPS all return NULL / false because the APT backend is not
 * actually present.
 */
#pragma once

#include <apt-pkg/depcache.h>
#include <apt-pkg/sourcelist.h>
#include <apt-pkg/macros.h>

class APT_PUBLIC pkgCacheFile
{
 public:
   pkgCacheFile() = default;
   ~pkgCacheFile() = default;

   pkgDepCache *GetDepCache() { return nullptr; }
   pkgSourceList *GetSourceList() { return nullptr; }
   pkgCache *GetPkgCache() { return nullptr; }

   bool Open(class RProgress * /*progress*/, unsigned int /*flags*/ = 0) { return false; }
   bool Open(class pkgAcquireStatus * /*progress*/) { return false; }
   void Close() {}

   /* In the real apt-pkg, OpenFile is also a member function returning
    * false for the in-memory cache. */
   bool BuildCaches(class pkgAcquireStatus * /*progress*/, bool /*withLock*/ = false) { return false; }
};
