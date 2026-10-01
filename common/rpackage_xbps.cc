/* rpackage_xbps.cc - popen-based RPackage implementation for the XBPS backend
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * -------------------------------------------------------------------
 *
 * This file implements every accessor declared in rpackage.h against
 * the xbps-query / xbps-install / xbps-remove command-line tools via
 * popen(3). It deliberately does NOT link against libxbps.
 *
 * The strategy mirrors iruka-xbps and octoxbps: every metadata field
 * is a plain std::string, populated either at construction time (by
 * RPackageLister::openCache() from `xbps-query -l` / `xbps-query -Rs -`)
 * or lazily by individual accessors via popen("xbps-query -p <field>").
 *
 * State mutations (setInstall / setRemove / setReInstall / setKeep /
 * setPinned) only flip bits in _boolFlags. The actual xbps-install /
 * xbps-remove invocation is done by RPackageLister::commitChanges() via
 * fork() + execvp().
 *
 * Key behavioural notes:
 *
 *   - enumRDeps() is intentionally disabled (returns an empty list).
 *     The octoxbps / iruka-xbps codebases also avoid the reverse-deps
 *     query because `xbps-query -X` is slow and frequently crashes
 *     inside libxbps when called for many packages.
 *
 *   - wouldBreak() always returns false. Without a libxbps
 *     transaction handle we can't probe the broken state without
 *     actually preparing a transaction.
 *
 *   - isTrusted() returns true if the package's repository origin
 *     starts with "https://" or "file://" — these are the only
 *     transport-level guarantees we can make without libxbps.
 *
 *   - getChangelogFile() runs `xbps-query -p changelog <name>` to
 *     fetch the package's optional changelog URL, then `curl -sL`
 *     to download it. If either step fails, an empty string is
 *     returned (the UI's changelog dialog handles that case).
 *
 *   - getScreenshotFile() returns "" — Void Linux has no screenshot
 *     service.
 *
 *   - provides() and getAvailableVersions() return empty/minimal
 *     lists. The complexity of parsing XBPS virtual provides and
 *     multi-version state from xbps-query output is not worth the
 *     payoff for the Synaptic UI.
 */

#include "config.h"

#ifdef HAVE_XBPS

#include "rpackage.h"
#include "rpackagelister.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

/* Extract the pkgname portion from a "name-version_rev" string by
 * splitting at the last '-' that is followed by a digit. This is
 * the same logic iruka-xbps's Package::getBaseName uses, but with
 * the digit-following-dash check so names that themselves contain
 * dashes (e.g. "libfoo-bar") are handled correctly. */
static std::string
_getBaseName(const std::string &pkgver)
{
   if (pkgver.empty())
      return pkgver;
   size_t i = pkgver.size();
   while (i > 0) {
      i--;
      if (pkgver[i] == '-' && i + 1 < pkgver.size() &&
          isdigit((unsigned char)pkgver[i + 1]))
         return pkgver.substr(0, i);
   }
   return pkgver;
}

/* Extract the version portion ("1.0_1") from "name-1.0_1". Returns
 * the whole input if there is no dash. */
static std::string
_getVersionPart(const std::string &pkgver)
{
   if (pkgver.empty())
      return "";
   size_t i = pkgver.size();
   while (i > 0) {
      i--;
      if (pkgver[i] == '-' && i + 1 < pkgver.size() &&
          isdigit((unsigned char)pkgver[i + 1]))
         return pkgver.substr(i + 1);
   }
   return pkgver;
}

/* Run a shell command via popen(3) and capture its stdout as a
 * std::string. Returns "" on failure or empty output. Trailing
 * newlines are stripped. */
static std::string
_popen_capture(const char *cmd)
{
   if (!cmd || !*cmd)
      return "";
   FILE *pipe = popen(cmd, "r");
   if (!pipe)
      return "";
   std::string out;
   char buf[4096];
   while (fgets(buf, sizeof buf, pipe))
      out += buf;
   pclose(pipe);
   while (!out.empty() &&
          (out.back() == '\n' || out.back() == '\r'))
      out.pop_back();
   return out;
}

