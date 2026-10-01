/* rpackage.h - wrapper for accessing package information
 *
 * Copyright (c) 2000, 2001 Conectiva S/A
 *               2002 Michael Vogt <mvo@debian.org>
 *               2025 Synaptic-XBPS port for Void Linux
 *
 * Author: Alfredo K. Kojima <kojima@conectiva.com.br>
 *         Michael Vogt <mvo@debian.org>
 *
 * Portions Taken from Gnome APT
 *   Copyright (C) 1998 Havoc Pennington <hp@pobox.com>
 *
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

#include "i18n.h"

#include <cstring>
#include <string>
#include <utility>
#include <vector>

#ifdef HAVE_XBPS
/* ----------------------------------------------------------------
 * XBPS backend — NO libxbps API.
 *
 * The XBPS backend stores every piece of package metadata as plain
 * std::string fields and feeds every query through `popen()` to the
 * xbps-query / xbps-install / xbps-remove command-line tools. This
 * keeps the binary free of any libxbps linkage and avoids the known
 * crashes inside libxbps 0.60.7 (xbps_pkgdb_get_pkg_files,
 * xbps_pkgdb_get_pkg_revdeps, xbps_object_type on keysym objects).
 *
 * The public RPackage API is unchanged from the APT branch so the
 * UI layer (gtk/) does not need to know which backend is active.
 * ------------------------------------------------------------ */
#else
#include <apt-pkg/pkgcache.h>
#endif

class RPackageLister;
#ifdef HAVE_XBPS
/* No libxbps handle is owned or borrowed — queries go through popen. */
#else
class pkgAcquire;
class pkgDepCache;
class pkgRecords;
#endif

enum { NO_PARSER, DEB_PARSER, STRIP_WS_PARSER, RPM_PARSER };

/* ------------------------------------------------------------------ */
/* Dependency-type enumeration (Synaptic-owned)                       */
/*                                                                    */
/* The APT and XBPS backends translate from their native enums into   */
/* RDepType. This keeps the public API free of libapt-pkg / libxbps   */
/* types.                                                             */
/* ------------------------------------------------------------------ */
enum RDepType {
   RDEP_NONE = 0,
   RDEP_DEPENDS,    /* "Depends"   */
   RDEP_PREDEPENDS, /* "PreDepends" (APT-only; XBPS has no equivalent) */
   RDEP_SUGGESTS,   /* "Suggests"  (APT-only)                            */
   RDEP_RECOMMENDS, /* "Recommends" (APT-only)                           */
   RDEP_CONFLICTS,  /* "Conflicts" */
   RDEP_REPLACES,   /* "Replaces"  */
   RDEP_OBSOLETES,  /* "Obsoletes" (APT-only — XBPS uses Replaces)       */
   RDEP_BREAKS,     /* "Breaks"    (APT-only)                            */
   RDEP_ENHANCES,   /* "Enhances"  (APT-only)                            */
   RDEP_DEPENDENCY_OF /* last — for the "Dependency of" string */
};

// Translated dependency-type names. Index by RDepType. The array is
// indexed by the APT values when HAVE_XBPS is *not* defined so we
// keep ABI compatibility with any external code that still uses the
// old enum.
#ifdef HAVE_XBPS
static const char *DepTypeStr[] = {
   "",
   _("Depends"),
   _("PreDepends"),
   _("Suggests"),
   _("Recommends"),
   _("Conflicts"),
   _("Replaces"),
   _("Obsoletes"),
   _("Breaks"),
   _("Enhances"),
   /* padding to keep the array the same size as the APT version */
   "",
   "",
   "",
   "",
   "",
   // make sure this is always the last member
   _("Dependency of"),
};
#else
// taken from apt (pkgcache.cc) to make our life easier
// (and added "RDepends" as last element)
static const char *DepTypeStr[] = {
   "",
   _("Depends"),
   _("PreDepends"),
   _("Suggests"),
   _("Recommends"),
   _("Conflicts"),
   _("Replaces"),
   _("Obsoletes"),
   _("Breaks"),
   _("Enhances"),
   /* padding */
   "",
   "",
   "",
   "",
   "",
   // make sure this is always the last member
   _("Dependency of"),
};
#endif

typedef struct
{
#ifdef HAVE_XBPS
   RDepType type; // Synaptic-owned enum (no APT coupling)
#else
   pkgCache::Dep::DepType type; // type as enum (APT)
#endif
   const char *name;            // target pkg name
   const char *version;         // target version
   const char *versionComp;     // target version compare type ( << , > etc)
   bool isSatisfied;            // dependecy is satified
   bool isVirtual;              // package is virtual
   bool isOr;                   // or dependency (with next pkg)
} DepInformation;


class RPackage
{

 public:
   RPackageLister *_lister;

   /* RPackageLister::openCache() needs to set _is_outdated (and a
    * few other fields) directly. Friend-declaring it here keeps the
    * fields protected while avoiding a public setter for every one
    * of them. */
   friend class RPackageLister;

