/* apt-pkg-stub/apt-pkg/pkgsystem.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg pkgSystem class. The Synaptic UI uses
 * the global `_system` pointer in a few places (e.g. gsynaptic.cc's
 * `_system->Lock()` for the package-cache lock; rgmainwindow.cc calls
 * `_system->CreatePM()` to build the install transaction).
 *
 * Under HAVE_XBPS we expose a no-op class and a NULL global pointer
 * (defined in apt-pkg-stub.cc). The gsynaptic.cc Lock path is gated on
 * !HAVE_XBPS already (the lock is taken by RPackageCacheXbps instead),
 * and rgmainwindow.cc's CreatePM path is gated on !HAVE_XBPS too, so
 * the NULL pointer is never dereferenced.
 */
#pragma once

#include <apt-pkg/macros.h>

#include <string>

class APT_PUBLIC pkgSystem
{
 public:
   virtual ~pkgSystem() = default;

   virtual int Lock() { return 0; }
   virtual int UnLock(bool /*oldstate*/ = false) { return 0; }
   virtual int LockInner() { return 0; }
   virtual int UnLockInner() { return 0; }
   virtual class pkgPackageManager *CreatePM(class pkgDepCache * /*cache*/) const { return nullptr; }

   /* The real class has a public Label member. */
   const char *Label = "";
};

/* The single global system pointer. Defined (as nullptr) in
 * apt-pkg-stub.cc. */
extern pkgSystem *_system APT_PUBLIC;
