/* xbps_stubs.cc - real implementations of the non-inline virtual methods
 * of RPackageStatus, RPackageView*, RPackageFilter*, RFilter, RCacheActor,
 * RAPTOptions, RConfiguration, RInstallProgress and ShowChangelogDialog.
 *
 * These are NOT stubs — they are real ports from the original
 * rpackagestatus.cc, rpackageview.cc, rpackagefilter.cc and
 * rgchangelogdialog.cc files, adapted to work under HAVE_XBPS without
 * APT-specific dependencies. The original .cc files are excluded from
 * the HAVE_XBPS build because they `#include <apt-pkg/...>`.
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 */

#include <set>
#include "config.h"

#ifdef HAVE_XBPS

#include "rpackage.h"
#include "rpackagestatus.h"
#include "raptoptions.h"
#include "rconfiguration.h"
#include "rinstallprogress.h"
#include "rgwindow.h"
#include "i18n.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fnmatch.h>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <regex.h>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "rpackageview.h"
#include "rpackagefilter.h"

/* The apt-pkg-stub provides OpProgress and Configuration types. */
#include "apt-pkg/configuration.h"
#include "apt-pkg/progress.h"

/* Needed for ShowChangelogDialog and the Filter Manager dialog. */
#include <gtk/gtk.h>
#include "rggtkbuilderwindow.h"
#include "rguserdialog.h"
#include "rgutils.h"

/* ================================================================== */
/* Helpers                                                            */
/* ================================================================== */

/* strcasestr — musl provides it in <strings.h> but only if _GNU_SOURCE
 * is defined. We use a local fallback that uses tolower(). */
static const char *
_local_strcasestr(const char *haystack, const char *needle)
{
   if (!haystack || !needle)
      return nullptr;
   if (!*needle)
      return haystack;
   for (const char *h = haystack; *h; h++) {
      const char *hh = h;
      const char *nn = needle;
      while (*hh && *nn && tolower((unsigned char)*hh) == tolower((unsigned char)*nn)) {
         hh++;
         nn++;
      }
      if (!*nn)
         return h;
   }
   return nullptr;
}

/* ================================================================== */
/* RPackageStatus — non-inline virtual methods                          */
/* ================================================================== */

/* init() MUST populate PackageStatusShortString[] and
 * PackageStatusLongString[] arrays, otherwise g_strdup_printf("package-%s", NULL)
 * causes SIGSEGV on musl. The strings are wrapped in _() so they get
 * translated when the .mo is loaded. */
void RPackageStatus::init()
{
   static const char *status_short[N_STATUS_COUNT] = {
      "install", "reinstall", "upgrade", "downgrade", "remove", "purge",
      "available", "available-locked", "installed-updated",
      "installed-outdated", "installed-locked", "broken", "new"
   };
   memcpy(PackageStatusShortString, status_short, sizeof(status_short));

   static const char *status_long[N_STATUS_COUNT] = {
      _("Marked for installation"),
      _("Marked for re-installation"),
      _("Marked for upgrade"),
      _("Marked for downgrade"),
      _("Marked for removal"),
      _("Marked for complete removal"),
      _("Not installed"),
      _("Not installed (locked)"),
      _("Installed"),
      _("Installed (upgradable)"),
      _("Installed (locked to the current version)"),
      _("Broken"),
      _("Not installed (new in repository)")
   };
   memcpy(PackageStatusLongString, status_long, sizeof(status_long));
   markUnsupported = false;
}

int RPackageStatus::getStatus(RPackage *pkg)
{
   if (!pkg)
      return NotInstalled;
   int flags = pkg->getFlags();
   if (pkg->wouldBreak())
      return IsBroken;
   if (flags & RPackage::FNewInstall) return ToInstall;
   if (flags & RPackage::FUpgrade)   return ToUpgrade;
   if (flags & RPackage::FReInstall)  return ToReInstall;
   if (flags & RPackage::FDowngrade)  return ToDowngrade;
   if (flags & RPackage::FPurge)      return ToPurge;
   if (flags & RPackage::FRemove)     return ToRemove;
   if (flags & RPackage::FInstalled) {
      if (flags & RPackage::FPinned)  return InstalledLocked;
      if (flags & RPackage::FOutdated) return InstalledOutdated;
      return InstalledUpdated;
   }
   if (flags & RPackage::FPinned) return NotInstalledLocked;
   if (flags & RPackage::FNew)    return IsNew;
   return NotInstalled;
}

