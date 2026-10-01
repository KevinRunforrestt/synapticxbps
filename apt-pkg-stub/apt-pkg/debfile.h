/* apt-pkg-stub/apt-pkg/debfile.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg debDebFile class. The Synaptic UI
 * uses debDebFile (and its inner MemControlExtract class) in rgdebfile
 * operations, which are excluded from the HAVE_XBPS build. The stub
 * is inert.
 */
#pragma once

#include <apt-pkg/macros.h>

#include <cstddef>
#include <string>

class APT_PUBLIC debDebFile
{
 public:
   debDebFile() = default;
   ~debDebFile() = default;

   class APT_PUBLIC MemControlExtract
   {
    public:
     MemControlExtract() = default;
     ~MemControlExtract() = default;

     bool Read(class pkgDebianFile * /*deb*/) { return false; }
     bool Write(std::string /*file*/) { return false; }
   };
};

class pkgDebianFile
{
};
