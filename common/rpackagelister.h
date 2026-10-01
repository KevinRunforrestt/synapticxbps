/* rpackagelister.h - package cache and list manipulation
 *
 * Copyright (c) 2000, 2001 Conectiva S/A
 *               2002 Michael Vogt <mvo@debian.org>
 *
 * Author: Alfredo K. Kojima <kojima@conectiva.com.br>
 *         Michael Vogt <mvo@debian.org>
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

#include "rpackagestatus.h"

#include <ctime>
#include <istream>
#include <list>
#include <regex.h>
#include <set>
#include <string>
#include <sys/types.h>
#include <vector>
#include <map>

// GLib forward declaration — we only need an opaque pointer to
// GThread in the _openCacheState struct; the actual g_thread_new()
// call is in rpackagelister_xbps.cc, which includes <glib.h>.
typedef struct _GThread GThread;

#ifdef HAVE_XBPS
#   include "rpackagecache_xbps.h"
#else
#   include "rpackagecache.h"
#   include <apt-pkg/pkgcache.h>
#   include <apt-pkg/progress.h>
#endif

#ifdef HAVE_RPM
#   include <apt-pkg/depcache.h>
#endif

#ifdef HAVE_XAPIAN
#   include <xapian.h>
#endif

#ifdef HAVE_XBPS
class RPackageCacheXbps;
class FileFd;     /* forward decl used only by lockPackageCache(FileFd&);
                   * under XBPS the lock is handled by xbps-install / xbps-remove
                   * and this method is a no-op stub that never actually touches
                   * a FileFd instance. */
#else
class FileFd;
class OpProgress;
class RPackageCache;
#endif
class RCacheActor;
class RInstallProgress;
class RPackage;
class RPackageView;
class RPackageViewFilter;
class RPackageViewSearch;
class RUserDialog;
#ifdef HAVE_XBPS
/* No APT pkgAcquireStatus under XBPS. We pass a RFetchStatus* instead,
 * which is a small abstract base defined in rfetchstatus.h. */
#   include "rfetchstatus.h"
#   include "rprogress.h"
#else
class pkgAcquireStatus;
class pkgRecords;
#endif

class RPackageObserver
{
 public:
   virtual void notifyChange(RPackage *pkg) = 0;
   virtual void notifyPreFilteredChange() = 0;
   virtual void notifyPostFilteredChange() = 0;
};

class RCacheObserver
{
 public:
   virtual void notifyCacheOpen() = 0;
   virtual void notifyCachePreChange() = 0;
   virtual void notifyCachePostChange() = 0;
};

// base sort class
// for a example use see sortPackages()
template <class T> class sortFunc
{
 protected:
   bool _ascent;
   T cmp;

 public:
   sortFunc(bool ascent) : _ascent(ascent)
   {}
   bool operator()(RPackage *x, RPackage *y)
   {
      if (_ascent)
         return cmp(x, y);
      else
         return cmp(y, x);
   }
};

class RPackageLister
{

 protected:
   // Internal back-end stuff.
#ifdef HAVE_XBPS
   RPackageCacheXbps *_cache;     /* no-op wrapper; no libxbps handle */
   RProgress *_progMeter;        /* abstract progress interface */
#else
   RPackageCache *_cache;
   pkgRecords *_records;
   OpProgress *_progMeter;
#endif

#ifdef HAVE_XAPIAN
   Xapian::Database *_xapianDatabase;
#endif

   // Other members.
   std::vector<RPackage *> _packages;
   std::vector<int> _packagesIndex;

   std::vector<RPackage *> _viewPackages;
   std::vector<int> _viewPackagesIndex;

   // this is what we feed to the views as "all packages" to avoid
   // to show all the multiarch versions by default, the user can
   // turn that off with a config option
   std::vector<RPackage *> _nativeArchPackages;

   // It shouldn't be needed to control this inside this class. -- niemeyer
   bool _updating;
   pid_t _updateCachePid = -1;

   // Async openCache state. Owned by RPackageLister; the worker
   // thread reads/writes _openCacheState via g_thread_new / g_idle_add
   // (mirrors iruka-xbps's std::thread + g_idle_add pattern).
   struct {
      bool   done;        // worker has finished
      int    rv;          // 0 = success, non-zero = error
      GThread *thread;    // worker thread handle (released on join)
   } _openCacheState;

   /* The worker thread populates this struct with new RPackage* and
    * metadata; the GTK thread swaps it into _packages in
    * openCacheFinalize(). This avoids the data race where the worker
    * thread modified _packages while the GTK treeview was reading it. */
   struct OpenCacheResult {
      std::vector<RPackage *> packages;
      std::set<std::string>   packageNames;
      int                     installedCount;
      std::map<std::string, std::string> outdatedMap;
   } *_pendingResult;
#ifdef HAVE_XBPS
   static void *_openCacheThreadFunc(void *arg);
#endif

   // all known packages (needed identifing "new" pkgs)
   std::set<std::string> packageNames;

   bool _cacheValid; // is the cache valid?

   int _installedCount; // # of installed packages

   std::vector<RCacheActor *> _actors;

