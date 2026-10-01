/* apt-pkg-stub/apt-pkg/acquire-item.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg acquire-item classes. The Synaptic UI
 * uses pkgAcqChangelog in rgchangelogdialog.cc (which is excluded from
 * the HAVE_XBPS build) and pkgAcqFile nowhere directly. These classes
 * are declared here for completeness but never instantiated.
 */
#pragma once

#include <apt-pkg/acquire.h>
#include <apt-pkg/macros.h>

#include <string>

class APT_PUBLIC pkgAcqFile : public pkgAcquire::Item
{
 public:
   pkgAcqFile() = default;
   virtual ~pkgAcqFile() = default;
};

class APT_PUBLIC pkgAcqChangelog : public pkgAcquire::Item
{
 public:
   pkgAcqChangelog() = default;
   virtual ~pkgAcqChangelog() = default;

   /* The real class has a static URI-builder used by rgchangelogdialog.
    * The stub returns an empty string. */
   static std::string URI(std::string /*pkg*/,
                          std::string /*ver*/,
                          std::string /*origin*/,
                          std::string /*suite*/,
                          std::string /*component*/) { return ""; }
};