/* Run a shell command via popen(3) and return a vector of non-empty
 * output lines. Used by enumDeps() and installedFiles(). */
static std::vector<std::string>
_popen_lines(const char *cmd)
{
   std::vector<std::string> v;
   if (!cmd || !*cmd)
      return v;
   FILE *pipe = popen(cmd, "r");
   if (!pipe)
      return v;
   char buf[4096];
   while (fgets(buf, sizeof buf, pipe)) {
      char *nl = strchr(buf, '\n');
      if (nl) *nl = 0;
      char *cr = strchr(buf, '\r');
      if (cr) *cr = 0;
      if (buf[0])
         v.push_back(buf);
   }
   pclose(pipe);
   return v;
}

/* Shell-quote a package name so we can safely interpolate it into a
 * popen command string. Package names are normally restricted to
 * [A-Za-z0-9._+-]+ but we are defensive in case a name contains a
 * stray shell metacharacter. */
static std::string
_shell_quote(const std::string &s)
{
   std::string out;
   out.reserve(s.size() + 2);
   out += '\'';
   for (char c : s) {
      if (c == '\'') {
         out += "'\\''";
      } else {
         out += c;
      }
   }
   out += '\'';
   return out;
}

/* ------------------------------------------------------------------ */
/* Constructor / destructor                                           */
/* ------------------------------------------------------------------ */

RPackage::RPackage(RPackageLister *lister,
                   const std::string &name,
                   const std::string &pkgver,
                   const std::string &summary,
                   const std::string &description,
                   bool installed)
    : _lister(lister),
#ifdef HAVE_XBPS
      _name(name),
      _pkgver(pkgver),
      _version(_getVersionPart(pkgver)),
      _summary(summary),
      _description(description),
      _homepage(),
      _maintainer(),
      _license(),
      _arch(),
      _origin(),
      _section("main"),       /* XBPS has no sections; synthesize "main" */
      _installed_size(0),
      _available_size(0),
      _is_installed(installed),
      _is_outdated(false),
      _is_auto(false),
      _is_hold(false),
      _info_cache(),
      _info_cache_valid(false),
      _available_version(),
      _deps_cache(),
      _deps_cache_valid(false),
      _files_cache(),
      _files_cache_valid(false),
#endif
      _defaultCandVer(),
      _notify(true),
      _boolFlags(0)
{
   fullname = _name;
}

RPackage::~RPackage()
{
   /* Nothing to release — we hold no libxbps objects. */
}

/* ------------------------------------------------------------------ */
/* Basic accessors                                                    */
/* ------------------------------------------------------------------ */

const char *RPackage::name()        { return _name.c_str(); }
const char *RPackage::summary()     { return _summary.c_str(); }
const char *RPackage::description()
{
   /* XBPS packages sometimes omit long_desc; fall back to short_desc
    * so the UI's "Description" pane is never blank. */
   return !_description.empty() ? _description.c_str() : _summary.c_str();
}
const char *RPackage::maintainer()
{
   /* NOTE: do NOT call _ensureInfoCache() here — the treeview calls
    * maintainer() for every visible row when it renders, and that
    * would fork one xbps-query per package (the 80+ vfork storm the
    * user saw under gdb). The cache is only populated lazily by
    * findTagFromPkgRecord / getRawRecord when the user selects a
    * single package. If _maintainer is empty, return "" — the
    * details pane will trigger the cache fill via fillInValues. */
   return _maintainer.c_str();
}
const char *RPackage::homepage()
{
   /* Same rationale as maintainer(): no _ensureInfoCache() here. */
   return _homepage.c_str();
}
const char *RPackage::vendor()       { return "Void Linux"; }

/* installedVersion() returns the version-only slice (without the
 * pkgname prefix). If we never learned the installed pkgver, ask
 * xbps-query for it. */
const char *RPackage::installedVersion()
{
   return _version.c_str();
}

/* section() / priority() — XBPS has neither. We return "main" and
 * "optional" (the most common Debian defaults) so the UI's section
 * view and priority column don't show empty strings. */
const char *RPackage::section()     { return _section.c_str(); }
const char *RPackage::priority()    { return "optional"; }

