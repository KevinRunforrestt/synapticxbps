/* apt-pkg-stub/apt-pkg/progress.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg OpProgress class. Under HAVE_XBPS the
 * Synaptic UI's RGCacheProgress inherits OpProgress (via the RProgress
 * typedef in rprogress.h), so the class MUST be defined here and MUST
 * inherit from RProgress so the GUI can keep its existing code paths
 * (which call Progress()/Done()/CheckChange()/MajorChange).
 *
 * Under !HAVE_XBPS the real apt-pkg/progress.h defines OpProgress
 * itself; this stub header is not used in that case.
 */
#pragma once

#include "config.h"
#ifdef HAVE_XBPS
#  include "rprogress.h"
#endif

#include <apt-pkg/macros.h>

#include <string>

class APT_PUBLIC OpProgress
#ifdef HAVE_XBPS
   : public RProgress
#endif
{
 public:
   OpProgress() = default;
   virtual ~OpProgress() = default;

   /* The real apt-pkg class has the following virtual methods. Under
    * HAVE_XBPS the RProgress base provides Update()/Done()/Reset();
    * here we add the remaining apt-pkg-specific methods as no-ops. */

   /* The GUI sets MajorChange=true before triggering a major progress
    * bar reset. We expose it as a public field so the GUI can write to
    * it directly (as it does in rgcacheprogress.cc). */
   bool MajorChange = false;

   /* APT's CheckChange() returns true if the progress bar should be
    * redrawn. The stub always returns false — the GUI's own pulse
    * timer drives redraws under HAVE_XBPS. */
   virtual bool CheckChange() { return false; }

   /* APT's Update() takes no arguments (it reads the current Op/SubOp/
    * Percent fields). Under HAVE_XBPS the RProgress base provides a
    * three-argument Update; the no-arg override is left here so the
    * GUI can call `OpProgress::Update()` as in the original code. */
   virtual void Update() {}

   /* Reset to "no operation in progress". */
   virtual void Reset() {}

   /* Done — the operation is complete. */
   virtual void Done() {}

   /* The real class exposes SubOp, Op, Percent fields. Under HAVE_XBPS
    * RProgress has its own _op, _sub, _percent members but their names
    * differ; we re-expose the apt-pkg names so the GUI code can keep
    * writing `progress.Op = "..."` etc. */
   std::string Op;
   std::string SubOp;
   int Percent = 0;
};
