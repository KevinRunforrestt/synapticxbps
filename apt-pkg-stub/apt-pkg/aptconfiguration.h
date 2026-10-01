/* apt-pkg-stub/apt-pkg/aptconfiguration.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg APT::Configuration class. The Synaptic
 * UI uses APT::Configuration::getArchitectures, getLanguages and
 * getCompressors in a few places. Under HAVE_XBPS these return empty
 * vectors.
 */
#pragma once

#include <apt-pkg/macros.h>

#include <string>
#include <vector>

namespace APT
{
class APT_PUBLIC Configuration
{
 public:
   static std::vector<std::string> getArchitectures(bool /*frozen*/ = false) { return {}; }
   static std::vector<std::string> getLanguages(bool /*all*/ = false,
                                                bool /*frozen*/ = true) { return {}; }
   static std::vector<std::string> getCompressors(bool /*frozen*/ = true) { return {}; }
   static std::string getLocale() { return ""; }
};
};
