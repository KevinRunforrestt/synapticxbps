/* rprogress.h - abstract progress interface
 *
 * This is a backend-agnostic replacement for APT's OpProgress class.
 * Under APT we still wrap pkgOpProgress; under XBPS we drive it from
 * libxbps state callbacks.
 *
 * The Synaptic UI (RGCacheProgress) subclasses this under both
 * backends. The public interface is identical so the UI does not need
 * to know which backend is in use.
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#pragma once

#include "config.h"

#include <string>

#ifdef HAVE_XBPS
/* Under XBPS we use this class directly. */
class RProgress
{
 public:
   RProgress() : _percent(0), _op(""), _sub("") {}
   virtual ~RProgress() {}

   /* Update the progress display. `op` is the major operation
    * (e.g. "Reading package lists"), `sub` is the sub-operation
    * (e.g. the name of the package currently being processed),
    * `percent` is 0..100. */
   virtual void Update(const std::string &op, const std::string &sub, int percent)
   {
      _op = op; _sub = sub; _percent = percent;
   }
   /* Called when the operation is done. */
   virtual void Done() {}
   /* A major operation starts; reset the meter. */
   virtual void Reset() { _percent = 0; _op.clear(); _sub.clear(); }

   int getPercent() const { return _percent; }
   const std::string &getOp() const { return _op; }
   const std::string &getSubOp() const { return _sub; }

 protected:
   int _percent;
   std::string _op;
   std::string _sub;
};
#else
/* Under APT we inherit from OpProgress directly. The wrapper is in
 * rprogress_apt.h (not included here to avoid pulling apt-pkg into
 * every consumer of this header). */
#   include <apt-pkg/progress.h>
class RProgress : public OpProgress
{
   /* Under APT the UI subclass directly inherits OpProgress, so this
    * is just a typedef-style alias. */
};
#endif