 protected:
   std::string fullname;
#ifdef HAVE_XBPS
   /* ----------------------------------------------------------------
    * XBPS backend — every metadata field is a plain std::string,
    * populated by RPackageLister::openCache() from xbps-query -l /
    * xbps-query -Rs - output and then refined lazily by per-method
    * popen() calls (enumDeps, installedFiles, getRawRecord, etc.).
    * No libxbps handle is held by RPackage.
    *
    * RPackageLister is declared a friend below so it can set the
    * cached fields directly during openCache() without needing a
    * public setter for every one of them.
    * -------------------------------------------------------------- */
   std::string _name;            /* pkgname        */
   std::string _pkgver;          /* "name-version_rev"            */
   std::string _version;         /* version-only slice of _pkgver */
   std::string _available_version;  /* repo version (from xbps-query -R) */
   std::string _summary;         /* short_desc      */
   std::string _description;     /* long_desc       */
   std::string _homepage;
   std::string _maintainer;
   std::string _license;
   std::string _arch;
   std::string _origin;          /* repository URL  */
   std::string _section;         /* XBPS has no section; we synthesize */
   long _installed_size;
   long _available_size;
   bool _is_installed;
   bool _is_outdated;
   bool _is_auto;
   bool _is_hold;

   /* Lazy cache of `xbps-query <name>` (installed) or `xbps-query -R
    * <name>` (remote) full output. Used by findTagFromPkgRecord() so
    * we don't fork one xbps-query process per field access. Invalidated
    * whenever the package state changes (setInstall / setRemove / …)
    * by _invalidateInfoCache(). */
   std::string _info_cache;
   bool _info_cache_valid;

   /* Cached dependency list — `xbps-query -x` (or `-Rx` for remote)
    * output parsed into DepInformation. First call to enumDeps()
    * populates this; subsequent calls reuse it so switching between
    * the "Dependencies" and "Available versions" tabs in the details
    * pane doesn't refork xbps-query. */
   std::vector<DepInformation> _deps_cache;
   bool _deps_cache_valid;

   /* Cached installed-files list — `xbps-query -f` output, one path
    * per line. First call to installedFiles() populates this. */
   std::string _files_cache;
   bool _files_cache_valid;
#else
   pkgRecords *_records;
   pkgDepCache *_depcache;
   pkgCache::PkgIterator *_package;
#endif

   // save the default candidate version to undo version selection
   std::string _defaultCandVer;

   bool _notify;

   // Virtual pkgs provided by this one.
   // FIXME: broken right now
   // bool isShallowDependency(RPackage *pkg);
   int _boolFlags;

 public:
   enum Flags {
      FKeep = 1 << 0,
      FInstall = 1 << 1,
      FNewInstall = 1 << 2,
      FReInstall = 1 << 3,
      FUpgrade = 1 << 4,
      FDowngrade = 1 << 5,
      FRemove = 1 << 6,
      FHeld = 1 << 7,
      FInstalled = 1 << 8,
      FOutdated = 1 << 9,
      FNowBroken = 1 << 10,
      FInstBroken = 1 << 11,
      FOrphaned = 1 << 12,
      FPinned = 1 << 13,
      FNew = 1 << 14,
      FResidualConfig = 1 << 15,
      FNotInstallable = 1 << 16,
      FPurge = 1 << 17,
      FImportant = 1 << 18,
      FOverrideVersion = 1 << 19,
      FIsAuto = 1 << 20,
      FIsGarbage = 1 << 21,
      FNowPolicyBroken = 1 << 22,
      FInstPolicyBroken = 1 << 23,
   };

   enum UpdateImportance { IUnknown, INormal, ICritical, ISecurity };

   /* ----------------------------------------------------------------
    * Back-end-agnostic opaque package handle.
    *
    * Under APT this returns the raw PkgIterator (kept for back-compat
    * with any code that hasn't been ported yet). Under XBPS this
    * always returns NULL — there is no opaque handle to expose
    * because we never hold a libxbps dictionary.
    * -------------------------------------------------------------- */
#ifdef HAVE_XBPS
   void *package()
   {
      return NULL;
   }
#else
   pkgCache::PkgIterator *package()
   {
      return _package;
   }
#endif

   const char *name();

   const char *section();
   const char *priority();

   const char *summary();
   const char *description();

#ifndef HAVE_RPM
   std::string installedFiles();
#endif

   std::string arch();

   // package is also available for the native architecture
   // (note that packages installed are never considered a duplicate
   bool isMultiArchDuplicate();

   // get changelog file from the debian server
   // (XBPS has no centralised changelog server; this fetches the
   //  package's optional `changelog` URL via xbps-query + curl)
#ifdef HAVE_XBPS
   std::string getChangelogFile(void *fetcher_unused);
   std::string getScreenshotFile(void *fetcher_unused, bool thumb = true);
#else
   std::string getChangelogFile(pkgAcquire *fetcher);
   std::string getScreenshotFile(pkgAcquire *fetcher, bool thumb = true);
#endif