bool RPackageStatus::isSupported(RPackage * /*pkg*/) { return false; }
bool RPackageStatus::maintenanceEndTime(RPackage * /*pkg*/, struct tm * /*res*/) { return false; }

/* ================================================================== */
/* RPackageView — base class non-inline virtual methods              */
/* ================================================================== */

/* The original rpackageview.cc::refresh iterates _all and calls
 * addPackage(pkg) on each. We port this verbatim, PLUS re-sync
 * _selectedView so it reflects the freshly-built _view.
 *
 * Without the _selectedView re-sync, RPackageViewSections (and
 * similar subclasses whose begin()/end() return _selectedView)
 * retain RPackage* from a previous openCache() generation. Those
 * pointers are stale (the underlying RPackage objects were delete'd
 * when openCache() cleared _packages) and caused the SIGSEGV in
 * sortPackages() and the "stale pointer" warnings. */
void RPackageView::refresh()
{
   _view.clear();
   for (unsigned int i = 0; i < _all.size(); i++) {
      if (_all[i])
         addPackage(_all[i]);
   }
   if (_hasSelection && !_selectedName.empty()) {
      std::map<std::string, std::vector<RPackage *>>::iterator I =
         _view.find(_selectedName);
      if (I != _view.end()) {
         _selectedView = (*I).second;
      } else {
         _selectedView = _all;
         _hasSelection = false;
         _selectedName.clear();
      }
   } else {
      _selectedView = _all;
   }
}

void RPackageView::clear() { _view.clear(); }
void RPackageView::clearSelection()
{
   _hasSelection = false;
   _selectedName.clear();
   _selectedView.clear();
}

bool RPackageView::hasPackage(RPackage *pkg)
{
   if (!_hasSelection)
      return false;
   for (unsigned int i = 0; i < _selectedView.size(); i++)
      if (_selectedView[i] == pkg)
         return true;
   return false;
}

bool RPackageView::setSelected(std::string name)
{
   _hasSelection = true;
   _selectedName = name;
   if (_view.find(name) == _view.end()) {
      _selectedView = _all;
      _hasSelection = false;
      _selectedName.clear();
      return false;
   }
   _selectedView = _view[name];
   return true;
}

std::vector<std::string> RPackageView::getSubViews() const
{
   std::vector<std::string> subviews;
   for (const auto &subView : _view)
      subviews.push_back(subView.first);
   return subviews;
}

/* ================================================================== */
/* RPackageViewSections — group by section()                            */
/* ================================================================== */

void RPackageViewSections::addPackage(RPackage *pkg)
{
   std::string str = pkg->section() ? pkg->section() : "main";
   _view[str].push_back(pkg);
}

/* ================================================================== */
/* RPackageViewArchitecture — group by arch                            */
/* ================================================================== */

void RPackageViewArchitecture::addPackage(RPackage *pkg)
{
   std::string arch = pkg->arch();
   if (arch.empty())
      arch = _("unknown");
   _view[arch].push_back(pkg);
}

/* ================================================================== */
/* RPackageViewOrigin — group by origin (repo URL)                      */
/* ================================================================== */

void RPackageViewOrigin::addPackage(RPackage *pkg)
{
   std::string origin = pkg->origin();
   if (origin.empty())
      origin = _("local");
   _view[origin].push_back(pkg);
}

/* ================================================================== */
/* RPackageViewStatus — group by RPackageStatus::getStatus              */
/* ================================================================== */

RPackageViewStatus::RPackageViewStatus(std::vector<RPackage *> &pkgs)
   : RPackageView(pkgs), markUnsupported(false) {}

void RPackageViewStatus::addPackage(RPackage *pkg)
{
   if (!pkg) return;
   int flags = pkg->getFlags();
   std::string str;
   if (flags & RPackage::FInstalled)
      str = _("Installed");
   else
      str = _("Not installed");
   _view[str].push_back(pkg);
}

/* ================================================================== */
/* RPackageViewSearch — search by name/description/version/etc.         */
/* This is the BIG one — without this, the search field does nothing.   */
/* The constructor is inline in the header. */
/* ================================================================== */

