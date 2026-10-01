/* apt-pkg-stub/apt-pkg/metaindex.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg metaIndex class. The Synaptic UI uses
 * metaIndex's GetOrigin, GetLabel, GetSuite, GetDist and GetComponents
 * in rpackagelister.cc and in rgmainwindow.cc. Under HAVE_XBPS these
 * all return empty strings.
 */
#pragma once

#include <apt-pkg/macros.h>

#include <string>
#include <vector>

class APT_PUBLIC metaIndex
{
 public:
   metaIndex() = default;
   virtual ~metaIndex() = default;

   virtual std::string GetOrigin() const { return ""; }
   virtual std::string GetLabel() const { return ""; }
   virtual std::string GetSuite() const { return ""; }
   virtual std::string GetDist() const { return ""; }
   virtual std::vector<std::string> GetComponents() const { return {}; }
};
