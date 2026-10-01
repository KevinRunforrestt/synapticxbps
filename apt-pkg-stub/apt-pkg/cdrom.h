/* apt-pkg-stub/apt-pkg/cdrom.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg pkgCdrom / pkgCdromStatus classes.
 * The Synaptic UI uses pkgCdromStatus in rgcdscanner.cc (which is
 * excluded from the HAVE_XBPS build) and pkgCdrom in rcdscanner.cc
 * (also excluded). The stubs are inert no-ops.
 */
#pragma once

#include <apt-pkg/macros.h>

#include <string>
#include <vector>

class APT_PUBLIC pkgCdromStatus
{
 public:
   virtual ~pkgCdromStatus() = default;

   virtual void Update(std::string /*text*/, unsigned int /*current*/ = 0) {}
   virtual void SetTotal(unsigned int /*total*/) {}
   virtual bool ChangeCdrom() { return false; }
   virtual bool AskCdromName(std::string &/*name*/) { return false; }
   virtual bool Status(std::string /*prompt*/, int /*percent*/) { return false; }
};

class APT_PUBLIC pkgCdrom
{
 public:
   pkgCdrom() = default;
   ~pkgCdrom() = default;

   bool Add(pkgCdromStatus * /*progress*/) { return false; }
   bool Ident(std::string &/*ident*/, pkgCdromStatus * /*progress*/) { return false; }
};
