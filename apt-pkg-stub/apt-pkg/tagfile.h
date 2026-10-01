/* apt-pkg-stub/apt-pkg/tagfile.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg tag file reader. The Synaptic UI uses
 * pkgTagSection (Scan, Find, FindS, FindI, Exists, size) and pkgTagFile
 * (Step, Init) in rpackagelister.cc (gated on !HAVE_XBPS) and in
 * raptoptions.cc (gated on !HAVE_XBPS). The stubs return inert defaults
 * so the headers compile.
 */
#pragma once

#include <apt-pkg/macros.h>

#include <cstddef>
#include <string>

class APT_PUBLIC pkgTagSection
{
 public:
   pkgTagSection() = default;
   ~pkgTagSection() = default;

   bool Scan(const char * /*start*/, unsigned long /*maxlen*/) { return false; }
   bool Find(const char * /*field*/, const char *& /*start*/, const char *& /*end*/) const { return false; }
   bool Find(const char * /*field*/, unsigned int &/*start*/, unsigned int &/*end*/) const { return false; }
   std::string FindS(const char * /*field*/) const { return ""; }
   signed int FindI(const char * /*field*/, signed int def = 0) const { return def; }
   bool Exists(const char * /*field*/) const { return false; }
   std::size_t size() const { return 0; }
};

class APT_PUBLIC pkgTagFile
{
 public:
   pkgTagFile() = default;
   explicit pkgTagFile(class FileFd * /*fd*/, unsigned long long /*size*/ = 0) {}
   ~pkgTagFile() = default;

   bool Init(class FileFd * /*fd*/) { return false; }
   bool Step(pkgTagSection & /*sect*/) { return false; }
};