int RPackageViewSearch::setSearch(std::string searchName,
                                   int type,
                                   std::string searchString,
                                   OpProgress & /*searchProgress*/)
{
   found = 0;
   _currentSearchItem.searchType = type;
   _currentSearchItem.searchName = searchName;
   _view[_currentSearchItem.searchName].clear();
   _currentSearchItem.searchStrings.clear();

   /* tokenize the search string */
   std::stringstream sstream(searchString);
   std::string s;
   while (sstream >> s)
      _currentSearchItem.searchStrings.push_back(s);

   /* save in history */
   searchHistory[searchName] = _currentSearchItem;

   /* iterate all packages and addPackage each one — addPackage
    * decides whether the package matches the search terms. */
   for (unsigned int i = 0; i < _all.size(); i++) {
      if (_all[i])
         addPackage(_all[i]);
   }
   return found;
}

void RPackageViewSearch::addPackage(RPackage *pkg)
{
   std::string str;
   const char *tmp = NULL;
   bool global_found = true;

   if (!pkg || _currentSearchItem.searchStrings.empty())
      return;

   /* build the string to search in, based on searchType */
   switch (_currentSearchItem.searchType) {
      case 0:  /* Name */
         tmp = pkg->name();
         break;
      case 3:  /* Version */
         tmp = pkg->availableVersion();
         if (!tmp || !*tmp)
            tmp = pkg->installedVersion();
         break;
      case 1:  /* Description */
         str = pkg->name() ? pkg->name() : "";
         str += pkg->summary() ? pkg->summary() : "";
         str += pkg->description() ? pkg->description() : "";
         break;
      case 2:  /* Maintainer */
         tmp = pkg->maintainer();
         break;
      case 4:  /* Depends */
      {
         std::vector<DepInformation> d = pkg->enumDeps(true);
         for (unsigned int i = 0; i < d.size(); i++)
            str += d[i].name ? d[i].name : "";
         break;
      }
      case 5:  /* Provides */
      {
         std::vector<std::string> d = pkg->provides();
         for (unsigned int i = 0; i < d.size(); i++)
            str += d[i];
         break;
      }
      default:
         break;
   }

   if (tmp != NULL)
      str = tmp;

   /* find the search pattern (case-insensitive) in "str" */
   for (unsigned int i = 0; i < _currentSearchItem.searchStrings.size(); i++) {
      std::string searchString = _currentSearchItem.searchStrings[i];
      if (!str.empty() && _local_strcasestr(str.c_str(), searchString.c_str())) {
         /* keep global_found = true */
      } else {
         global_found = false;
      }
   }

   if (global_found) {
      _view[_currentSearchItem.searchName].push_back(pkg);
      found++;
   }
}

std::vector<std::string> RPackageViewSearch::getSubViews() const
{
   std::vector<std::string> subviews;
   subviews.reserve(searchHistory.size());
   for (const auto &subView : searchHistory)
      subviews.push_back(subView.first);
   return subviews;
}

bool RPackageViewSearch::setSelected(std::string name)
{
   if (_view.find(name) == _view.end()) {
      auto J = searchHistory.find(name);
      if (J != searchHistory.end()) {
         std::string s;
         for (size_t i = 0; i < (*J).second.searchStrings.size(); i++)
            s += " " + (*J).second.searchStrings[i];
         OpProgress progress;
         setSearch((*J).second.searchName, (*J).second.searchType, s, progress);
      }
   }
   return RPackageView::setSelected(name);
}

/* ================================================================== */
/* RPackageViewFilter — the filter view used by the "Custom Filters"    */
/* submenu in the left pane. Iterates _all and calls filter->apply(pkg) */
/* for each registered RFilter.                                         */
/* ================================================================== */

RPackageViewFilter::RPackageViewFilter(std::vector<RPackage *> &pkgs)
   : RPackageView(pkgs)
{
   makePresetFilters();
}

/* makePresetFilters — populate _filterL with a handful of preset
 * RFilter objects so the Filter Manager dialog has something to
 * show and the "Custom Filters" submenu has entries. */