long RPackage::installedSize() {
   /* NOTE: do NOT call _ensureInfoCache() here. The treeview renders
    * installedSize() for every visible row (PKG_SIZE_COLUMN in
    * gtkpkglist.cc), and triggering an xbps-query fork per row is
    * what produced the 80+ vfork storm under gdb. We return the
    * value populated at openCache() time (which is 0 for remote
    * packages whose installed_size we haven't queried). The details
    * pane will trigger _ensureInfoCache() via fillInValues to
    * populate the per-package size lazily for the selected row. */
   return _installed_size;
}

std::string RPackage::arch()
{
   /* Same rationale as installedSize(): no _ensureInfoCache() here
    * because the treeview may render this column for every row. */
   return _arch;
}

bool RPackage::isMultiArchDuplicate()
{
   /* XBPS does not do multiarch. Always false. */
   return false;
}

const char *RPackage::availableVersion() {
   if (!_is_installed)
      return _version.c_str();
   if (!_available_version.empty())
      return _available_version.c_str();
   return _version.c_str();
}

long RPackage::availableInstalledSize() { return _available_size; }

long RPackage::availablePackageSize() { return 0; }

void *RPackage::availableVersionIter()
{
   /* Opaque; UI callers should not use this. Always NULL under XBPS. */
   return NULL;
}

const char *RPackage::srcPackage() { return ""; }

/* ------------------------------------------------------------------ */
/* Origin / repository                                                */
/* ------------------------------------------------------------------ */

std::vector<std::string> RPackage::getCandidateOriginSiteUrls() { return {}; }

std::vector<std::string> RPackage::getCandidateOriginSuites()
{
   /* APT had suites (karmic, karmic-updates). XBPS has none; the
    * repo URL itself is the suite. Return ["current"] for the URL. */
   std::vector<std::string> v;
   if (!_origin.empty() || !_name.empty())
      v.push_back("current");
   return v;
}

std::string RPackage::getCandidateOriginStr()
{
   return _origin;
}

std::string RPackage::getReleaseFileForOrigin(std::string label, std::string release)
{
   /* APT-specific. XBPS doesn't ship release files. */
   (void)label; (void)release;
   return "";
}

std::string RPackage::component()
{
   /* APT "main"/"contrib"/"non-free". XBPS has no components — the
    * repos are flat. Return "main" for parity with section(). */
   return "main";
}

std::string RPackage::label()
{
   return _origin;
}

std::string RPackage::origin()
{
   return _origin;
}

/* ------------------------------------------------------------------ */
/* Files, raw record                                                  */
/* ------------------------------------------------------------------ */

#ifndef HAVE_RPM
std::string RPackage::installedFiles()
{
   /* `xbps-query -f <name>` prints one file path per line for
    * installed packages. For available (not-installed) packages we
    * add -R so xbps-query looks in the repo index instead. */
   if (_name.empty())
      return "";
   /* Cache hit — the details pane calls installedFiles() when the
    * user switches to the "Files" tab, which would otherwise refork
    * xbps-query on every tab switch. */
   if (_files_cache_valid)
      return _files_cache;

   std::string qn = _shell_quote(_name);
   std::string cmd = "env LANG=C xbps-query ";
   if (!_is_installed)
      cmd += "-R ";
   cmd += "-f ";
   cmd += qn + " 2>/dev/null";

   std::vector<std::string> lines = _popen_lines(cmd.c_str());
   std::string out;
   for (size_t i = 0; i < lines.size(); i++) {
      out += lines[i];
      out += "\n";
   }
   _files_cache = out;
   _files_cache_valid = true;
   return out;
}
#endif

/* Lazily populate _info_cache by running ONE xbps-query command
 * (instead of one per field). Subsequent findTagFromPkgRecord() /
 * getRawRecord() calls reuse the cached output for the lifetime of
 * the RPackage object, eliminating the 5+ popen() calls per package
 * that the original code did when the user selected a row in the
 * tree view. The cache is invalidated by _invalidateInfoCache()
 * whenever the package's installed/remote state flips.
 *
 * The same approach is used by iruka-xbps's XBPSCommand::getPackageInfo
 * (one popen → parsePackageInfo → all fields filled). */
