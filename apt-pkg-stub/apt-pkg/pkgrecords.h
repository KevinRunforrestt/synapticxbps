/* apt-pkg-stub/apt-pkg/pkgrecords.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg pkgRecords class and its Parser
 * inner class. The Synaptic UI uses pkgRecords::Parser methods
 * (Maintainer, ShortDesc, LongDesc, Name, Homepage, SourcePkg, Hashes)
 * in rpackage.cc (gated on !HAVE_XBPS). The stub is inert.
 */
#pragma once

#include <apt-pkg/macros.h>
#include <apt-pkg/pkgcache.h>

#include <string>

class APT_PUBLIC pkgRecords
{
 public:
   pkgRecords() = default;
   virtual ~pkgRecords() = default;

   class APT_PUBLIC Parser
   {
    public:
     Parser() = default;
     virtual ~Parser() = default;

     virtual std::string Maintainer() const { return ""; }
     virtual std::string ShortDesc() const { return ""; }
     virtual std::string LongDesc() const { return ""; }
     virtual std::string Name() const { return ""; }
     virtual std::string Homepage() const { return ""; }
     virtual std::string SourcePkg() const { return ""; }
     virtual std::string Hashes() const { return ""; }
   };

   Parser &Lookup(pkgCache::VerFileIterator const & /*ver*/) { return _stub; }

 private:
   Parser _stub;
};