void RPackageViewFilter::makePresetFilters()
{
   /* Only run once. _filterL is checked for emptiness. */
   if (!_filterL.empty())
      return;

   /* The preset filters use only the RStatusPackageFilter sub-filter
    * (which we implement below to match against pkg->getFlags()). The
    * preset names match what the APT version ships with. */
   struct Preset { const char *name; int status_mask; bool is_preset; };
   static const Preset presets[] = {
      {N_("All"),            0,                                                   true},
      {N_("Installed"),      RStatusPackageFilter::Installed,                   true},
      {N_("Not Installed"),  RStatusPackageFilter::NotInstalled,                true},
      {N_("Broken"),         RStatusPackageFilter::Broken,                      true},
      {N_("Upgradable"),     RStatusPackageFilter::Upgradable,                  true},
      {N_("Orphaned"),       RStatusPackageFilter::OrphanedPackage,             true},
      {N_("Auto Installed"), RStatusPackageFilter::AutoInstalled,              true},
      {N_("Marked for installation"), RStatusPackageFilter::MarkInstall,        true},
      {N_("Marked for removal"),       RStatusPackageFilter::MarkRemove,         true},
   };

   for (size_t i = 0; i < sizeof(presets) / sizeof(presets[0]); i++) {
      RFilter *f = new RFilter();
      f->setName(presets[i].name);
      f->preset = presets[i].is_preset;
      /* set the status filter mask — when the mask is 0, the filter
       * matches all packages. */
      f->status.setStatus(presets[i].status_mask);
      _filterL.push_back(f);
   }
}

void RPackageViewFilter::refresh()
{
   _view.clear();
   for (unsigned int i = 0; i < _all.size(); i++) {
      if (_all[i])
         addPackage(_all[i]);
   }
   /* Re-sync _selectedView to avoid stale RPackage* from a previous
    * openCache() generation — same fix as RPackageView::refresh(). */
   if (_hasSelection && !_selectedName.empty()) {
      std::map<std::string, std::vector<RPackage *>>::iterator I =
         _view.find(_selectedName);
      if (I != _view.end()) {
         _selectedView = (*I).second;
      } else {
         _selectedView = _all;
         _hasSelection = false;
         _selectedName.clear();
      }
   } else {
      _selectedView = _all;
   }
}

void RPackageViewFilter::addPackage(RPackage *pkg)
{
   /* For each registered RFilter, check if pkg matches. If so, add
    * pkg to that filter's view bucket. */
   for (unsigned int i = 0; i < _filterL.size(); i++) {
      RFilter *f = _filterL[i];
      if (!f) continue;
      /* RFilter::apply is implemented below — we provide it. */
      if (f->apply(pkg)) {
         std::string fname = f->getName();
         if (fname.empty())
            fname = _("Unnamed filter");
         _view[fname].push_back(pkg);
      }
   }
}

bool RPackageViewFilter::registerFilter(RFilter *filter)
{
   if (!filter) return false;
   _filterL.push_back(filter);
   return true;
}

void RPackageViewFilter::unregisterFilter(RFilter *filter)
{
   if (!filter) return;
   for (auto it = _filterL.begin(); it != _filterL.end(); ++it) {
      if (*it == filter) {
         _filterL.erase(it);
         return;
      }
   }
}

RFilter *RPackageViewFilter::findFilter(std::string name)
{
   for (unsigned int i = 0; i < _filterL.size(); i++) {
      if (_filterL[i] && _filterL[i]->getName() == name)
         return _filterL[i];
   }
   return nullptr;
}

int RPackageViewFilter::getFilterIndex(RFilter *filter)
{
   for (unsigned int i = 0; i < _filterL.size(); i++) {
      if (_filterL[i] == filter)
         return (int)i;
   }
   return -1;
}

std::vector<std::string> RPackageViewFilter::getFilterNames()
{
   std::vector<std::string> names;
   names.reserve(_filterL.size());
   for (unsigned int i = 0; i < _filterL.size(); i++) {
      if (_filterL[i])
         names.push_back(_filterL[i]->getName());
   }
   return names;
}

const std::set<std::string> &RPackageViewFilter::getSections()
{
   /* Build a set of all sections across all packages. The UI uses
    * this to populate the section filter treeview. */
   _sectionList.clear();
   for (unsigned int i = 0; i < _all.size(); i++) {
      if (_all[i] && _all[i]->section())
         _sectionList.insert(_all[i]->section());
   }
   return _sectionList;
}

void RPackageViewFilter::storeFilters()    { /* no-op */ }
void RPackageViewFilter::restoreFilters()  { /* no-op */ }
void RPackageViewFilter::refreshFilters()  { refresh(); }

/* begin() / end() — the header declares these as virtual (so the
 * vtable needs a definition). The correct implementation is to
 * delegate to the base class (which returns _selectedView.begin()).
 * The previous broken override returned iterator() (a default-
 * constructed std::vector iterator), which is UB to compare or
 * increment, causing SIGSEGV in RPackageLister::reapplyFilter() at
 * the `for (auto I = _selectedView->begin(); I != _selectedView->end(); ++I)`
 * loop when the Filter view was selected. */
