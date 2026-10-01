/* rfetchstatus.h - abstract download-progress interface
 *
 * Replaces APT's pkgAcquireStatus under the XBPS backend. The Synaptic
 * UI (RGFetchProgress) subclasses this. The popen-based XBPS backend
 * does not invoke any per-file callback — it just runs `xbps-install`
 * in a child process and pulses the UI's progress bar while the
 * child runs. This abstract interface bridges the APT and XBPS
 * backends so the UI does not need to know which is active.
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

#ifndef HAVE_XBPS
/* Under APT, the UI subclass inherits pkgAcquireStatus directly. We
 * define RFetchStatus as an alias so that the RPackageLister signature
 * can name it consistently. */
#   include <apt-pkg/acquire.h>
class RFetchStatus : public pkgAcquireStatus
{
   /* Pure alias — all behaviour comes from the base class. */
};
#else

class RFetchStatus
{
 public:
   RFetchStatus() {}
   virtual ~RFetchStatus() {}

   /* A whole transaction's download phase is starting / stopping. */
   virtual void Start() {}
   virtual void Stop() {}

   /* Per-file events. `file_size` is -1 if the server didn't send a
    * Content-Length. `cb_start` is set on the first call for a given
    * file, `cb_end` on the last, `cb_update` for intermediate calls. */
   virtual void Fetch(const std::string &file_name,
                     long long file_size,
                     long long file_offset,
                     long long file_dloaded,
                     bool cb_start,
                     bool cb_update,
                     bool cb_end)
   {
      (void)file_name; (void)file_size;
      (void)file_offset; (void)file_dloaded;
      (void)cb_start; (void)cb_update; (void)cb_end;
   }

   /* A package download failed. */
   virtual void Fail(const std::string &file_name, const std::string &error)
   {
      (void)file_name; (void)error;
   }

   /* Media change request (XBPS doesn't have this concept — the
    * method exists for API parity so the APT branch can keep using
    * the same call sites). */
   virtual bool MediaChange(const std::string &media, const std::string &drive)
   {
      (void)media; (void)drive;
      return false;
   }

   /* Global pulse — called periodically so the UI can refresh. */
   virtual bool Pulse() { return true; }
};
#endif