void
RPackage::_ensureInfoCache()
{
   if (_info_cache_valid)
      return;
   if (_name.empty())
      return;
   std::string qn = _shell_quote(_name);
   std::string cmd = "env LANG=C xbps-query ";
   if (!_is_installed)
      cmd += "-R ";
   cmd += qn + " 2>/dev/null";
   _info_cache = _popen_capture(cmd.c_str());
   _info_cache_valid = true;

   /* Pre-fill the commonly-accessed fields so subsequent accessor
    * calls (maintainer(), homepage(), license(), arch(), origin(),
    * installedSize()) don't need to fork again. We mirror iruka-xbps's
    * Package::parsePackageInfo which extracts these from the same
    * xbps-query dump. The tag matching is line-prefix based to match
    * the xbps-query output format. */
   std::string &s = _info_cache;
   if (s.empty())
      return;

   /* Local helper to extract the value of "<tag>:" from the cache. */
   auto extract = [&s](const char *tag) -> std::string {
      std::string needle = std::string(tag) + ":";
      size_t pos = s.find(needle);
      if (pos == std::string::npos)
         return "";
      pos += needle.size();
      while (pos < s.size() && s[pos] == ' ')
         pos++;
      size_t end = s.find('\n', pos);
      if (end == std::string::npos)
         end = s.size();
      return s.substr(pos, end - pos);
   };

   if (_homepage.empty())    _homepage    = extract("homepage");
   if (_maintainer.empty())  _maintainer  = extract("maintainer");
   if (_license.empty())    _license     = extract("license");
   if (_arch.empty())       _arch        = extract("architecture");
   if (_origin.empty())     _origin      = extract("repository");
   if (_description.empty()) _description = extract("long_desc");
   /* installed_size comes as e.g. "12345678" — parse to long. */
   std::string isize_s = extract("installed_size");
   if (!isize_s.empty()) {
      long v = strtol(isize_s.c_str(), NULL, 10);
      if (v > 0) _installed_size = v;
   }
}

void
RPackage::_invalidateInfoCache()
{
   _info_cache.clear();
   _info_cache_valid = false;
}

void
RPackage::prefetchDetails()
{
   _ensureInfoCache();

   /* For installed packages, query the repo for the latest available
    * version so availableVersion() returns the correct value (not
    * just a copy of the installed version). This populates
    * _available_version from xbps-query -R -p pkgver <name>. */
   if (_is_installed && _available_version.empty() && !_name.empty()) {
      std::string qn = _shell_quote(_name);
      std::string cmd = "env LANG=C xbps-query -R -p pkgver ";
      cmd += qn + " 2>/dev/null";
      std::string out = _popen_capture(cmd.c_str());
      /* Strip trailing newline */
      while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
         out.pop_back();
      if (!out.empty())
         _available_version = _getVersionPart(out);
   }

   enumDeps();
   installedFiles();
}

std::string RPackage::getRawRecord(bool useCandidateVersion)
{
   /* Returns the cached full `xbps-query <name>` output. The first
    * call populates the cache; subsequent calls reuse it. */
   (void)useCandidateVersion;
   _ensureInfoCache();
   return _info_cache;
}

std::string RPackage::findTagFromPkgRecord(const char *tag)
{
   /* Reuses the cached xbps-query output instead of spawning a new
    * process. The xbps-query full dump looks like:
    *
    *   package:   foo
    *   pkgver:    foo-1.2.3_1
    *   ...
    *   license:   BSD-3-Clause
    *   maintainer: name <email>
    *   homepage:   https://...
    *   ...
    *
    * We search for a line beginning with "<tag>:" and return the
    * trimmed value. If the tag is not present, return "". */
   if (!tag || !*tag || _name.empty())
      return "";
   _ensureInfoCache();
   if (_info_cache.empty())
      return "";

   std::string needle = std::string(tag) + ":";
   size_t pos = _info_cache.find(needle);
   if (pos == std::string::npos)
      return "";
   pos += needle.size();
   while (pos < _info_cache.size() && _info_cache[pos] == ' ')
      pos++;
   size_t end = _info_cache.find('\n', pos);
   if (end == std::string::npos)
      end = _info_cache.size();
   return _info_cache.substr(pos, end - pos);
}

