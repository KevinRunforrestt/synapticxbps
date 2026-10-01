/* apt-pkg-stub/apt-pkg/versionmatch.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg pkgVersionMatch class. The Synaptic
 * UI uses pkgVersionMatch (MatchType enum: Version, Release, Origin,
 * and the Find method) in rpackagelister.cc (gated on !HAVE_XBPS).
 * The stub is inert.
 */
#pragma once

#include <apt-pkg/macros.h>
#include <apt-pkg/pkgcache.h>

#include <string>

class APT_PUBLIC pkgVersionMatch
{
 public:
   enum MatchType
   {
      Version = 0,
      Release = 1,
      Origin = 2
   };

   pkgVersionMatch() = default;
   pkgVersionMatch(std::string /*pattern*/, MatchType /*type*/) {}
   ~pkgVersionMatch() = default;

   bool Find(pkgCache::PkgIterator /*pkg*/) { return false; }
};
