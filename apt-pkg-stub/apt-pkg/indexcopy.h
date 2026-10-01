/* apt-pkg-stub/apt-pkg/indexcopy.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg pkgCdrom::CopyPackages helper. The
 * Synaptic UI uses it in rpmindexcopy.cc (gated on HAVE_RPM, which is
 * not set under HAVE_XBPS). The stub is inert.
 */
#pragma once

#include <apt-pkg/cdrom.h>
#include <apt-pkg/macros.h>

class APT_PUBLIC pkgCdrom::CopyPackages
{
 public:
   CopyPackages() = default;
   ~CopyPackages() = default;

   bool Copy(std::string /*root*/, std::string /*cd*/,
             std::vector<std::string> & /*cds*/, std::vector<std::string> & /*packages*/) { return false; }
};