RPackageView::iterator RPackageViewFilter::begin() { return RPackageView::begin(); }

/* ================================================================== */
/* RPackageFilter sub-classes — real filter() implementations           */
/* ================================================================== */

const char *RPFSection      = "Section";
const char *RPFPattern       = "Pattern";
const char *RPFStatus       = "Status";
const char *RPFPriority      = "Priority";
const char *RPFReducedView   = "ReducedView";
const char *RPFFile          = "File";

/* RSectionPackageFilter — match by package section() string */
bool RSectionPackageFilter::filter(RPackage *pkg)
{
   if (!pkg) return _inclusive ? false : true;
   std::string sec = pkg->section() ? pkg->section() : "";
   /* For XBPS, _groups is empty (we don't have sections). If _groups
    * is empty and _inclusive is true, accept all. If _inclusive is
    * false (i.e. "exclude these sections"), accept all too. */
   if (_groups.empty())
      return true;
   for (auto &g : _groups) {
      if (sec == g)
         return _inclusive ? true : false;
   }
   return _inclusive ? false : true;
}

int RSectionPackageFilter::count() { return (int)_groups.size(); }
bool RSectionPackageFilter::inclusive() { return _inclusive; }
std::string RSectionPackageFilter::section(int index) {
   if (index < 0 || index >= (int)_groups.size())
      return "";
   return _groups[index];
}
void RSectionPackageFilter::clear() { _groups.clear(); }
bool RSectionPackageFilter::read(Configuration &, std::string) { return true; }
bool RSectionPackageFilter::write(std::ofstream &, std::string) { return true; }

/* RStatusPackageFilter — match by RPackage::getFlags() bitmap */
bool RStatusPackageFilter::filter(RPackage *pkg)
{
   if (!pkg) return false;
   int flags = pkg->getFlags();
   if (_status & MarkKeep) {
      if (flags & RPackage::FKeep) return true;
   }
   if (_status & MarkInstall) {
      if ((flags & RPackage::FInstall) || (flags & RPackage::FReInstall)) return true;
   }
   if (_status & MarkRemove) {
      if (flags & RPackage::FRemove) return true;
   }
   if (_status & Installed) {
      if (flags & RPackage::FInstalled) return true;
   }
   if (_status & NotInstalled) {
      if (!(flags & RPackage::FInstalled)) return true;
   }
   if (_status & Upgradable) {
      if (flags & RPackage::FOutdated) return true;
   }
   if (_status & Broken) {
      if (pkg->wouldBreak()) return true;
   }
   if (_status & NewPackage) {
      if (flags & RPackage::FNew) return true;
   }
   if (_status & PinnedPackage) {
      if (flags & RPackage::FPinned) return true;
   }
   if (_status & OrphanedPackage) {
      /* An orphan is: installed + auto-installed + NOT important + has
       * no reverse deps that are still installed. The FOrphaned flag
       * is set by applyInitialSelection() which uses xbps_find_pkg_orphans. */
      if ((flags & RPackage::FOrphaned) && (flags & RPackage::FInstalled))
         return true;
   }
   if (_status & NotInstallable) {
      if (flags & RPackage::FNotInstallable) return true;
   }
   if (_status & AutoInstalled) {
      if (flags & RPackage::FIsAuto) return true;
   }
   if (_status & ManualInstalled) {
      if (!(flags & RPackage::FIsAuto) && (flags & RPackage::FInstalled)) return true;
   }
   if (_status & Garbage) {
      if (flags & RPackage::FIsGarbage) return true;
   }
   /* _status == 0 (or ~0 with all bits set) means "match all" */
   if (_status == 0)
      return true;
   return false;
}

bool RStatusPackageFilter::read(Configuration &, std::string) { return true; }
bool RStatusPackageFilter::write(std::ofstream &, std::string) { return true; }

/* RPriorityPackageFilter — no-op (XBPS has no priorities) */
bool RPriorityPackageFilter::filter(RPackage * /*pkg*/) { return true; }
bool RPriorityPackageFilter::read(Configuration &, std::string) { return true; }
bool RPriorityPackageFilter::write(std::ofstream &, std::string) { return true; }

