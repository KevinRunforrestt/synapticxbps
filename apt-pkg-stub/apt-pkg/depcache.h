/* apt-pkg-stub/apt-pkg/depcache.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg pkgDepCache class. The Synaptic UI
 * refers to pkgDepCache::StateCache, pkgDepCache::ActionGroup,
 * pkgDepCache::State (SaveState/RestoreState), MarkInstall, MarkDelete,
 * MarkKeep, MarkAuto, SetReInstall, writeStateFile, MarkAndSweep and
 * BrokenCount. Under HAVE_XBPS all these are inert no-ops so the GUI
 * compiles and links cleanly.
 *
 * The State typedef inside pkgDepCache is the State_SaveRestore class
 * declared in pkgcache.h. We typedef it here so rpackagelister.h's
 * `pkgDepCache::State pkgState` declaration (under HAVE_RPM, which is
 * never set under HAVE_XBPS) still resolves.
 */
#pragma once

#include <apt-pkg/macros.h>
#include <apt-pkg/pkgcache.h>

#include <string>

class APT_PUBLIC pkgDepCache
{
 public:
   /* The per-package state-cache entry the GUI iterates over. */
   struct APT_PUBLIC StateCache
   {
      bool NowBroken() const { return false; }
      bool InstBroken() const { return false; }
      bool Keep() const { return true; }
      bool Install() const { return false; }
      bool Delete() const { return false; }
      bool NewInstall() const { return false; }
      bool ReInstall() const { return false; }
      bool Upgrade() const { return false; }
      bool Downgrade() const { return false; }
      bool Held() const { return false; }
      bool Garbage() const { return false; }
      const char *CandVerStr() const { return ""; }
   };

   /* ActionGroup defers dependency re-resolution in the real apt-pkg;
    * under HAVE_XBPS the constructor / destructor are no-ops. */
   class APT_PUBLIC ActionGroup
   {
    public:
     explicit ActionGroup(pkgDepCache & /*cache*/) {}
     ~ActionGroup() = default;
   };

   /* State snapshot/restore. The real apt-pkg uses pkgCache::State_SaveRestore;
    * the stub aliases it to keep rpackagelister.h's HAVE_RPM branch happy. */
   typedef pkgCache::State_SaveRestore State;

   pkgDepCache() = default;
   virtual ~pkgDepCache() = default;

   /* Marking. The stubs return true (success) so the GUI's call sites
    * continue without aborting. */
   bool MarkInstall(pkgCache::PkgIterator & /*pkg*/, bool /*autoInst*/ = true,
                    bool /*fromUser*/ = true, unsigned long /*Depth*/ = 0,
                    bool /*Force*/ = false, bool /*Update*/ = true) { return true; }
   bool MarkDelete(pkgCache::PkgIterator & /*pkg*/, bool /*purge*/ = false,
                   unsigned long /*Depth*/ = 0, bool /*fromUser*/ = true) { return true; }
   bool MarkKeep(pkgCache::PkgIterator & /*pkg*/, bool /*autoInst*/ = false,
                 bool /*fromUser*/ = true, unsigned long /*Depth*/ = 0) { return true; }
   bool MarkAuto(pkgCache::PkgIterator & /*pkg*/, bool /*autoInst*/) { return true; }
   void SetReInstall(pkgCache::PkgIterator & /*pkg*/, bool /*reinstall*/) {}

   /* writeStateFile / MarkAndSweep are used by rgmainwindow.cc to flush
    * the auto-install bookkeeping. Under HAVE_XBPS the equivalent is
    * the `automatic-install` bool key on the pkgdb dictionary (set by
    * RPackage::setAuto()), so these are no-ops here. */
   bool writeStateFile(class RProgress * /*prog*/, bool /*installedOnly*/) { return true; }
   void MarkAndSweep() {}

   /* BrokenCount returns the number of broken packages; the stub
    * returns 0. */
   unsigned long BrokenCount() const { return 0; }

   /* HeadPkg / NextPkg iterators used by the GUI to walk every package.
    * The stub returns end()-style iterators. */
   pkgCache::PkgIterator PkgBegin() { return pkgCache::PkgIterator(); }
   pkgCache::PkgIterator PkgFindByName(const std::string & /*name*/) { return pkgCache::PkgIterator(); }
};
