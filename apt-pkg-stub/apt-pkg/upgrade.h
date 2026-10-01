/* apt-pkg-stub/apt-pkg/upgrade.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg APT::Upgrade helpers. The Synaptic UI
 * calls APT::Upgrade::Upgrade() for the "Mark All Upgrades" action.
 * Under HAVE_XBPS the equivalent is RPackageLister::upgrade(); the
 * stub is a no-op returning true.
 */
#pragma once

#include <apt-pkg/depcache.h>
#include <apt-pkg/macros.h>

namespace APT
{
namespace Upgrade
{
   enum UpgradeMode
   {
      ALLOW_EVERYTHING = 0,
      FORBID_REMOVE_PACKAGES = 1,
      FORBID_INSTALL_NEW_PACKAGES = 2
   };

   APT_PUBLIC bool Upgrade(pkgDepCache & /*cache*/,
                           UpgradeMode /*mode*/ = ALLOW_EVERYTHING);
};
};
