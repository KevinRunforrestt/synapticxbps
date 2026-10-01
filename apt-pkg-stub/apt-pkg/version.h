/* apt-pkg-stub/apt-pkg/version.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg version-compare helpers. The Synaptic
 * UI uses pkgVersionCompare and pkgVersionCheckDep in a few places.
 * Under HAVE_XBPS the version comparisons are done by xbps_cmpver
 * (called from rpackagelister_xbps.cc), so these stubs just return 0
 * (equal) — they are never actually called.
 */
#pragma once

#include <apt-pkg/macros.h>

#include <string>

APT_PUBLIC int pkgVersionCompare(const std::string & /*a*/, const std::string & /*b*/);
APT_PUBLIC int pkgVersionCompare(const char * /*a*/, const char * /*b*/);

APT_PUBLIC bool pkgVersionCheckDep(const std::string & /*pkg_ver*/,
                                   const std::string & /*dep_op*/,
                                   const std::string & /*dep_ver*/);
