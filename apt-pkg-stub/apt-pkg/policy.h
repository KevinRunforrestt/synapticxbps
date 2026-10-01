/* apt-pkg-stub/apt-pkg/policy.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg pkgPolicy class and the ReadPinFile
 * free function. The Synaptic UI uses pkgPolicy in rpackagelister.cc
 * (gated on !HAVE_XBPS). The stub is inert.
 */
#pragma once

#include <apt-pkg/depcache.h>
#include <apt-pkg/macros.h>
#include <apt-pkg/pkgcache.h>

#include <string>

class APT_PUBLIC pkgPolicy
{
 public:
   pkgPolicy() = default;
   virtual ~pkgPolicy() = default;

   pkgCache::VerIterator GetCandidateVer(pkgCache::PkgIterator const & /*pkg*/) { return pkgCache::VerIterator(); }
   bool InitDefaults() { return true; }
};

APT_PUBLIC bool ReadPinFile(pkgPolicy & /*policy*/, std::string /*file*/ = "");
