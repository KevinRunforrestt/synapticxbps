/* apt-pkg-stub/apt-pkg/indexfile.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg pkgIndexFile class. The Synaptic UI
 * uses pkgIndexFile in rpackagelister.cc (gated on !HAVE_XBPS) and in
 * rtagcollbuilder.cc (gated on HAVE_XAPIAN && !HAVE_XBPS). The stub is
 * inert.
 */
#pragma once

#include <apt-pkg/macros.h>

#include <string>

class APT_PUBLIC pkgIndexFile
{
 public:
   pkgIndexFile() = default;
   virtual ~pkgIndexFile() = default;

   virtual std::string Describe() const { return ""; }
   virtual std::string ArchiveURI(std::string /*file*/) const { return ""; }
   virtual bool Exists() const { return false; }
};
