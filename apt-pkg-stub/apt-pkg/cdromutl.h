/* apt-pkg-stub/apt-pkg/cdromutl.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg cdrom utility functions. The Synaptic
 * UI uses FindCdromDevice, MountCdrom, UnmountCdrom, IdentCdrom and
 * ReduceSourcelist. Under HAVE_XBPS all are no-ops returning false.
 */
#pragma once

#include <apt-pkg/macros.h>

#include <string>
#include <vector>

APT_PUBLIC std::string FindCdromDevice(std::string /*path*/ = "");
APT_PUBLIC bool MountCdrom(std::string /*path*/) { return false; }
APT_PUBLIC bool UnmountCdrom(std::string /*path*/) { return false; }
APT_PUBLIC bool IdentCdrom(std::string /*cd*/, std::string &/*ident*/) { return false; }
APT_PUBLIC bool ReduceSourcelist(std::string /*out*/,
                                std::vector<std::string> &/*list*/) { return false; }