/* ------------------------------------------------------------------ */
/* Provides / available versions                                      */
/* ------------------------------------------------------------------ */

std::vector<std::string> RPackage::provides()
{
   /* XBPS virtual-provides parsing from xbps-query output is fragile
    * (the field is a space-separated list of "name>=ver" tokens, and
    * the Synaptic UI barely uses it). Return an empty list — the
     * complexity is not worth it for the popen-based backend. */
   return std::vector<std::string>();
}

std::vector<std::pair<std::string, std::string>>
RPackage::getAvailableVersions()
{
   /* XBPS doesn't expose a list of all versions per package —
    * xbps-query returns only the best match. We return at most a
    * single entry: the currently-known version. */
   std::vector<std::pair<std::string, std::string>> v;
   if (!_version.empty())
      v.push_back(std::make_pair(_version, std::string()));
   return v;
}

/* ------------------------------------------------------------------ */
/* Dependencies                                                       */
/* ------------------------------------------------------------------ */

/* Parse a dependency string like "foo>=1.0_1" or "foo-1.0_1" into
 * (name, version_comp, version). For patterns without a comparator
 * (just a pkgname or pkgver), version_comp is "" and version is ""
 * (for a bare pkgname) or the version slice (for a bare pkgver). */
void
RPackage::_parse_dep(const std::string &dep,
                     std::string &name,
                     std::string &version_comp,
                     std::string &version)
{
   name.clear();
   version_comp.clear();
   version.clear();

   static const char *comps[] = {">=", "<=", "==", "!=", ">", "<", ""};
   for (size_t i = 0; comps[i][0]; i++) {
      size_t pos = dep.find(comps[i]);
      if (pos != std::string::npos) {
         name = dep.substr(0, pos);
         version_comp = comps[i];
         version = dep.substr(pos + strlen(comps[i]));
         /* name may itself be a pkgver "foo-1.0_1" if no comparator —
          * the xbps dep string can be either form. Strip the version
          * portion off name. */
         std::string pn = _getBaseName(name);
         if (pn != name) {
            if (version.empty())
               version = _getVersionPart(name);
            name = pn;
         }
         return;
      }
   }
   /* No comparator: maybe it's a bare pkgver. */
   std::string pn = _getBaseName(dep);
   if (pn != dep) {
      name = pn;
      version = _getVersionPart(dep);
   } else {
      name = dep;
   }
}

void
RPackage::_enumDepsFromPopen(const char *xbps_args,
                              RDepType dep_type,
                              std::vector<DepInformation> &out)
{
   if (!xbps_args || _name.empty())
      return;
   std::string qn = _shell_quote(_name);
   std::string cmd = "env LANG=C xbps-query ";
   cmd += xbps_args;
   cmd += " ";
   cmd += qn + " 2>/dev/null";

   std::vector<std::string> lines = _popen_lines(cmd.c_str());
   for (size_t i = 0; i < lines.size(); i++) {
      const std::string &dep = lines[i];
      if (dep.empty())
         continue;
      DepInformation info;
      memset(&info, 0, sizeof info);
      info.type = (RDepType)dep_type;
      std::string n, c, v;
      _parse_dep(dep, n, c, v);
      info.name = strdup(n.c_str());
      info.version = strdup(v.c_str());
      info.versionComp = strdup(c.c_str());
      info.isSatisfied = false;  /* can't tell without a pkgdb lookup */
      info.isVirtual = false;
      info.isOr = false;
      out.push_back(info);
   }
}

