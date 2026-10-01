/* rpackagecache_xbps.h - no-op RPackageCache wrapper for the XBPS backend
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * The XBPS backend no longer links against libxbps. All package
 * queries are issued via `popen("xbps-query …")` from RPackageLister
 * and RPackage, and all privileged operations run via
 * `fork() + execvp("xbps-install" | "xbps-remove")` from a GThread.
 *
 * This class is retained as an inert placeholder so the existing
 * RPackageLister API (which holds a `RPackageCacheXbps *_cache`
 * pointer) keeps compiling. Every method is a no-op or returns an
 * empty result.
 */

#pragma once

#include "config.h"

#ifdef HAVE_XBPS

#include <string>
#include <vector>

class RProgress;

class RPackageCacheXbps
{
 public:
   RPackageCacheXbps() {}
   ~RPackageCacheXbps() { close(); }

   /* Always returns 0 (success) — there is no handle to initialize. */
   int open(RProgress *prog = NULL,
            const std::string &rootdir = "",
            const std::string &confdir = "",
            bool lock = true);

   /* No-op — there is no handle to release. */
   void close();

   /* No-ops — xbps-install / xbps-remove handle their own pkgdb
    * locking via flock(2) on /var/db/xbps/.xbps-pkgdb.lock. */
   int lock()  { return 0; }
   void unlock() {}

   /* Borrowed accessors — always NULL because there is no handle. */
   void *handle() { return NULL; }
   void *pkgdb()  { return NULL; }

   /* Lookups — always NULL because we don't keep a pkgdb dictionary
    * in memory. RPackageLister::getPackage(name) does the lookup
    * against the in-memory RPackage list instead. */
   void *getInstalledPkg(const std::string &name) { (void)name; return NULL; }
   void *getAvailablePkg(const std::string &name) { (void)name; return NULL; }

   /* Iteration — no-ops; RPackageLister::openCache() drives the
    * iteration itself via popen("xbps-query -l") and
    * popen("xbps-query -Rs -"). */
   typedef int (*PkgdbForeachCb)(void *, void *, const char *, void *, bool *);
   void foreachInstalled(PkgdbForeachCb cb, void *arg) { (void)cb; (void)arg; }

   typedef int (*RpoolForeachCb)(void *, void *, bool *);
   void foreachRepo(RpoolForeachCb cb, void *arg) { (void)cb; (void)arg; }

   /* List of configured repository URLs — empty. The repository
    * editor (rsources_xbps.cc) reads /etc/xbps.d directly. */
   std::vector<std::string> getRepositoryUrls() { return std::vector<std::string>(); }

   /* Policy-stub: APT's RPackageCache::getPolicyArchives returned the
    * list of locally cached .deb files. Under XBPS we return an
    * empty list; cleanPackageCache() in RPackageLister does the
    * actual /var/cache/xbps directory walk. */
   std::vector<std::string> getPolicyArchives(bool filenames_only = false)
   {
      (void)filenames_only;
      return std::vector<std::string>();
   }

 private:
   /* No state — every member is gone now that we don't hold a
    * libxbps handle. The bool below is kept only so the class has
    * a non-empty representation for debuggers. */
   bool _placeholder;
};

#endif /* HAVE_XBPS */