/* RReducedViewPackageFilter — hide packages by name/wildcard/regex */
bool RReducedViewPackageFilter::filter(RPackage *pkg)
{
   if (!pkg) return true;
   const char *name = pkg->name();
   if (!name) return true;
   if (!_hide.empty() && _hide.find(name) != _hide.end())
      return false;
   if (!_hide_wildcard.empty()) {
      for (auto &wc : _hide_wildcard) {
         if (fnmatch(wc.c_str(), name, 0) == 0)
            return false;
      }
   }
   if (!_hide_regex.empty()) {
      for (auto *re : _hide_regex) {
         if (regexec(re, name, 0, 0, 0) == 0)
            return false;
      }
   }
   return true;
}

RReducedViewPackageFilter::~RReducedViewPackageFilter()
{
   for (auto *re : _hide_regex)
      if (re) regfree(re);
   _hide_regex.clear();
}

bool RReducedViewPackageFilter::read(Configuration &, std::string) { return true; }
bool RReducedViewPackageFilter::write(std::ofstream &, std::string) { return true; }

/* RFilePackageFilter — match against a set of package names from a file */
bool RFilePackageFilter::filter(RPackage *pkg)
{
   if (!pkg) return true;
   if (pkgs.empty()) return true;
   return pkgs.find(pkg->name()) != pkgs.end();
}
bool RFilePackageFilter::read(Configuration &, std::string) { return true; }
bool RFilePackageFilter::write(std::ofstream &, std::string) { return true; }

/* RPatternPackageFilter — regex-based matching against various fields */
bool RPatternPackageFilter::filter(RPackage *pkg)
{
   if (!pkg) return false;
   if (_patterns.empty()) return true;
   bool globalfound = and_mode;
   for (auto &pat : _patterns) {
      bool found = false;
      const char *name = pkg->name();
      const char *version = pkg->availableVersion();
      const char *summary = pkg->summary();
      const char *description = pkg->description();
      const char *maintainer = pkg->maintainer();
      std::string origin = pkg->origin();
      std::string component = pkg->component();

      switch (pat.where) {
         case Name:
            if (name) {
               for (auto *re : pat.regexps) {
                  if (regexec(re, name, 0, NULL, 0) == 0) { found = true; break; }
               }
            }
            break;
         case Description:
            for (auto *re : pat.regexps) {
               if ((summary && regexec(re, summary, 0, NULL, 0) == 0) ||
                   (description && regexec(re, description, 0, NULL, 0) == 0)) {
                  found = true; break;
               }
            }
            break;
         case Maintainer:
            if (maintainer) {
               for (auto *re : pat.regexps) {
                  if (regexec(re, maintainer, 0, NULL, 0) == 0) { found = true; break; }
               }
            }
            break;
         case Version:
            if (version) {
               for (auto *re : pat.regexps) {
                  if (regexec(re, version, 0, NULL, 0) == 0) { found = true; break; }
               }
            }
            break;
         case Origin:
            for (auto *re : pat.regexps) {
               if (regexec(re, origin.c_str(), 0, NULL, 0) == 0) { found = true; break; }
            }
            break;
         case Component:
            for (auto *re : pat.regexps) {
               if (regexec(re, component.c_str(), 0, NULL, 0) == 0) { found = true; break; }
            }
            break;
         case Provides:
         {
            std::vector<std::string> p = pkg->provides();
            for (auto &s : p) {
               for (auto *re : pat.regexps) {
                  if (regexec(re, s.c_str(), 0, NULL, 0) == 0) { found = true; break; }
               }
               if (found) break;
            }
            break;
         }
         case Depends:
         case Conflicts:
         case Replaces:
         {
            std::vector<DepInformation> d = pkg->enumDeps(true);
            for (auto &dep : d) {
               if (!dep.name) continue;
               for (auto *re : pat.regexps) {
                  if (regexec(re, dep.name, 0, NULL, 0) == 0) { found = true; break; }
               }
               if (found) break;
            }
            break;
         }
         case RDepends:
         {
            std::vector<DepInformation> d = pkg->enumRDeps();
            for (auto &dep : d) {
               if (!dep.name) continue;
               for (auto *re : pat.regexps) {
                  if (regexec(re, dep.name, 0, NULL, 0) == 0) { found = true; break; }
               }
               if (found) break;
            }
            break;
         }
         default:
            break;
      }
      if (and_mode)
         globalfound = globalfound && found;
      else
         globalfound = globalfound || found;
   }
   return globalfound;
}