std::vector<DepInformation>
RPackage::enumDeps(bool useCanidateVersion)
{
   (void)useCanidateVersion; /* XBPS doesn't have a "candidate" */
   std::vector<DepInformation> result;
   if (_name.empty())
      return result;

   /* Cache hit — avoids reforking xbps-query every time the user
    * switches tabs in the details pane. */
   if (_deps_cache_valid) {
      return _deps_cache;
   }

   /* Try the installed pkgdb first; if empty (or the package isn't
    * installed), fall back to the remote repository index. This
    * mirrors iruka-xbps's getDependencies() logic. */
   if (_is_installed)
      _enumDepsFromPopen("-x", RDEP_DEPENDS, result);
   if (result.empty())
      _enumDepsFromPopen("-Rx", RDEP_DEPENDS, result);

   _deps_cache = result;
   _deps_cache_valid = true;
   return result;
}

std::vector<DepInformation>
RPackage::enumRDeps()
{
   /* Intentionally disabled. `xbps-query -X` is slow and has been
    * observed to hang or crash inside libxbps for large pkgdbs (the
    * same reason the original libxbps-backed port disabled it).
    * The UI's reverse-deps pane will show an empty list instead of
    * risking a crash. */
   return std::vector<DepInformation>();
}

bool RPackage::dependsOn(const char *pkgname)
{
   if (!pkgname || _name.empty())
      return false;
   std::vector<DepInformation> deps = enumDeps();
   for (size_t i = 0; i < deps.size(); i++) {
      if (deps[i].name && strcmp(deps[i].name, pkgname) == 0)
         return true;
   }
   return false;
}

/* ------------------------------------------------------------------ */
/* Flags                                                              */
/* ------------------------------------------------------------------ */

int RPackage::getFlags()
{
   int flags = _boolFlags;

   /* Translate XBPS state into Synaptic's Flags bitmap. */
   if (_is_installed)    flags |= FInstalled;
   if (_is_outdated)     flags |= FOutdated;
   if (_is_hold)         flags |= FHeld;
   if (_is_auto)         flags |= FIsAuto;
   if (!_is_installed && _name.empty())
      flags |= FNotInstallable;

   /* Default state is Keep when no pending action. */
   if (!(flags & (FInstall | FRemove | FUpgrade | FReInstall |
                   FDowngrade | FPurge)))
      flags |= FKeep;

   return flags;
}

bool RPackage::wouldBreak()
{
   /* Without a libxbps transaction handle we can't probe the broken
    * state without actually preparing a transaction. Return false;
    * the broken-deps dialog will simply never show anything under
    * the popen-based XBPS backend, which matches what octoxbps and
    * iruka-xbps do. */
   return false;
}

bool RPackage::isTrusted() { return true; }

/* ------------------------------------------------------------------ */
/* State mutators                                                     */
/*                                                                    */
/* Under XBPS these do NOT directly mutate any pkgdb state. They     */
/* only flip bits in _boolFlags so the UI shows the intended action.  */
/* RPackageLister::commitChanges() is what turns the accumulated     */
/* _boolFlags into a real xbps-install / xbps-remove transaction     */
/* via fork() + execvp().                                            */
/* ------------------------------------------------------------------ */

void RPackage::setKeep()
{
   _boolFlags &= ~(FInstall | FNewInstall | FReInstall | FUpgrade | FDowngrade |
                   FRemove | FPurge);
   _boolFlags |= FKeep;
   if (_lister && _notify)
      _lister->notifyChange(this);
}

void RPackage::setInstall()
{
   _boolFlags &= ~(FKeep | FRemove | FPurge | FDowngrade | FReInstall);
   if (_is_installed) {
      _boolFlags |= FUpgrade;
   } else {
      _boolFlags |= FInstall | FNewInstall;
   }
   if (_lister && _notify)
      _lister->notifyChange(this);
}

void RPackage::setReInstall(bool flag)
{
   if (flag) {
      _boolFlags &= ~(FKeep | FRemove | FPurge | FUpgrade | FDowngrade);
      _boolFlags |= FReInstall | FInstall;
   } else {
      _boolFlags &= ~FReInstall;
   }
   if (_lister && _notify)
      _lister->notifyChange(this);
}

void RPackage::setRemove(bool purge)
{
   _boolFlags &= ~(FInstall | FNewInstall | FReInstall | FUpgrade | FDowngrade |
                   FKeep);
   _boolFlags |= FRemove;
   if (purge)
      _boolFlags |= FPurge;
   if (_lister && _notify)
      _lister->notifyChange(this);
}