   RPackageViewFilter *_filterView; // the package view that does the filtering
   RPackageViewSearch
      *_searchView; // the package view that does the (simple) search

   // helper for the limitBySearch() code
   bool xapianSearch(std::string searchString);

 public:
   unsigned int _viewMode;

   typedef enum {
      LIST_SORT_DEFAULT,
      LIST_SORT_NAME_ASC,
      LIST_SORT_NAME_DES,
      LIST_SORT_SIZE_ASC,
      LIST_SORT_SIZE_DES,
      LIST_SORT_SUPPORTED_ASC,
      LIST_SORT_SUPPORTED_DES,
      LIST_SORT_SECTION_ASC,
      LIST_SORT_SECTION_DES,
      LIST_SORT_COMPONENT_ASC,
      LIST_SORT_COMPONENT_DES,
      LIST_SORT_DLSIZE_ASC,
      LIST_SORT_DLSIZE_DES,
      LIST_SORT_STATUS_ASC,
      LIST_SORT_STATUS_DES,
      LIST_SORT_VERSION_ASC,
      LIST_SORT_VERSION_DES,
      LIST_SORT_INST_VERSION_ASC,
      LIST_SORT_INST_VERSION_DES
   } listSortMode;
   listSortMode _sortMode;

#if defined(HAVE_RPM)
   typedef pkgDepCache::State pkgState;
#elif defined(HAVE_XBPS)
   /* XBPS keeps a snapshot of every RPackage's int flags bitmap. */
   typedef std::vector<int> pkgState;
#else
   typedef std::vector<int> pkgState;
#endif

 private:
   std::vector<RPackageView *> _views;
   RPackageView *_selectedView;
   RPackageStatus _pkgStatus;

   void applyInitialSelection();

   bool lockPackageCache(FileFd &lock);

   void sortPackages(std::vector<RPackage *> &packages, listSortMode mode);

   struct
   {
      char *pattern;
      regex_t regex;
      bool isRegex;
      int last;
   } _searchData;

   std::vector<RPackageObserver *> _packageObservers;
   std::vector<RCacheObserver *> _cacheObservers;

   RUserDialog *_userDialog;

   void makeCommitLog();
   void writeCommitLog();
   std::string _logEntry;
   time_t _logTime;

   // undo/redo stuff
   std::list<pkgState> undoStack;
   std::list<pkgState> redoStack;

 public:
   // limit what the current view displays
   bool limitBySearch(std::string searchString);

   // clean files older than "Synaptic::delHistory"
   void cleanCommitLog();

   void sortPackages(listSortMode mode)
   {
      sortPackages(_viewPackages, mode);
   }

   void setView(unsigned int index);
   std::vector<std::string> getViews() const;
   std::vector<std::string> getSubViews() const;

   // set subView (if newView is empty, set to all packages)
   bool setSubView(std::string newView = "");

   // this needs a different name, something like refresh
   void reapplyFilter();

   // refresh view
   void refreshView();

   // is is exposed for the stuff like filter manager window
   RPackageViewFilter *filterView()
   {
      return _filterView;
   }
   RPackageViewSearch *searchView()
   {
      return _searchView;
   }

   // find
   int findPackage(const char *pattern);
   int findNextPackage();

   const std::vector<RPackage *> &getPackages()
   {
      return _packages;
   }
   const std::vector<RPackage *> &getViewPackages()
   {
      return _viewPackages;
   }
   RPackage *getPackage(int index)
   {
      return _packages.at(index);
   }
   RPackage *getViewPackage(int index)
   {
      return _viewPackages.at(index);
   }
#ifdef HAVE_XBPS
   RPackage *getPackage(void *pkgdict) { (void)pkgdict; return NULL; }
#else
   RPackage *getPackage(pkgCache::PkgIterator &pkg);
#endif
   RPackage *getPackage(std::string name);
   int getPackageIndex(RPackage *pkg);
   int getViewPackageIndex(RPackage *pkg);

   int packagesSize()
   {
      return _packages.size();
   }
   int viewPackagesSize()
   {
      return _updating ? 0 : _viewPackages.size();
   }

   void getStats(int &installed,
                 int &broken,
                 int &toInstall,
                 int &toRemove,
                 double &sizeChange);

   void getSummary(int &held,
                   int &kept,
                   int &essential,
                   int &toInstall,
                   int &toReInstall,
                   int &toUpgrade,
                   int &toRemove,
                   int &toDowngrade,
                   int &unAuthenticated,
                   double &sizeChange);


   void getDetailedSummary(std::vector<RPackage *> &held,
                           std::vector<RPackage *> &kept,
                           std::vector<RPackage *> &essential,
                           std::vector<RPackage *> &toInstall,
                           std::vector<RPackage *> &toReInstall,
                           std::vector<RPackage *> &toUpgrade,
                           std::vector<RPackage *> &toRemove,
                           std::vector<RPackage *> &toPurge,
                           std::vector<RPackage *> &toDowngrade,
#ifdef WITH_APT_AUTH
                           std::vector<std::string> &notAuthenticated,
#endif
                           double &sizeChange);

   void getDownloadSummary(int &dlCount, double &dlSize);

