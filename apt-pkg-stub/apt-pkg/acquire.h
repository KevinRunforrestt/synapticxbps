/* apt-pkg-stub/apt-pkg/acquire.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg pkgAcquire / pkgAcquire::Item /
 * pkgAcquire::ItemDesc classes and the pkgAcquireStatus base class.
 *
 * Under HAVE_XBPS the GUI's RGFetchProgress subclass still inherits
 * pkgAcquireStatus (via RFetchStatus) so the latter MUST be defined here.
 * We make pkgAcquireStatus inherit from RFetchStatus so the GUI can
 * continue to call Start()/Stop()/Pulse()/MediaChange() through the
 * APT-style vtable without any backend code being executed.
 */
#pragma once

#include "config.h"
#ifdef HAVE_XBPS
#  include "rfetchstatus.h"
#endif

#include <apt-pkg/macros.h>

#include <cstddef>
#include <string>

class APT_PUBLIC pkgAcquire
{
 public:
   /* Used by rgfetchprogress.h: enum StatIdle/StatDone/StatError */
   enum StatIdle { StatIdle = 0 };
   enum StatDone { StatDone = 1 };
   enum StatError { StatError = 2 };
   enum StatFetching { StatFetching = 3 };

   pkgAcquire() = default;
   explicit pkgAcquire(class pkgAcquireStatus * /*status*/) {}
   ~pkgAcquire() = default;

   class APT_PUBLIC Item
   {
    public:
     enum {StatIdle, StatFetching, StatDone, StatError, StatAuthError} Stat;
     Item() : Stat(StatIdle), Owner(nullptr) {}
     virtual ~Item() = default;
     std::string DescURI() const { return ""; }
     pkgAcquire *Owner;
   };

   class APT_PUBLIC ItemDesc
   {
    public:
     std::string URI;
     std::string Description;
     std::string ShortDesc;
     Item *Owner;
     ItemDesc() : Owner(nullptr) {}
   };
};

/* Under HAVE_XBPS, RFetchStatus is the abstract base defined in
 * rfetchstatus.h. We make pkgAcquireStatus inherit from it so the GUI
 * subclass RGFetchProgress can keep inheriting pkgAcquireStatus
 * (its declaration in rgfetchprogress.h references it directly) while
 * the back-end code is replaced by the inert no-op implementations in
 * rgfetchprogress_xbps.cc.
 *
 * Under !HAVE_XBPS (the APT backend) the real apt-pkg/acquire.h declares
 * pkgAcquireStatus itself; this stub header is not used in that case
 * because apt_stub_inc is not on the include search path.
 */
class APT_PUBLIC pkgAcquireStatus
#ifdef HAVE_XBPS
   : public RFetchStatus
#endif
{
 public:
   pkgAcquireStatus() = default;
   virtual ~pkgAcquireStatus() = default;

   /* APT-style overrides that the GUI calls. They forward to the
    * RFetchStatus base under HAVE_XBPS; under !HAVE_XBPS the real
    * apt-pkg base provides them. */
   virtual void Start() {}
   virtual void Stop() {}
   virtual bool Pulse() { return true; }
   virtual bool MediaChange(std::string /*media*/, std::string /*drive*/) { return false; }

   /* Per-item events used by the GUI's table view. */
   virtual void IMSHit(pkgAcquire::ItemDesc &/*itm*/) {}
   virtual void Fetch(pkgAcquire::ItemDesc &/*itm*/) {}
   virtual void Done(pkgAcquire::ItemDesc &/*itm*/) {}
   virtual void Fail(pkgAcquire::ItemDesc &/*itm*/) {}

   /* Download-speed tracking fields. The GUI's rgfetchprogress.cc reads
    * CurrentCPS to render the "kB/s" label. */
   unsigned long CurrentCPS = 0;
   unsigned long CurrentID = 0;
   unsigned long TotalBytes = 0;
   unsigned long FetchedBytes = 0;
   unsigned long ElapsedTime = 0;
   unsigned long TotalItems = 0;
   unsigned long FetchedItems = 0;
   bool Update = false;
   bool MorePulses = false;
};
