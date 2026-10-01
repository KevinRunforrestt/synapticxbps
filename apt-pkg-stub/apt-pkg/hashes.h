/* apt-pkg-stub/apt-pkg/hashes.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg Hashes, HashString and the RFC1123
 * date parser. The Synaptic UI uses Hashes, HashString and
 * RFC1123StrToTime in a few places (mostly in deb-specific code paths
 * that are gated on !HAVE_XBPS).
 */
#pragma once

#include <apt-pkg/macros.h>

#include <string>
#include <time.h>

class APT_PUBLIC Hashes
{
 public:
   Hashes() = default;
   ~Hashes() = default;

   bool Add(const void * /*buf*/, unsigned long long /*len*/) { return false; }
   bool AddFD(int /*fd*/, unsigned long long /*size*/) { return false; }
};

class APT_PUBLIC HashString
{
 public:
   HashString() = default;
   HashString(const std::string & /*type*/, const std::string & /*value*/) {}
   ~HashString() = default;

   std::string HashType() const { return ""; }
   std::string HashValue() const { return ""; }
   bool VerifyFile(std::string /*file*/) const { return false; }
};

APT_PUBLIC bool RFC1123StrToTime(const char * /*str*/, time_t & /*t*/);
