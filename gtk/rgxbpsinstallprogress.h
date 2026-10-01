/* rgxbpsinstallprogress.h - XBPS install progress (stub)
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * The real install-progress implementation lives in
 * common/rpackagelister_xbps.cc::commitChanges. This header is
 * intentionally empty — it exists so any future code that wants to
 * include "rgxbpsinstallprogress.h" compiles cleanly under both
 * the APT and the XBPS backend.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#pragma once

#include "config.h" // IWYU pragma: associated

#ifdef HAVE_XBPS

/* No symbols — the install progress is integrated into commitChanges
 * in common/rpackagelister_xbps.cc. We deliberately do NOT include
 * rginstallprogress.h here because that header transitively pulls in
 * <apt-pkg/packagemanager.h>, which is not available in the XBPS
 * build environment. */

#endif /* HAVE_XBPS */