   void saveUndoState(pkgState &state);
   void saveUndoState();
   void undo();
   void redo();
   void saveState(pkgState &state);
   void restoreState(pkgState &state);
   bool getStateChanges(pkgState &state,
                        std::vector<RPackage *> &kept,
                        std::vector<RPackage *> &toInstall,
                        std::vector<RPackage *> &toReInstall,
                        std::vector<RPackage *> &toUpgrade,
                        std::vector<RPackage *> &toRemove,
                        std::vector<RPackage *> &toDowngrade,
                        std::vector<RPackage *> &notAuthenticated,
                        const std::vector<RPackage *> &exclude,
                        bool sorted = true);

   // open (lock if run as root)
   bool openCache();
   // GTK-thread continuation of openCache() — runs the parts that
   // mutate _packages / _viewPackages / _views (applyInitialSelection,
   // view refresh, sort, reapplyFilter, notifyCacheOpen). MUST be
   // called from the GTK main thread because the treeview reads the
   // same vectors. Called automatically by openCache() when running
   // synchronously; the async path requires the UI poll to call it
   // explicitly after openCacheAsyncIsDone() == true.
   void openCacheFinalize();
   // Synchronous-but-non-blocking variant: fires openCacheAsync and
   // pumps GTK events on the calling thread until the worker is done,
   // then calls openCacheFinalize on the GTK thread. Use this from
   // code paths that need a "refresh the cache now" semantics but
   // cannot afford to crash the treeview by mutating _packages
   // while the model is still attached. Returns the openCache rv.
   int  openCacheSyncGtk();
   // Async variant: forks a worker thread that runs openCache() off
   // the GTK main loop. Returns immediately. The caller polls
   // openCacheAsyncIsDone() from a g_timeout_add callback (typical
   // interval 50-100ms). When done, openCacheAsyncResult() returns
   // the same bool openCache() would have returned.
   //
   // This mirrors the iruka-xbps pattern (std::thread + g_idle_add)
   // and the octoxbps pattern (QtConcurrent::run + QFutureWatcher)
   // so the GTK UI does not freeze while xbps-query -l / -Rs - runs.
   bool openCacheAsync();
   bool openCacheAsyncIsDone();
   int  openCacheAsyncResult();
   // Wait synchronously for the async open to finish. Used by the
   // non-interactive command-line paths in gsynaptic.cc.
   void openCacheAsyncWait();
   bool fixBroken();
   bool check();
   bool upgradable();
   bool upgrade();
   bool distUpgrade();
   bool cleanPackageCache(bool forceClean = false);
#ifdef HAVE_XBPS
   bool updateCache(RFetchStatus *status, std::string &error);
   bool updateCacheWait();
   bool updateCacheStart();
   bool updateCacheIsDone();
   int updateCacheResult();
   void setUiPumpCallback(void (*cb)(void));
   /* handle() kept for ABI parity with the APT branch. The XBPS
    * backend no longer owns a libxbps handle, so this always
    * returns NULL. */
   void *handle() { return NULL; }
   bool commitChanges(RFetchStatus *status, RInstallProgress *iprog);
#else
   bool updateCache(pkgAcquireStatus *status, std::string &error);
   bool commitChanges(pkgAcquireStatus *status, RInstallProgress *iprog);
#endif

   // some information
   bool getDownloadUris(std::vector<std::string> &uris);
   bool addArchiveToCache(std::string archiveDir, std::string &pkgname);

   void setProgressMeter(RProgress *progMeter)
   {
      if (_progMeter != NULL)
         delete _progMeter;
      _progMeter = progMeter;
   }

   void setUserDialog(RUserDialog *dialog)
   {
      _userDialog = dialog;
   }

   // policy stuff
   std::vector<std::string> getPolicyArchives(bool filenames_only = false)
   {
      if (_cacheValid)
         return _cache->getPolicyArchives(filenames_only);
      else
         return std::vector<std::string>();
   }

   // multiarch
   bool isMultiarchSystem();

   // notification stuff about changes in packages
   void notifyPreChange(RPackage *pkg);
   void notifyPostChange(RPackage *pkg);
   void notifyChange(RPackage *pkg);
   void registerObserver(RPackageObserver *observer);
   void unregisterObserver(RPackageObserver *observer);

   // notification stuff about changes in cache
   void notifyCacheOpen();
   void notifyCachePreChange();
   void notifyCachePostChange();
   void registerCacheObserver(RCacheObserver *observer);
   void unregisterCacheObserver(RCacheObserver *observer);

   bool readSelections(std::istream &in);
   bool writeSelections(std::ostream &out, bool fullState);

#ifdef HAVE_XBPS
   RPackageCacheXbps *getCache() { return _cache; }
#else
   RPackageCache *getCache() { return _cache; }
#endif
#ifdef HAVE_XAPIAN
   Xapian::Database *xapiandatabase()
   {
      return _xapianDatabase;
   }
   time_t xapianIndexTimestamp();
   bool xapianIndexNeedsUpdate();
   bool openXapianIndex();
#endif

   RPackageLister();
   ~RPackageLister();
};