   std::vector<std::string> provides();

   // get all available versions (version, release)
   std::vector<std::pair<std::string, std::string>> getAvailableVersions();

   // get origins url of the package (e.g. http://security.ubuntu.com)
   std::vector<std::string> getCandidateOriginSiteUrls();
   // get origin "archive" release header (e.g. karmic, karmic-updates)
   std::vector<std::string> getCandidateOriginSuites();
   // get origin "origin" release header (e.g. Ubuntu,
   std::string getCandidateOriginStr();

   // get the release file for the givel origin label string
   std::string getReleaseFileForOrigin(std::string label, std::string release);

   // get installed component (like main, contrib, non-free)
   std::string component();

   // get label of download site
   std::string label();

   // get origin (Origin tag from the release file)
   std::string origin();

   const char *maintainer();
   const char *homepage();
   const char *vendor();

   const char *installedVersion();
   long installedSize();

   // get tag from pkg record
   std::string findTagFromPkgRecord(const char *tag);

   // get the raw package record
   std::string getRawRecord(bool useCandidateVersion = true);

   // sourcepkg
   const char *srcPackage();

   // relative to version that would be installed
   const char *availableVersion();
#ifdef HAVE_XBPS
   /* No VerIterator under XBPS — exposed as a void* so callers that
    * still use this API can be ported incrementally. Always NULL. */
   void *availableVersionIter();
#else
   pkgCache::VerIterator availableVersionIter();
#endif
   long availableInstalledSize();
   long availablePackageSize();

   // does the pkg depends on this one?
   bool dependsOn(const char *pkgname);

   // getDeps of installed pkg
   std::vector<DepInformation> enumDeps(bool useCanidateVersion = false);

   // reverse dependencies
   std::vector<DepInformation> enumRDeps();

   int getFlags();

   bool wouldBreak();

   bool isTrusted();

   void setKeep();
   void setInstall();
   void setReInstall(bool flag);
   void setRemove(bool purge = false);

   void setPinned(bool flag);

   void setNew(bool flag = true)
   {
      _boolFlags = flag ? (_boolFlags | FNew) : (_boolFlags & ~FNew);
   }
   void setOrphaned(bool flag = true)
   {
      _boolFlags = flag ? (_boolFlags | FOrphaned) : (_boolFlags & ~FOrphaned);
   }

   // set/unset the auto-installed flag
   void setAuto(bool flag = true);

   void setNotify(bool flag = true);

   // Shallow doesnt remove things other pkgs depend on.
   void setRemoveWithDeps(bool shallow, bool purge = false);

   // mainpulate the candiate version
   bool setVersion(std::string verTag);
   void unsetVersion();
   std::string showWhyInstBroken();

   // Custom sort comparator to be used with std::sort.
   inline static bool byNameAscending(RPackage *a, RPackage *b)
   {
      return strcmp(a->name(), b->name()) < 0;
   };

#ifdef HAVE_XBPS
   RPackage(RPackageLister *lister,
            const std::string &name,
            const std::string &pkgver,
            const std::string &summary,
            const std::string &description,
            bool installed);
#else
   RPackage(RPackageLister *lister,
            pkgDepCache *depcache,
            pkgRecords *records,
            pkgCache::PkgIterator &pkg);
#endif
   ~RPackage();

   // Pre-fetch all slow fields (info dump, deps, files) off the GTK
   // main thread. The details pane calls this from a worker thread so
   // the UI doesn't freeze while xbps-query runs. When done, the
   // GTK-thread fillInValues call hits the per-package cache (zero
   // fork). This is the iruka-xbps showPackageInfo pattern.
#ifdef HAVE_XBPS
   void prefetchDetails();
#endif

 private:
   std::string getChangelogURI();

#ifdef HAVE_XBPS
   /* Helper used by enumDeps() — parses a single dependency string
    * like "foo>=1.0_1" or "foo-1.0_1" into (name, comp, version). */
   static void _parse_dep(const std::string &dep,
                          std::string &name,
                          std::string &version_comp,
                          std::string &version);
   /* Helper used by enumDeps() — runs a single xbps-query call
    * through popen and appends each parsed line to the result. */
   void _enumDepsFromPopen(const char *xbps_args,
                           RDepType dep_type,
                           std::vector<DepInformation> &out);
   /* Lazily fetches `xbps-query <name>` (or `xbps-query -R <name>`
    * for non-installed) into _info_cache, so all findTagFromPkgRecord
    * and getRawRecord calls on the same RPackage share a single
    * fork. */
   void _ensureInfoCache();
   /* Invalidates _info_cache — call when the package's installed /
    * remote state changes (currently unused, reserved for future
    * re-fetch-after-commit support). */
   void _invalidateInfoCache();
#endif
};
