/* apt-pkg-stub/apt-pkg/install-progress.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg APT::Progress::PackageManagerProgressFd
 * class. The Synaptic UI uses it in rginstallprogress.cc and in
 * rgdebinstallprogress.cc (both excluded from the HAVE_XBPS build).
 * The stub is inert.
 */
#pragma once

#include <apt-pkg/macros.h>

#include <cstddef>
#include <string>

namespace APT
{
namespace Progress
{
class APT_PUBLIC PackageManagerProgressFd
{
 public:
   PackageManagerProgressFd() = default;
   PackageManagerProgressFd(int /*fd*/) {}
   virtual ~PackageManagerProgressFd() = default;

   virtual void StartDpkg() {}
   virtual void StopDpkg() {}
   virtual void SetPid(pid_t /*pid*/) {}
   virtual bool Pulse(int /*fd*/) { return false; }
   virtual void Error(std::string /*msg*/, bool /*err*/ = true) {}
};
};
};
