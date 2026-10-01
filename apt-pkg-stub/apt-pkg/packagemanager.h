/* apt-pkg-stub/apt-pkg/packagemanager.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg pkgPackageManager class. The Synaptic
 * UI uses pkgPackageManager::OrderResult (Failed/Completed/Incomplete)
 * and DoInstallPreFork/DoInstallPostFork in rginstallprogress.cc.
 *
 * Under HAVE_XBPS RInstallProgress::start() takes a `void *pm_unused`
 * instead of `pkgPackageManager *pm` (see rinstallprogress.h), so this
 * class is never instantiated. The stub exists only so the APT
 * variant's #include <apt-pkg/packagemanager.h> compiles cleanly
 * (under !HAVE_XBPS the real header is used instead).
 */
#pragma once

#include <apt-pkg/depcache.h>
#include <apt-pkg/macros.h>

class APT_PUBLIC pkgPackageManager
{
 public:
   enum OrderResult
   {
      Failed = 0,
      Completed = 1,
      Incomplete = 2
   };

   pkgPackageManager() = default;
   explicit pkgPackageManager(pkgDepCache * /*cache*/) {}
   virtual ~pkgPackageManager() = default;

   virtual OrderResult DoInstallPreFork() { return Completed; }
   virtual OrderResult DoInstallPostFork(int /*statusFd*/ = -1) { return Completed; }
};