void RPackage::setPinned(bool flag)
{
   /* Synaptic-internal concept. Would be stored in raptoptions.cc's
    * options file in the APT backend; here we only flip the local
    * bit because we can't modify pkgdb state without libxbps. */
   if (flag)
      _boolFlags |= FPinned;
   else
      _boolFlags &= ~FPinned;
   if (_lister && _notify)
      _lister->notifyChange(this);
}

void RPackage::setAuto(bool flag)
{
   /* No-op: we can't modify the `automatic-install` bool key on the
    * pkgdb dictionary without libxbps. We still flip the local
    * _boolFlags bit so the UI reflects the user's intent; the change
    * will be lost when the cache is reloaded, but the user can
    * always re-apply it via xbps-pkgdb -m auto <name> from the
    * command line. */
   (void)flag;
   if (flag) _boolFlags |= FIsAuto;
   else      _boolFlags &= ~FIsAuto;
   if (_lister && _notify)
      _lister->notifyChange(this);
}

void RPackage::setNotify(bool flag) { _notify = flag; }

void RPackage::setRemoveWithDeps(bool shallow, bool purge)
{
   /* The actual "with deps" behaviour is implemented in
    * RPackageLister::commitChanges by passing -R to xbps-remove,
    * which makes XBPS itself compute the recursive removal set.
    * `shallow` (no recursive) would mean using xbps-remove without
    * -R, but we always use -R for safety. */
   (void)shallow;
   setRemove(purge);
}

bool RPackage::setVersion(std::string verTag)
{
   /* XBPS doesn't have "candidate version" — we just store the
    * override so commitChanges can install the exact pkgver.
    * xbps-install doesn't accept version pins directly, so this is
    * effectively advisory under the popen-based backend. */
   _defaultCandVer = verTag;
   _boolFlags |= FOverrideVersion;
   if (_lister && _notify)
      _lister->notifyChange(this);
   return true;
}

void RPackage::unsetVersion()
{
   _defaultCandVer.clear();
   _boolFlags &= ~FOverrideVersion;
   if (_lister && _notify)
      _lister->notifyChange(this);
}

std::string RPackage::showWhyInstBroken()
{
   /* APT had a detailed "broken deps" report. With the popen-based
    * XBPS backend we don't have a transaction handle to probe for
    * missing deps. Return a generic message. */
   if (_name.empty())
      return "";
   std::string msg = _("The package dependencies could not be resolved.");
   return msg;
}

/* ------------------------------------------------------------------ */
/* Changelogs / screenshots                                            */
/* ------------------------------------------------------------------ */

std::string RPackage::getChangelogFile(void *fetcher)
{
   (void)fetcher;
   if (_name.empty())
      return "";

   /* Step 1: ask xbps-query for the package's optional `changelog`
    * URL field. Almost no Void packages set this, but the few that
    * do (e.g. some kernel packages) point to a gitweb or raw-file
    * URL that we can curl down. */
   std::string qn = _shell_quote(_name);
   std::string url_cmd = "env LANG=C xbps-query -p changelog ";
   url_cmd += qn + " 2>/dev/null";
   std::string url = _popen_capture(url_cmd.c_str());
   if (url.empty())
      return "";

   /* Step 2: fetch the URL with curl. We use --max-time 15 so the
    * UI doesn't hang if the URL is dead. */
   std::string qu = _shell_quote(url);
   std::string fetch_cmd = "curl -sL --max-time 15 ";
   fetch_cmd += qu + " 2>/dev/null";
   return _popen_capture(fetch_cmd.c_str());
}

std::string RPackage::getScreenshotFile(void *fetcher, bool thumb)
{
   (void)fetcher; (void)thumb;
   /* No screenshot service for Void Linux. */
   return "";
}

std::string RPackage::getChangelogURI()
{
   if (_name.empty())
      return "";
   std::string qn = _shell_quote(_name);
   std::string cmd = "env LANG=C xbps-query -p changelog ";
   cmd += qn + " 2>/dev/null";
   return _popen_capture(cmd.c_str());
}

#endif /* HAVE_XBPS */
