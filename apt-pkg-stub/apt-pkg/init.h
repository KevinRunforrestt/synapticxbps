/* apt-pkg-stub/apt-pkg/init.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg init functions. Synaptic's main()
 * calls pkgInitConfig(*_config) and pkgInitSystem(*_config, _system)
 * before doing anything else. Under HAVE_XBPS these are no-ops.
 */
#pragma once

#include <apt-pkg/configuration.h>
#include <apt-pkg/macros.h>
#include <apt-pkg/pkgsystem.h>

APT_PUBLIC bool pkgInitConfig(Configuration & /*config*/);
APT_PUBLIC bool pkgInitSystem(Configuration & /*config*/, pkgSystem *& /*system*/);