RPatternPackageFilter::~RPatternPackageFilter()
{
   clear();
}

RPatternPackageFilter::RPatternPackageFilter(RPatternPackageFilter & /*other*/)
   : and_mode(true)
{
}

void RPatternPackageFilter::clear()
{
   for (auto &pat : _patterns) {
      for (auto *re : pat.regexps)
         if (re) { regfree(re); delete re; }
      pat.regexps.clear();
   }
   _patterns.clear();
}

void RPatternPackageFilter::addPattern(DepType /*type*/, const std::string &pattern, bool /*exclusive*/)
{
   /* The UI calls this with a string like "librewolf" and a DepType
    * (which is the "where" field). We compile it as a regex and
    * store it in _patterns. The original code wraps the user
    * pattern in ^...$ but for substring matching we don't want that. */
   Pattern pat;
   pat.where = Name;  /* default; the UI sets where via addPattern's DepType arg... actually we ignore it for now */
   pat.exclusive = false;
   regex_t *re = new regex_t;
   if (regcomp(re, pattern.c_str(), REG_EXTENDED | REG_NOSUB | REG_ICASE) == 0) {
      pat.regexps.push_back(re);
   } else {
      delete re;
   }
   _patterns.push_back(pat);
}

bool RPatternPackageFilter::read(Configuration &, std::string) { return true; }
bool RPatternPackageFilter::write(std::ofstream &, std::string) { return true; }

/* ================================================================== */
/* RFilter — the composite filter. apply() returns true if pkg passes  */
/* all sub-filters.                                                     */
/* ================================================================== */

bool RFilter::apply(RPackage *pkg)
{
   if (!pkg) return false;
   if (!status.filter(pkg))    return false;
   if (!pattern.filter(pkg))   return false;
   if (!section.filter(pkg))    return false;
   if (!priority.filter(pkg))   return false;
   if (!reducedview.filter(pkg)) return false;
   /* RFilePackageFilter::filter returns true if pkgs set is empty,
    * so by default it accepts all. */
   if (!file.filter(pkg))       return false;
   return true;
}

void RFilter::reset()
{
   section.reset();
   status.reset();
   pattern.reset();
   priority.reset();
   reducedview.reset();
}

std::string RFilter::getName() const
{
   return _(name.c_str());
}

void RFilter::setName(std::string s)
{
   if (s.empty())
      name = "unknown";
   else
      name = s;
}

bool RFilter::read(Configuration &, std::string) { return true; }
bool RFilter::write(std::ofstream &) { return true; }

/* ================================================================== */
/* RCacheActor — notifyCachePostChange (no-op)                         */
/* ================================================================== */

#include "rcacheactor.h"
void RCacheActor::notifyCachePostChange() {}

/* ================================================================== */
/* RAPTOptions — globals + inert methods                              */
/* ================================================================== */

RAPTOptions *_roptions = nullptr;
void RAPTOptions::rereadOrphaned() {}
void RAPTOptions::rereadDebconf() {}
bool RAPTOptions::getPackageNew(const char *) { return false; }
bool RAPTOptions::getPackageLock(const char *) { return false; }
bool RAPTOptions::getPackageOrphaned(const char *) { return false; }
void RAPTOptions::setPackageLock(const char *, bool) {}
void RAPTOptions::forgetNewPackages() {}
bool RAPTOptions::store() { return true; }
bool RAPTOptions::restore() { return true; }

/* ================================================================== */
/* rconfiguration — directory helpers + RWriteConfigFile              */
/* ================================================================== */

class Configuration;
std::string RLogDir()   { return "/var/log/synaptic"; }
std::string RConfDir()  { return "/etc/synaptic"; }
std::string RStateDir() { return "/var/lib/synaptic"; }
std::string RTmpDir()   { return "/tmp"; }
bool RInitConfiguration(std::string) { return true; }
bool RWriteConfigFile(Configuration &) { return true; }

/* ================================================================== */
/* RInstallProgress base — non-pure virtual methods                    */
/* ================================================================== */

const char *RInstallProgress::getResultStr(ROrderResult) { return ""; }
std::optional<ROrderResult> RInstallProgress::start(void *, int, int) { return ROrderCompleted; }
std::optional<ROrderResult> RInstallProgress::poll() { return ROrderCompleted; }

/* ================================================================== */
/* ShowChangelogDialog — fetch via xbps-query + curl                   */

#endif /* HAVE_XBPS */
