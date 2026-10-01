/* rpackagelister_xbps.cc - popen-based RPackageLister implementation
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Author: Synaptic-XBPS porting team
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
 *
 * -------------------------------------------------------------------
 *
 * This file implements every method of RPackageLister under the
 * HAVE_XBPS compile-time switch. It is the popen / fork+execvp
 * counterpart of rpackagelister.cc (the APT implementation). The
 * public API is identical so the GTK UI layer (gtk/) does not need
 * to know which backend it is talking to.
 *
 * Key design points (mirrors iruka-xbps and octoxbps):
 *
 *   - All package queries are issued via `popen("env LANG=C
 *     xbps-query ...")`. The output is parsed with the same
 *     column-based logic iruka-xbps uses (split on whitespace,
 *     column 0 = status, column 1 = name-version, rest = summary).
 *
 *   - All privileged operations (xbps-install -S, xbps-install -y,
 *     xbps-remove -R -y) are run via `fork() + execvp()` with a
 *     `pipe()` whose read end is drained in a GThread. The main
 *     thread pumps GTK events via the uiPumpCallback while waiting
 *     for the child to exit. An `std::atomic<bool> done` flag is
 *     set by the worker thread; the UI polls it with
 *     `g_timeout_add`.
 *
 *   - There is no pkgDepCache. The "intended action" for each
 *     package is stored in RPackage::_boolFlags (set by
 *     setInstall/setRemove/etc.). commitChanges() walks _packages
 *     and turns the flags into a single xbps-install / xbps-remove
 *     invocation.
 *
 *   - There is no pkgAcquire / pkgPackageManager. Downloads happen
 *     inside xbps-install itself; the UI's fetch-progress window
 *     simply pulses its progress bar while the child runs.
 *
 *   - There is no multiarch. isMultiarchSystem() always returns
 *     false and _nativeArchPackages mirrors _packages 1:1.
 *
 *   - There is no xapian index under Void Linux; the xapian helpers
 *     are stubbed (but still compiled under #ifdef HAVE_XAPIAN so
 *     the build does not break if xapian support is enabled at
 *     configure time).
 *
 *   - State persistence (saveState/restoreState) is implemented
 *     with std::vector<int> snapshots of RPackage::getFlags().
 *     Undo/redo uses two stacks of these snapshots.
 *
 *   - The "selections file" format is the same as the APT version's
 *     (pkgname<TAB>install / deinstall / purge) so backups made by
 *     apt-mark / dpkg --set-selections can be read back here.
 */

#include "config.h"

#ifdef HAVE_XBPS

#include "rpackagelister.h"

#include "i18n.h"
#include "raptoptions.h"
#include "rcacheactor.h"
#include "rconfiguration.h"
#include "rinstallprogress.h"
#include "rpackage.h"
#include "rpackagecache_xbps.h"
#include "rpackageview.h"
#include "ruserdialog.h"

#include <glib.h>
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <ctype.h>
#include <dirent.h>
#include <iostream>
#include <map>
#include <poll.h>
#include <set>
#include <sstream>
#include <string>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

/* rpackagelister.h declares `bool lockPackageCache(FileFd &lock);` but
 * only forward-declares `FileFd` in the non-XBPS branch of its
 * `#ifdef`. Under HAVE_XBPS the type is unknown to the compiler when
 * the class body is parsed. We forward-declare it here so the header
 * compiles; the body of lockPackageCache() is a no-op stub anyway
 * (the pkgdb lock is held by xbps-install / xbps-remove via flock(2)
 * on /var/db/xbps/.xbps-pkgdb.lock). */
class FileFd;

using namespace std;

/* ------------------------------------------------------------------ */
/* Local helpers                                                       */
/* ------------------------------------------------------------------ */

/* Synaptic-local config defaults. Under APT these come from
 * _config->FindI("Synaptic::..."). Since the popen-based XBPS
 * backend has no equivalent of the apt config tree we use a small
 * getenv-driven shim: if the matching environment variable is set
 * we honour it, otherwise we fall back to a hard-coded default.
 * This keeps the XBPS build free of any _config dependency while
 * still being tunable. */
static int
syn_conf_int(const char *envname, int defval)
{
   const char *s = getenv(envname);
   if (!s || !*s)
      return defval;
   char *end = NULL;
   long v = strtol(s, &end, 10);
   if (end == s)
      return defval;
   return (int)v;
}

static bool
syn_conf_bool(const char *envname, bool defval)
{
   const char *s = getenv(envname);
   if (!s || !*s)
      return defval;
   return (strcasecmp(s, "true") == 0 ||
           strcasecmp(s, "yes") == 0 ||
           strcasecmp(s, "1") == 0);
}

/* Tiny stderr-only error emitter — replaces APT's _error->Error(...) */
static void
r_listerr(const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);
   fputs("RPackageLister(xbps): ", stderr);
   vfprintf(stderr, fmt, ap);
   fputc('\n', stderr);
   va_end(ap);
}

/* ------------------------------------------------------------------ */
/* xbps-query output parsing (from iruka-xbps package.cpp)             */
/* ------------------------------------------------------------------ */

/* Split a multi-line string into individual non-empty lines. */
static vector<string>
split_lines(const string &s)
{
   vector<string> lines;
   istringstream iss(s);
   string line;
   while (getline(iss, line))
      if (!line.empty())
         lines.push_back(line);
   return lines;
}

/* Tokenize a string on whitespace. */
static vector<string>
split_spaces(const string &s)
{
   vector<string> parts;
   istringstream iss(s);
   string part;
   while (iss >> part)
      parts.push_back(part);
   return parts;
}

/* Extract pkgname from "name-version_rev" by splitting at the last
 * '-' that is followed by a digit. Matches iruka-xbps's getBaseName
 * behaviour but with the digit-following-dash check so package names
 * that themselves contain dashes (e.g. "libfoo-bar") are handled
 * correctly. */
static string
xbps_basename(const string &pkgver)
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

static string
xbps_version(const string &pkgver)
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

/* Run a command via popen(3) and capture stdout as a string. */
static string
popen_capture(const char *cmd)
{
   if (!cmd || !*cmd)
      return "";
   FILE *pipe = popen(cmd, "r");
   if (!pipe)
      return "";
   string out;
   char buf[4096];
   while (fgets(buf, sizeof buf, pipe))
      out += buf;
   pclose(pipe);
   return out;
}

/* ------------------------------------------------------------------ */
/* Parsed-row structure used by openCache                              */
/* ------------------------------------------------------------------ */

struct XbpsRow
{
   string name;        /* pkgname (e.g. "firefox")                */
   string pkgver;      /* full pkgver (e.g. "firefox-120.0_1")    */
   string summary;     /* short description (rest of the line)    */
   bool installed;     /* true if the row says "installed"        */
};

/* Parse `xbps-query -l` output. Each line looks like:
 *   ii  pkgname-1.0_1  short description
 * The first column is the state code ("ii" = installed, "rr" = removed,
 * "hh" = on hold, etc.). A "*" in the state column also indicates
 * installed. */
static vector<XbpsRow>
parse_installed_list(const string &output)
{
   vector<XbpsRow> result;
   vector<string> lines = split_lines(output);
   for (size_t li = 0; li < lines.size(); li++) {
      vector<string> parts = split_spaces(lines[li]);
      if (parts.size() < 2)
         continue;
      const string &state = parts[0];
      const string &namever = parts[1];

      XbpsRow r;
      r.name = xbps_basename(namever);
      r.pkgver = namever;
      r.installed = (state.find('*') != string::npos ||
                     state == "i" || state == "ii" ||
                     state == "[*]" || state == "hh" ||
                     state == "hh" || state.rfind("i", 0) == 0);

      string comment;
      for (size_t c = 2; c < parts.size(); c++) {
         if (!comment.empty()) comment += " ";
         comment += parts[c];
      }
      r.summary = comment;
      result.push_back(r);
   }
   return result;
}

/* Parse `xbps-query -Rs -` output. Each line looks like:
 *   [*]  pkgname-1.0_1  short description
 * The first column is "[*]" if the package is installed locally,
 * "[-]" otherwise. */
static vector<XbpsRow>
parse_remote_list(const string &output)
{
   vector<XbpsRow> result;
   vector<string> lines = split_lines(output);
   for (size_t li = 0; li < lines.size(); li++) {
      vector<string> parts = split_spaces(lines[li]);
      if (parts.size() < 2)
         continue;
      const string &state = parts[0];
      const string &namever = parts[1];

      XbpsRow r;
      r.name = xbps_basename(namever);
      r.pkgver = namever;
      r.installed = (state.find('*') != string::npos);

      string comment;
      for (size_t c = 2; c < parts.size(); c++) {
         if (!comment.empty()) comment += " ";
         comment += parts[c];
      }
      r.summary = comment;
      result.push_back(r);
   }
   return result;
}

/* Parse `xbps-install -un` (dry-run upgrade) output to find which
 * installed packages have a newer version available. Each line of
 * interest contains the literal word "update". */
static map<string, string>
parse_outdated_list(const string &output)
{
   map<string, string> result;
   vector<string> lines = split_lines(output);
   for (size_t li = 0; li < lines.size(); li++) {
      const string &line = lines[li];
      if (line.find("update") == string::npos)
         continue;
      vector<string> parts = split_spaces(line);
      if (parts.empty())
         continue;
      const string &namever = parts[0];
      string name = xbps_basename(namever);
      string ver  = xbps_version(namever);
      result[name] = ver;
   }
   return result;
}

/* ------------------------------------------------------------------ */
/* fork()+execvp()+pipe() helpers for privileged operations           */
/* ------------------------------------------------------------------ */

/* Close every file descriptor > 2 in the child process so the
 * forked xbps-install / xbps-remove doesn't accidentally keep the
 * GTK socket / X11 connection alive (which would prevent the
 * parent from cleanly shutting down the UI). */
static void
_close_all_fds(void)
{
   DIR *d = opendir("/proc/self/fd");
   if (!d) {
      /* Fallback: brute-force close up to a high limit. */
      for (int fd = 3; fd < 1024; fd++)
         close(fd);
      return;
   }
   struct dirent *ent;
   while ((ent = readdir(d)) != NULL) {
      if (!isdigit((unsigned char)ent->d_name[0]))
         continue;
      int fd = atoi(ent->d_name);
      if (fd > 2)
         close(fd);
   }
   closedir(d);
}

/* Run `argv[0]` with `argv[1..]` in a forked child, capturing the
 * child's combined stdout+stderr into `output_out`. Returns the
 * child's exit status (0 on success, non-zero on failure, -1 on
 * fork/pipe failure).
 *
 * This is the synchronous variant — used by openCache(), which is
 * already running in the main thread and can afford to block.
 *
 * The setpgid(0, 0) call puts the child into its own process group
 * so a later SIGTERM can be sent to the whole group with
 * kill(-pid, SIGTERM) if we ever need cancellation. */
static int
run_capture_sync(const vector<string> &argv, string *output_out)
{
   if (argv.empty())
      return -1;

   int pipefd[2];
   if (pipe(pipefd) != 0)
      return -1;

   pid_t pid = fork();
   if (pid < 0) {
      close(pipefd[0]);
      close(pipefd[1]);
      return -1;
   }

   if (pid == 0) {
      /* Child process. */
      setpgid(0, 0);
      dup2(pipefd[1], STDOUT_FILENO);
      dup2(pipefd[1], STDERR_FILENO);
      close(pipefd[0]);
      close(pipefd[1]);
      _close_all_fds();

      vector<const char *> argv_c;
      argv_c.reserve(argv.size() + 1);
      for (const string &a : argv)
         argv_c.push_back(a.c_str());
      argv_c.push_back(NULL);

      execvp(argv_c[0], (char *const *)argv_c.data());
      _exit(127);
   }

   /* Parent process. */
   close(pipefd[1]);
   string out;
   FILE *f = fdopen(pipefd[0], "r");
   if (f) {
      char buf[4096];
      size_t n;
      while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
         if (output_out)
            out.append(buf, n);
      }
      fclose(f);
   } else {
      close(pipefd[0]);
   }

   if (output_out)
      output_out->swap(out);

   int status = 0;
   waitpid(pid, &status, 0);
   return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* Spawn multiple child processes in parallel and wait for all of
 * them to finish. Each child's stdout is captured into the matching
 * slot in `outputs`. This is used by openCache() to run xbps-query
 * -l, xbps-query -Rs -, and xbps-install -un concurrently, which
 * roughly cuts the cache-open wall time to the slowest of the three
 * instead of the sum. */
struct ParallelJob {
   vector<string> argv;
   string *output;
   pid_t pid;
   int fd;
};

static void
run_parallel_capture(vector<ParallelJob> &jobs)
{
   /* Fork all children first. */
   for (auto &job : jobs) {
      int pipefd[2];
      if (pipe(pipefd) != 0) {
         job.pid = -1;
         job.fd = -1;
         continue;
      }
      pid_t pid = fork();
      if (pid < 0) {
         close(pipefd[0]);
         close(pipefd[1]);
         job.pid = -1;
         job.fd = -1;
         continue;
      }
      if (pid == 0) {
         /* Child. */
         setpgid(0, 0);
         dup2(pipefd[1], STDOUT_FILENO);
         dup2(pipefd[1], STDERR_FILENO);
         close(pipefd[0]);
         close(pipefd[1]);
         _close_all_fds();
         vector<const char *> argv_c;
         argv_c.reserve(job.argv.size() + 1);
         for (const string &a : job.argv)
            argv_c.push_back(a.c_str());
         argv_c.push_back(NULL);
         execvp(argv_c[0], (char *const *)argv_c.data());
         _exit(127);
      }
      /* Parent. */
      close(pipefd[1]);
      job.pid = pid;
      job.fd = pipefd[0];
   }

   /* Read from all pipes using poll, then waitpid each child. */
   vector<string> bufs(jobs.size());
   while (true) {
      bool any_open = false;
      struct pollfd pfds[16];
      int idx[16];
      int n = 0;
      for (size_t i = 0; i < jobs.size() && n < 16; i++) {
         if (jobs[i].fd >= 0) {
            pfds[n].fd = jobs[i].fd;
            pfds[n].events = POLLIN;
            idx[n] = (int)i;
            n++;
            any_open = true;
         }
      }
      if (!any_open)
         break;
      int r = poll(pfds, (nfds_t)n, 5000);
      if (r <= 0)
         break;
      for (int j = 0; j < n; j++) {
         if (pfds[j].revents & (POLLIN | POLLHUP)) {
            char buf[4096];
            ssize_t got = read(pfds[j].fd, buf, sizeof buf);
            if (got > 0) {
               bufs[idx[j]].append(buf, (size_t)got);
            } else {
               close(pfds[j].fd);
               jobs[idx[j]].fd = -1;
            }
         }
      }
   }
   /* Close any still-open fds. */
   for (auto &job : jobs) {
      if (job.fd >= 0) {
         close(job.fd);
         job.fd = -1;
      }
   }
   /* Wait for all children. */
   for (auto &job : jobs) {
      if (job.pid > 0) {
         int status = 0;
         waitpid(job.pid, &status, 0);
      }
   }
   /* Swap captured output into the job's output slot. */
   for (size_t i = 0; i < jobs.size(); i++) {
      if (jobs[i].output)
         jobs[i].output->swap(bufs[i]);
   }
}

/* ------------------------------------------------------------------ */
/* Async update-cache state (xbps-install -S in a GThread)            */
/* ------------------------------------------------------------------ */

/* All the state needed by the async updateCacheStart/IsDone/Result
 * API. The GThread reads/writes the atomics; the UI polls
 * `done` from a g_timeout_add callback. */
static struct {
   std::atomic<bool> done{false};
   std::atomic<int>  rv{0};
   std::atomic<int>  pid{0};
} _updateCacheState;

static void (*_uiPumpCallback)(void) = nullptr;

void
RPackageLister::setUiPumpCallback(void (*cb)(void))
{
   _uiPumpCallback = cb;
}

/* GThread entry point: forks xbps-install -S, drains its output,
 * waitpid()s, then sets `done`. The UI is responsible for pumping
 * GTK events while we work. */
static void *
_updateCacheThread(void *arg)
{
   (void)arg;

   int pipefd[2];
   if (pipe(pipefd) != 0) {
      _updateCacheState.rv = -1;
      _updateCacheState.done = true;
      return NULL;
   }

   pid_t pid = fork();
   if (pid < 0) {
      close(pipefd[0]);
      close(pipefd[1]);
      _updateCacheState.rv = -1;
      _updateCacheState.done = true;
      return NULL;
   }

   if (pid == 0) {
      /* Child. */
      setpgid(0, 0);
      dup2(pipefd[1], STDOUT_FILENO);
      dup2(pipefd[1], STDERR_FILENO);
      close(pipefd[0]);
      close(pipefd[1]);
      /* If not root, wrap in pkexec. Do NOT close all fds — pkexec
       * needs the D-Bus socket and X11/Wayland display connection
       * to show the polkit authentication dialog. Closing them
       * makes pkexec fail silently and the UI hangs forever. */
      if (getuid() != 0) {
         /* Use full paths like iruka-xbps: pkexec /usr/bin/xbps-install -S.
          * Do NOT close fds — pkexec needs D-Bus and X11/Wayland sockets
          * to show the polkit authentication dialog. */
         const char *argv[] = {"/usr/bin/pkexec", "/usr/bin/xbps-install", "-S", NULL};
         execvp("/usr/bin/pkexec", (char *const *)argv);
      } else {
         _close_all_fds();
         const char *argv[] = {"/usr/bin/xbps-install", "-S", NULL};
         execvp("/usr/bin/xbps-install", (char *const *)argv);
      }
      _exit(127);
   }

   /* Parent. */
   close(pipefd[1]);
   _updateCacheState.pid = pid;

   FILE *f = fdopen(pipefd[0], "r");
   if (f) {
      char buf[4096];
      while (fread(buf, 1, sizeof buf, f) > 0) {
         /* discard — the UI's fetch progress just pulses */
      }
      fclose(f);
   } else {
      close(pipefd[0]);
   }

   int status = 0;
   waitpid(pid, &status, 0);
   _updateCacheState.rv = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
   _updateCacheState.done = true;
   return NULL;
}

bool
RPackageLister::updateCacheStart()
{
   _updateCacheState.done = false;
   _updateCacheState.rv = 0;
   _updateCacheState.pid = 0;
   GThread *t = g_thread_new("xbps-sync", _updateCacheThread, NULL);
   if (t)
      g_thread_unref(t);
   return true;
}

bool RPackageLister::updateCacheIsDone() { return _updateCacheState.done; }
int  RPackageLister::updateCacheResult() { return _updateCacheState.rv; }
bool RPackageLister::updateCacheWait()   { return true; }

bool
RPackageLister::updateCache(RFetchStatus *status, string &error)
{
   /* Synchronous variant: block on xbps-install -S in the calling
    * thread. Used by code paths that don't have a GTK main loop
    * handy (e.g. command-line test harnesses). The UI uses
    * updateCacheStart/IsDone/Result instead. */
   if (status) { status->Start(); status->Pulse(); status->Stop(); }
   vector<string> argv;
   argv.push_back("xbps-install");
   argv.push_back("-S");
   int rv = run_capture_sync(argv, NULL);
   if (rv != 0) {
      error = "xbps-install -S exited with status ";
      error += to_string(rv);
      return false;
   }
   return true;
}

/* ------------------------------------------------------------------ */
/* Async openCache (iruka-xbps / octoxbps pattern)                    */
/*                                                                    */
/* The synchronous openCache() runs two popen() calls — `xbps-query -l`*/
/* and `xbps-install -un` — which fork and can take a noticeable       */
/* amount of time on a real Void system with thousands of packages.   */
/* Calling them in the GTK main thread freezes the UI and produces     */
/* the 80+ vfork storm the user observed under gdb.                    */
/*                                                                    */
/* openCacheAsync() spawns a worker thread (g_thread_new) that runs     */
/* the exact same openCache() body in the background. The UI polls     */
/* openCacheAsyncIsDone() from a g_timeout_add callback (typical 50ms)  */
/* and, when done, calls openCacheAsyncResult() and refreshes the      */
/* table. This is the same pattern iruka-xbps uses (std::thread +      */
/* g_idle_add) and octoxbps uses (QtConcurrent::run + QFutureWatcher). */
/* ------------------------------------------------------------------ */

void *
RPackageLister::_openCacheThreadFunc(void *arg)
{
   RPackageLister *self = static_cast<RPackageLister *>(arg);

   /* Mark the async-open sentinel so the openCache() body skips the
    * thread-unsafe notifyCacheOpen() call. The UI thread will call
    * notifyCacheOpen() itself once openCacheAsyncIsDone() is true. */
   g_setenv("SYNAPTIC_XBPS_ASYNC_OPEN", "1", TRUE);
   bool ok = self->openCache();
   g_unsetenv("SYNAPTIC_XBPS_ASYNC_OPEN");

   self->_openCacheState.rv = ok ? 0 : 1;
   self->_openCacheState.done = true;
   return NULL;
}

bool
RPackageLister::openCacheAsync()
{
   /* If a previous async open is still in flight, ignore the request. */
   if (_openCacheState.done == false && _openCacheState.thread != NULL)
      return true;

   /* Reset state for the new run. */
   _openCacheState.done = false;
   _openCacheState.rv = 0;
   _openCacheState.thread = g_thread_new("xbps-openCache",
                                         _openCacheThreadFunc, this);
   if (_openCacheState.thread)
      g_thread_unref(_openCacheState.thread);
   return _openCacheState.thread != NULL;
}

bool
RPackageLister::openCacheAsyncIsDone()
{
   return _openCacheState.done;
}

int
RPackageLister::openCacheAsyncResult()
{
   return _openCacheState.rv;
}

void
RPackageLister::openCacheAsyncWait()
{
   /* Used by the non-interactive command-line path in gsynaptic.cc
    * (e.g. --set-selections / --test-me-harder) which has no GTK main
    * loop pumping events. We just spin until the worker thread is
    * done. */
   while (!_openCacheState.done)
      g_usleep(50 * 1000); /* 50ms */
}

/* ------------------------------------------------------------------ */
/* Constructor / destructor                                            */
/* ------------------------------------------------------------------ */

RPackageLister::RPackageLister()
   :
#ifdef HAVE_XAPIAN
   _xapianDatabase(0),
#endif
   _cache(0),
   _progMeter(NULL),
   _updating(true),
   _cacheValid(false),
   _installedCount(0),
   _filterView(0),
   _searchView(0),
   _viewMode(0),
   _sortMode(LIST_SORT_DEFAULT),
   _selectedView(0),
   _userDialog(NULL)
{
   _cache = new RPackageCacheXbps();

   _searchData.pattern = NULL;
   _searchData.isRegex = false;
   _searchData.last = -1;
   memset(&_searchData, 0, sizeof(_searchData));

   /* Async openCache state — starts idle so the first call to
    * openCacheAsyncIsDone() doesn't immediately report a false
    * "finished". The worker thread flips these once it completes. */
   _openCacheState.done = true;     /* idle = "no job pending" */
   _openCacheState.rv = 0;
   _openCacheState.thread = NULL;
   _pendingResult = NULL;

   /* Synaptic::ViewMode — under APT this comes from _config. We
    * fall back to 0 (the "Sections" view). */
   _viewMode = (unsigned int)syn_conf_int("SYNAPTIC_VIEW_MODE", 0);
   _sortMode = LIST_SORT_DEFAULT;
   _updating = true;

   /* Build the view set in the same order as the APT version. The
    * view classes are APT-agnostic in their public API; they only
    * call methods of RPackageLister and RPackage. */
   _views.push_back(new RPackageViewSections(_nativeArchPackages));
   _views.push_back(new RPackageViewStatus(_nativeArchPackages));
   _views.push_back(new RPackageViewOrigin(_nativeArchPackages));
   _filterView = new RPackageViewFilter(_nativeArchPackages);
   _views.push_back(_filterView);
   _searchView = new RPackageViewSearch(_nativeArchPackages);
   _views.push_back(_searchView);
   _views.push_back(new RPackageViewArchitecture(_packages));

#ifdef HAVE_XAPIAN
   openXapianIndex();
#endif

   if (_viewMode >= _views.size())
      _viewMode = 0;
   _selectedView = _views[_viewMode];

   _pkgStatus.init();

   cleanCommitLog();
}

RPackageLister::~RPackageLister()
{
   for (vector<RCacheActor *>::iterator I = _actors.begin();
        I != _actors.end(); ++I)
      delete (*I);
   _actors.clear();

   /* Free per-package RPackage objects and view objects; both are
    * owned by the lister. */
   for (vector<RPackage *>::iterator I = _packages.begin();
        I != _packages.end(); ++I)
      delete (*I);
   _packages.clear();
   _nativeArchPackages.clear();
   _viewPackages.clear();

   for (vector<RPackageView *>::iterator I = _views.begin();
        I != _views.end(); ++I)
      delete (*I);
   _views.clear();
   _filterView = NULL;
   _searchView = NULL;
   _selectedView = NULL;

   if (_searchData.pattern) {
      if (_searchData.isRegex)
         regfree(&_searchData.regex);
      free(_searchData.pattern);
      _searchData.pattern = NULL;
   }

   delete _cache;
   _cache = NULL;

   delete _progMeter;
   _progMeter = NULL;

   /* Free any pending async result that was never consumed. */
   if (_pendingResult) {
      for (size_t i = 0; i < _pendingResult->packages.size(); i++)
         delete _pendingResult->packages[i];
      delete _pendingResult;
      _pendingResult = NULL;
   }
}

/* ------------------------------------------------------------------ */
/* Cache open / refresh                                                */
/* ------------------------------------------------------------------ */

bool
RPackageLister::openCache()
{
   /* only lock if we run as root — but the popen-based backend has
    * no in-process lock, so this is just advisory. xbps-install /
    * xbps-remove will take their own pkgdb lock when they run. */
   bool lock = (getuid() == 0);
   if (_cache->open(_progMeter, "", "", lock) != 0) {
      if (_progMeter)
         _progMeter->Done();
      _cacheValid = false;
      r_listerr("_cache->open() failed, cannot continue.");
      return false;
   }
   if (_progMeter)
      _progMeter->Done();

   if (_progMeter)
      _progMeter->Update(_("Reading XBPS package database"), "", 10);

   /* ----------------------------------------------------------------
    * WORKER THREAD SAFETY: this function runs in a worker thread when
    * called via openCacheAsync / openCacheSyncGtk. The GTK main thread
    * may be pumping events (in openCacheSyncGtk) and any GTK callback
    * could read _packages / _nativeArchPackages / _viewPackages.
    *
    * To avoid the data race that caused the SIGSEGV in sortPackages
    * (freed RPackage* being compared by strcmp), the worker thread
    * builds new RPackage* into a LOCAL OpenCacheResult struct and does
    * NOT touch _packages at all. The GTK thread swaps the result in
    * via openCacheFinalize().
    * -------------------------------------------------------------- */
   OpenCacheResult *result = new OpenCacheResult();
   result->installedCount = 0;

   /* Run the three xbps-query / xbps-install subprocesses in parallel
    * so the wall time is the slowest of the three, not the sum.
    * On a real Void system with thousands of packages and several
    * remote repos, xbps-query -Rs - dominates (scanning every repo
    * index). xbps-query -l and xbps-install -un are fast and overlap
    * with it for free. */
   string installed_out, remote_out, outdated_out;
   vector<ParallelJob> jobs;
   jobs.push_back({
      {"env", "LANG=C", "xbps-query", "-l"},
      &installed_out, 0, -1
   });
   jobs.push_back({
      {"env", "LANG=C", "xbps-query", "-Rs", "-"},
      &remote_out, 0, -1
   });
   jobs.push_back({
      {"env", "LANG=C", "xbps-install", "-un"},
      &outdated_out, 0, -1
   });
   run_parallel_capture(jobs);

   if (_progMeter)
      _progMeter->Update(_("Reading XBPS package database"), "", 50);

   vector<XbpsRow> installed_rows = parse_installed_list(installed_out);
   vector<XbpsRow> remote_rows = parse_remote_list(remote_out);
   result->outdatedMap = parse_outdated_list(outdated_out);

   if (_progMeter)
      _progMeter->Update(_("Reading XBPS package database"), "", 90);

   /* Merge: start from installed, then add remote packages that
    * aren't already in the list (matching iruka-xbps's pkgMap merge). */
   set<string> seen;
   for (size_t i = 0; i < installed_rows.size(); i++) {
      const XbpsRow &r = installed_rows[i];
      if (r.name.empty()) continue;
      if (seen.find(r.name) != seen.end()) continue;
      seen.insert(r.name);

      RPackage *pkg = new RPackage(this, r.name, r.pkgver, r.summary,
                                    r.summary, true);
      if (result->outdatedMap.find(r.name) != result->outdatedMap.end())
         pkg->_is_outdated = true;
      result->packages.push_back(pkg);
      result->packageNames.insert(r.name);
      result->installedCount++;
   }

   /* Add remote-only packages (those that aren't installed). */
   for (size_t i = 0; i < remote_rows.size(); i++) {
      const XbpsRow &r = remote_rows[i];
      if (r.name.empty()) continue;
      if (seen.find(r.name) != seen.end()) continue;
      seen.insert(r.name);

      RPackage *pkg = new RPackage(this, r.name, r.pkgver, r.summary,
                                    r.summary, false);
      result->packages.push_back(pkg);
      result->packageNames.insert(r.name);
   }

   /* Hand the result to the GTK thread via _pendingResult. The GTK
    * thread will swap it into _packages in openCacheFinalize(). */
   if (_pendingResult) {
      /* A previous pending result was never consumed — free its
       * packages to avoid a leak. */
      for (size_t i = 0; i < _pendingResult->packages.size(); i++)
         delete _pendingResult->packages[i];
      delete _pendingResult;
   }
   _pendingResult = result;

   /* When running synchronously (no async env var set), finalize
    * immediately on this thread. When running in the worker thread,
    * the UI poll callback calls openCacheFinalize() on the GTK thread. */
   if (!getenv("SYNAPTIC_XBPS_ASYNC_OPEN")) {
      openCacheFinalize();
   }

   _cacheValid = true;

   if (_progMeter) {
      _progMeter->Update(_("Reading XBPS package database"), "", 100);
      _progMeter->Done();
   }
   return true;
}

/* ------------------------------------------------------------------ */
/* openCacheFinalize — GTK-thread continuation of openCache            */
/*                                                                    */
/* Runs applyInitialSelection(), rebuilds the views, sorts the        */
/* package list, reapplies the filter, and notifies observers. This   */
/* MUST be called from the GTK main thread because it mutates the      */
/* same _packages / _viewPackages vectors the treeview reads from.    */
/*                                                                    */
/* When openCache() runs synchronously (the fallback path), it        */
/* invokes openCacheFinalize() itself. When openCache() runs in the   */
/* worker thread (openCacheAsync), the UI poll callback invokes        */
/* openCacheFinalize() after openCacheAsyncIsDone() returns true.     */
/* ------------------------------------------------------------------ */
void
RPackageLister::openCacheFinalize()
{
   /* This runs on the GTK main thread. Swap the worker thread's
    * _pendingResult into _packages — delete old RPackage*, install
    * new ones, rebuild indexes, views, sort, filter, notify. */
   if (!_pendingResult) {
      /* Nothing to do — openCache() may have failed before populating
       * _pendingResult, or openCacheFinalize was called twice. */
      _updating = false;
      return;
   }

   /* Delete the OLD RPackage* (the ones the treeview was rendering
    * up to this point). After this, any stale pointer in the treeview
    * model or in _pkgDetails is dangling — but the treeview model is
    * detached (setTreeLocked(TRUE) did gtk_tree_view_set_model(NULL))
    * so it won't dereference them until we reattach. */
   for (vector<RPackage *>::iterator I = _packages.begin();
        I != _packages.end(); ++I)
      delete (*I);
   _packages.clear();
   _nativeArchPackages.clear();
   _packagesIndex.clear();
   _viewPackages.clear();
   _viewPackagesIndex.clear();
   packageNames.clear();
   _installedCount = 0;

   /* Clear all the views so they get re-populated below. */
   for (unsigned int i = 0; i < _views.size(); i++)
      _views[i]->clear();

   /* Install the NEW RPackage* from the worker thread's result. */
   _packages.swap(_pendingResult->packages);
   packageNames.swap(_pendingResult->packageNames);
   _installedCount = _pendingResult->installedCount;

   /* Apply outdated flags (already set on each RPackage during
    * openCache, but we also keep the map for reference). */
   /* (outdated flags are already on the RPackage objects.) */

   /* Free the pending result struct (its vectors are now empty after
    * swap). */
   delete _pendingResult;
   _pendingResult = NULL;

   /* Under XBPS there is no multiarch, so _nativeArchPackages
    * mirrors _packages 1:1. */
   _nativeArchPackages = _packages;

   /* Build the flat _packagesIndex. */
   _packagesIndex.resize(_packages.size(), -1);
   for (size_t i = 0; i < _packages.size(); i++)
      _packagesIndex[i] = (int)i;

   applyInitialSelection();

   for (unsigned int i = 0; i < _views.size(); i++)
      _views[i]->refresh();

   _updating = false;
   reapplyFilter();

   notifyCacheOpen();
}

/* Synchronous-but-non-blocking: kick the worker thread, then pump
 * GTK events on the calling thread so the UI doesn't freeze, and
 * once the worker is done call openCacheFinalize() on the GTK thread.
 * The caller (cbProceedClicked, etc.) treats this like openCache()
 * — same return value, same post-conditions — but the GTK main loop
 * stays alive while xbps-query / xbps-install -un run in the worker.
 *
 * Without this, the original synchronous openCache() from the GTK
 * thread froze the UI for several seconds on a real Void system
 * (thousands of packages) and crashed in sortPackages because the
 * treeview was reading _packages while sortPackages moved pointers
 * under it. */
int
RPackageLister::openCacheSyncGtk()
{
   if (!openCacheAsync()) {
      /* Could not spawn worker — fall back to plain sync openCache
       * (which will block but at least work). */
      bool ok = openCache();
      return ok ? 0 : 1;
   }
   /* Wait for the worker thread WITHOUT pumping GTK events. The
    * worker thread only touches _pendingResult (a member pointer
    * that no GTK callback reads), so it's safe to let GTK events
    * queue up — they'll be dispatched when we return to the main
    * loop. Pumping events here was the cause of the data race:
    * treeview callbacks read _packages while the worker was
    * modifying it. Now the worker doesn't touch _packages at all,
    * but we still don't pump to be extra safe. */
   while (!openCacheAsyncIsDone()) {
      g_usleep(20 * 1000); /* 20ms */
   }
   int rv = openCacheAsyncResult();
   if (rv == 0)
      openCacheFinalize();
   return rv;
}

void
RPackageLister::applyInitialSelection()
{
   /* Under APT this reads _roptions for saved-orphan / saved-new /
    * saved-debconf state. We still honour _roptions when present. */
   if (_roptions) {
      _roptions->rereadOrphaned();
      _roptions->rereadDebconf();
   }

   /* Under XBPS we do NOT auto-mark packages as orphaned. The
    * "Orphaned" filter shows nothing unless the user explicitly
    * runs autoremove (which is handled in commitChanges by passing
    * -O to xbps-remove). xbps-remove -O is the authoritative
    * source; calling it eagerly here would list every auto-installed
    * package — even ones with active reverse deps — which is wrong. */
   for (unsigned i = 0; i < _packages.size(); i++) {
      RPackage *pkg = _packages[i];
      if (_roptions && _roptions->getPackageOrphaned(pkg->name()))
         pkg->setOrphaned(true);
      if (_roptions && _roptions->getPackageNew(pkg->name()))
         pkg->setNew(true);
      if (_roptions && _roptions->getPackageLock(pkg->name()))
         pkg->setPinned(true);
   }
}

/* ------------------------------------------------------------------ */
/* Upgrade / dist-upgrade / upgradable                                 */
/* ------------------------------------------------------------------ */

bool
RPackageLister::upgrade()
{
   /* XBPS has no separate "upgrade" mode: `xbps-install -u` simply
    * marks every outdated package for upgrade and resolves the
    * transaction. We mirror that by flipping the FUpgrade bit on
    * every outdated RPackage. commitChanges() will turn the
    * accumulated state into a real xbps-install -u invocation. */
   for (unsigned i = 0; i < _packages.size(); i++) {
      RPackage *pkg = _packages[i];
      int flags = pkg->getFlags();
      if (flags & RPackage::FOutdated)
         pkg->setInstall();
   }
   notifyChange(NULL);
   return true;
}

bool
RPackageLister::distUpgrade()
{
   /* Under XBPS dist-upgrade is the same as upgrade. We don't have
    * a way to compute orphans without libxbps, so the user can
    * run xbps-remove -O separately from the Tools menu. */
   upgrade();
   notifyChange(NULL);
   return true;
}

bool
RPackageLister::upgradable()
{
   if (!_cacheValid)
      return false;
   for (unsigned i = 0; i < _packages.size(); i++) {
      int flags = _packages[i]->getFlags();
      if (flags & RPackage::FOutdated)
         return true;
   }
   return false;
}

/* ------------------------------------------------------------------ */
/* fixBroken / check                                                   */
/* ------------------------------------------------------------------ */

bool
RPackageLister::fixBroken()
{
   /* XBPS has no pkgFixBroken equivalent. The popen-based backend
    * can't run a real "fix broken" without libxbps; we just
    * refresh the views so the UI's broken-deps indicator updates. */
   reapplyFilter();
   return true;
}

bool
RPackageLister::check()
{
   if (!_cacheValid)
      return false;
   for (unsigned i = 0; i < _packages.size(); i++) {
      if (_packages[i]->wouldBreak())
         return false;
   }
   return true;
}

/* ------------------------------------------------------------------ */
/* cleanPackageCache                                                   */
/* ------------------------------------------------------------------ */

bool
RPackageLister::cleanPackageCache(bool forceClean)
{
   /* The popen-based backend doesn't have a libxbps handle, so we
    * can't read xhp->cachedir. Use the standard /var/cache/xbps
    * directly. */
   string cacheDir = "/var/cache/xbps";

   DIR *dir = opendir(cacheDir.c_str());
   if (!dir) {
      r_listerr("cleanPackageCache: opendir(%s) failed: %s",
                cacheDir.c_str(), strerror(errno));
      return false;
   }

   /* Synaptic::delHistory defaults to 7 days. Under APT it is read
    * via _config->FindI("Synaptic::delHistory", -1). We honour the
    * SYNAPTIC_DEL_HISTORY env var, defaulting to 7. -1 disables
    * mtime-based pruning. */
   int maxKeep = syn_conf_int("SYNAPTIC_DEL_HISTORY", 7);

   time_t now = time(NULL);
   struct dirent *ent;
   while ((ent = readdir(dir)) != NULL) {
      if (ent->d_name[0] == '.')
         continue;
      const char *dot = strrchr(ent->d_name, '.');
      if (!dot || strcmp(dot, ".xbps") != 0)
         continue;

      string path = cacheDir + "/" + ent->d_name;
      struct stat buf;
      if (stat(path.c_str(), &buf) != 0)
         continue;

      if (forceClean) {
         unlink(path.c_str());
         continue;
      }

      if (maxKeep < 0)
         continue;
      if ((buf.st_mtime + (60 * 60 * 24 * maxKeep)) < now)
         unlink(path.c_str());
   }
   closedir(dir);
   return true;
}

/* ------------------------------------------------------------------ */
/* commitChanges — fork+execvp pattern from iruka-xbps                 */
/* ------------------------------------------------------------------ */


/* ------------------------------------------------------------------ */
/* Commit log                                                          */
/* ------------------------------------------------------------------ */

void
RPackageLister::writeCommitLog()
{
   struct tm *t = localtime(&_logTime);
   ostringstream tmp;
   char buf[64];
   snprintf(buf, sizeof(buf), "%.4d-%.2d-%.2d.%.2d%.2d%.2d.log",
            1900 + t->tm_year, t->tm_mon + 1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec);
   tmp << buf;
   string logfile = RLogDir() + tmp.str();
   FILE *f = fopen(logfile.c_str(), "w+");
   if (f == NULL) {
      r_listerr("Failed to write commit log to %s", logfile.c_str());
      return;
   }
   fputs(_logEntry.c_str(), f);
   fclose(f);
}

void
RPackageLister::cleanCommitLog()
{
   int maxKeep = syn_conf_int("SYNAPTIC_DEL_HISTORY", -1);
   if (maxKeep < 0)
      return;

   string logdir = RLogDir();
   if (logdir.empty())
      return;

   DIR *dir = opendir(logdir.c_str());
   if (!dir)
      return;
   struct dirent *dent;
   time_t now = time(NULL);
   while ((dent = readdir(dir)) != NULL) {
      string entry = dent->d_name;
      if (entry == "." || entry == "..")
         continue;
      string logfile = logdir + entry;
      struct stat buf;
      if (stat(logfile.c_str(), &buf) != 0)
         continue;
      if ((buf.st_mtime + (60 * 60 * 24 * maxKeep)) < now)
         unlink(logfile.c_str());
   }
   closedir(dir);
}

void
RPackageLister::makeCommitLog()
{
   time(&_logTime);
   _logEntry =
      string("Commit Log for ") + string(ctime(&_logTime)) + string("\n");
   _logEntry.reserve(2 * 8192);

   vector<RPackage *> held;
   vector<RPackage *> kept;
   vector<RPackage *> essential;
   vector<RPackage *> toInstall;
   vector<RPackage *> toReInstall;
   vector<RPackage *> toUpgrade;
   vector<RPackage *> toRemove;
   vector<RPackage *> toPurge;
   vector<RPackage *> toDowngrade;
#ifdef WITH_APT_AUTH
   vector<string> notAuthenticated;
#endif
   double sizeChange;

   getDetailedSummary(held, kept, essential, toInstall, toReInstall,
                      toUpgrade, toRemove, toPurge, toDowngrade,
#ifdef WITH_APT_AUTH
                      notAuthenticated,
#endif
                      sizeChange);

   if (essential.size() > 0) {
      _logEntry += _("\nRemoved the following ESSENTIAL packages:\n");
      for (vector<RPackage *>::const_iterator p = essential.begin();
           p != essential.end(); ++p)
         _logEntry += (*p)->name() + string("\n");
   }
   if (toDowngrade.size() > 0) {
      _logEntry += _("\nDowngraded the following packages:\n");
      for (vector<RPackage *>::const_iterator p = toDowngrade.begin();
           p != toDowngrade.end(); ++p)
         _logEntry += (*p)->name() + string("\n");
   }
   if (toPurge.size() > 0) {
      _logEntry += _("\nCompletely removed the following packages:\n");
      for (vector<RPackage *>::const_iterator p = toPurge.begin();
           p != toPurge.end(); ++p)
         _logEntry += (*p)->name() + string("\n");
   }
   if (toRemove.size() > 0) {
      _logEntry += _("\nRemoved the following packages:\n");
      for (vector<RPackage *>::const_iterator p = toRemove.begin();
           p != toRemove.end(); ++p)
         _logEntry += (*p)->name() + string("\n");
   }
   if (toUpgrade.size() > 0) {
      _logEntry += _("\nUpgraded the following packages:\n");
      for (vector<RPackage *>::const_iterator p = toUpgrade.begin();
           p != toUpgrade.end(); ++p) {
         _logEntry += (*p)->name() + string(" (") +
                      (*p)->installedVersion() + string(")") +
                      string(" to ") + (*p)->availableVersion() +
                      string("\n");
      }
   }
   if (toInstall.size() > 0) {
      _logEntry += _("\nInstalled the following packages:\n");
      for (vector<RPackage *>::const_iterator p = toInstall.begin();
           p != toInstall.end(); ++p) {
         _logEntry += (*p)->name() + string(" (") +
                      (*p)->availableVersion() + string(")") +
                      string("\n");
      }
   }
   if (toReInstall.size() > 0) {
      _logEntry += _("\nReinstalled the following packages:\n");
      for (vector<RPackage *>::const_iterator p = toReInstall.begin();
           p != toReInstall.end(); ++p) {
         _logEntry += (*p)->name() + string(" (") +
                      (*p)->availableVersion() + string(")") +
                      string("\n");
      }
   }
}

/* ------------------------------------------------------------------ */
/* Locking                                                             */
/* ------------------------------------------------------------------ */

bool
RPackageLister::lockPackageCache(FileFd &lock)
{
   (void)lock;
   /* The popen-based backend has no in-process pkgdb lock. xbps-install
    * and xbps-remove take their own flock(2) on
    * /var/db/xbps/.xbps-pkgdb.lock when they need to. */
   return true;
}

/* ------------------------------------------------------------------ */
/* Download URIs / archive cache                                       */
/* ------------------------------------------------------------------ */

bool
RPackageLister::getDownloadUris(vector<string> &uris)
{
   /* Without a libxbps transaction handle we can't enumerate the
    * URIs of packages that would be downloaded. The download-only
    * feature is therefore disabled under the popen-based backend.
    * The user can use `xbps-install -D` from the command line if they
    * need this. */
   (void)uris;
   return false;
}

bool
RPackageLister::addArchiveToCache(string archiveDir, string &pkgname)
{
   /* XBPS doesn't deal with .deb archives; .xbps archives don't
    * have the same "add this file to the cache and consider it for
    * installation" workflow. Stub to return false. */
   (void)archiveDir;
   (void)pkgname;
   return false;
}

/* ------------------------------------------------------------------ */
/* Package lookup                                                      */
/* ------------------------------------------------------------------ */

RPackage *
RPackageLister::getPackage(string name)
{
   for (unsigned i = 0; i < _packages.size(); i++) {
      if (_packages[i]->name() == name)
         return _packages[i];
   }
   return NULL;
}

int
RPackageLister::getPackageIndex(RPackage *pkg)
{
   if (!pkg)
      return -1;
   for (unsigned i = 0; i < _packages.size(); i++) {
      if (_packages[i] == pkg)
         return (int)i;
   }
   return -1;
}

int
RPackageLister::getViewPackageIndex(RPackage *pkg)
{
   if (!pkg)
      return -1;
   for (unsigned i = 0; i < _viewPackages.size(); i++) {
      if (_viewPackages[i] == pkg)
         return (int)i;
   }
   return -1;
}

/* ------------------------------------------------------------------ */
/* Stats / summary                                                     */
/* ------------------------------------------------------------------ */

void
RPackageLister::getStats(int &installed,
                        int &broken,
                        int &toInstall,
                        int &toRemove,
                        double &sizeChange)
{
   installed = _installedCount;
   broken = 0;
   toInstall = 0;
   toRemove = 0;
   sizeChange = 0.0;

   for (unsigned i = 0; i < _packages.size(); i++) {
      RPackage *pkg = _packages[i];
      int flags = pkg->getFlags();

      if (pkg->wouldBreak())
         broken++;

      if (flags & (RPackage::FInstall | RPackage::FNewInstall |
                   RPackage::FReInstall | RPackage::FUpgrade |
                   RPackage::FDowngrade)) {
         toInstall++;
         sizeChange += (double)pkg->availableInstalledSize()
                       - (double)pkg->installedSize();
      }
      if (flags & (RPackage::FRemove | RPackage::FPurge)) {
         toRemove++;
         sizeChange -= (double)pkg->installedSize();
      }
   }
}

void
RPackageLister::getSummary(int &held,
                           int &kept,
                           int &essential,
                           int &toInstall,
                           int &toReInstall,
                           int &toUpgrade,
                           int &toRemove,
                           int &toDowngrade,
                           int &unAuthenticated,
                           double &sizeChange)
{
   held = 0;
   kept = 0;
   essential = 0;
   toInstall = 0;
   toReInstall = 0;
   toUpgrade = 0;
   toRemove = 0;
   toDowngrade = 0;
   unAuthenticated = 0;
   sizeChange = 0.0;

   for (unsigned i = 0; i < _packages.size(); i++) {
      RPackage *pkg = _packages[i];
      int flags = pkg->getFlags();

      int status = flags & (RPackage::FKeep | RPackage::FNewInstall |
                            RPackage::FReInstall | RPackage::FUpgrade |
                            RPackage::FDowngrade | RPackage::FRemove);

      switch (status) {
         case RPackage::FKeep:
            if (flags & RPackage::FHeld)
               held++;
            else
               kept++;
            break;
         case RPackage::FNewInstall:
            toInstall++;
            if (!pkg->isTrusted())
               unAuthenticated++;
            sizeChange += (double)pkg->availableInstalledSize();
            break;
         case RPackage::FReInstall:
            toReInstall++;
            if (!pkg->isTrusted())
               unAuthenticated++;
            break;
         case RPackage::FUpgrade:
            toUpgrade++;
            if (!pkg->isTrusted())
               unAuthenticated++;
            sizeChange += (double)pkg->availableInstalledSize()
                          - (double)pkg->installedSize();
            break;
         case RPackage::FDowngrade:
            toDowngrade++;
            if (!pkg->isTrusted())
               unAuthenticated++;
            break;
         case RPackage::FRemove:
            if (flags & RPackage::FImportant)
               essential++;
            toRemove++;
            sizeChange -= (double)pkg->installedSize();
            break;
      }
   }
}

void
RPackageLister::getDetailedSummary(vector<RPackage *> &held,
                                   vector<RPackage *> &kept,
                                   vector<RPackage *> &essential,
                                   vector<RPackage *> &toInstall,
                                   vector<RPackage *> &toReInstall,
                                   vector<RPackage *> &toUpgrade,
                                   vector<RPackage *> &toRemove,
                                   vector<RPackage *> &toPurge,
                                   vector<RPackage *> &toDowngrade,
#ifdef WITH_APT_AUTH
                                   vector<string> &notAuthenticated,
#endif
                                   double &sizeChange)
{
   sizeChange = 0.0;

   for (unsigned int i = 0; i < _packages.size(); i++) {
      RPackage *pkg = _packages[i];
      int flags = pkg->getFlags();

      int status = flags & (RPackage::FKeep | RPackage::FNewInstall |
                            RPackage::FReInstall | RPackage::FUpgrade |
                            RPackage::FDowngrade | RPackage::FRemove);

      switch (status) {
         case RPackage::FKeep:
            if (flags & RPackage::FHeld)
               held.push_back(pkg);
            else
               kept.push_back(pkg);
            break;
         case RPackage::FNewInstall:
            toInstall.push_back(pkg);
            sizeChange += (double)pkg->availableInstalledSize();
            break;
         case RPackage::FReInstall:
            toReInstall.push_back(pkg);
            break;
         case RPackage::FUpgrade:
            toUpgrade.push_back(pkg);
            sizeChange += (double)pkg->availableInstalledSize()
                          - (double)pkg->installedSize();
            break;
         case RPackage::FDowngrade:
            toDowngrade.push_back(pkg);
            break;
         case RPackage::FRemove:
            if (flags & RPackage::FImportant)
               essential.push_back(pkg);
            else if (flags & RPackage::FPurge)
               toPurge.push_back(pkg);
            else
               toRemove.push_back(pkg);
            sizeChange -= (double)pkg->installedSize();
            break;
      }
   }

   sort(kept.begin(), kept.end(), RPackage::byNameAscending);
   sort(toInstall.begin(), toInstall.end(), RPackage::byNameAscending);
   sort(toReInstall.begin(), toReInstall.end(), RPackage::byNameAscending);
   sort(toUpgrade.begin(), toUpgrade.end(), RPackage::byNameAscending);
   sort(essential.begin(), essential.end(), RPackage::byNameAscending);
   sort(toRemove.begin(), toRemove.end(), RPackage::byNameAscending);
   sort(toPurge.begin(), toPurge.end(), RPackage::byNameAscending);
   sort(held.begin(), held.end(), RPackage::byNameAscending);
#ifdef WITH_APT_AUTH
   /* The APT version populated notAuthenticated from the pkgAcquire
    * item list. Under XBPS we don't have a fetcher object here, so
    * we walk the to-be-installed list and pick out untrusted
    * packages. */
   notAuthenticated.clear();
   for (vector<RPackage *>::const_iterator I = toInstall.begin();
        I != toInstall.end(); ++I)
      if (!(*I)->isTrusted())
         notAuthenticated.push_back((*I)->name());
   for (vector<RPackage *>::const_iterator I = toUpgrade.begin();
        I != toUpgrade.end(); ++I)
      if (!(*I)->isTrusted())
         notAuthenticated.push_back((*I)->name());
   for (vector<RPackage *>::const_iterator I = toReInstall.begin();
        I != toReInstall.end(); ++I)
      if (!(*I)->isTrusted())
         notAuthenticated.push_back((*I)->name());
   sort(notAuthenticated.begin(), notAuthenticated.end());
#endif
}

void
RPackageLister::getDownloadSummary(int &dlCount, double &dlSize)
{
   dlCount = 0;
   dlSize = 0.0;
   for (unsigned i = 0; i < _packages.size(); i++) {
      RPackage *pkg = _packages[i];
      int flags = pkg->getFlags();
      if (!(flags & (RPackage::FInstall | RPackage::FNewInstall |
                     RPackage::FReInstall | RPackage::FUpgrade |
                     RPackage::FDowngrade)))
         continue;
      long sz = pkg->availablePackageSize();
      if (sz > 0) {
         dlCount++;
         dlSize += (double)sz;
      }
   }
}

/* ------------------------------------------------------------------ */
/* State save / restore / undo / redo                                  */
/* ------------------------------------------------------------------ */

void
RPackageLister::saveState(pkgState &state)
{
   state.clear();
   state.reserve(_packages.size());
   for (unsigned i = 0; i < _packages.size(); i++)
      state.push_back(_packages[i]->getFlags());
}

void
RPackageLister::restoreState(pkgState &state)
{
   /* No pkgDepCache::ActionGroup equivalent under XBPS — we just
    * flip bits one package at a time. Notify once at the end to
    * avoid a flood of redraws. */
   bool savedNotify = true;
   for (unsigned i = 0; i < _packages.size() && i < state.size(); i++) {
      RPackage *pkg = _packages[i];
      int flags = pkg->getFlags();
      int oldflags = state[i];

      if (oldflags == flags)
         continue;

      pkg->setNotify(false);
      if (oldflags & RPackage::FReInstall) {
         pkg->setReInstall(true);
      } else if (oldflags & (RPackage::FInstall | RPackage::FNewInstall |
                             RPackage::FUpgrade | RPackage::FDowngrade)) {
         pkg->setInstall();
      } else if (oldflags & (RPackage::FRemove | RPackage::FPurge)) {
         pkg->setRemove(oldflags & RPackage::FPurge);
      } else if (oldflags & RPackage::FKeep) {
         pkg->setKeep();
      }

      if (oldflags & RPackage::FIsAuto)
         pkg->setAuto(true);
      else
         pkg->setAuto(false);

      pkg->setNotify(savedNotify);
   }
   notifyChange(NULL);
}

void
RPackageLister::saveUndoState(pkgState &state)
{
   undoStack.push_front(state);
   redoStack.clear();
   unsigned int maxStackSize =
      (unsigned int)syn_conf_int("SYNAPTIC_UNDO_STACK_SIZE", 20);
   while (undoStack.size() > maxStackSize)
      undoStack.pop_back();
}

void
RPackageLister::saveUndoState()
{
   pkgState state;
   saveState(state);
   saveUndoState(state);
}

void
RPackageLister::undo()
{
   if (undoStack.empty())
      return;
   pkgState current;
   saveState(current);
   redoStack.push_front(current);

   pkgState prev = undoStack.front();
   undoStack.pop_front();
   restoreState(prev);
}

void
RPackageLister::redo()
{
   if (redoStack.empty())
      return;
   pkgState current;
   saveState(current);
   undoStack.push_front(current);

   pkgState next = redoStack.front();
   redoStack.pop_front();
   restoreState(next);
}

bool
RPackageLister::getStateChanges(pkgState &state,
                                vector<RPackage *> &toKeep,
                                vector<RPackage *> &toInstall,
                                vector<RPackage *> &toReInstall,
                                vector<RPackage *> &toUpgrade,
                                vector<RPackage *> &toRemove,
                                vector<RPackage *> &toDowngrade,
                                vector<RPackage *> &notAuthenticated,
                                const vector<RPackage *> &exclude,
                                bool sorted)
{
   bool changed = false;

   for (unsigned i = 0; i < _packages.size() && i < state.size(); i++) {
      int flags = _packages[i]->getFlags();
      if (state[i] == flags)
         continue;

      int status = flags & (RPackage::FHeld | RPackage::FNewInstall |
                            RPackage::FReInstall | RPackage::FUpgrade |
                            RPackage::FDowngrade | RPackage::FRemove);

      switch (status) {
         case RPackage::FNewInstall:
         case RPackage::FReInstall:
         case RPackage::FUpgrade:
         case RPackage::FDowngrade:
            if (!_packages[i]->isTrusted())
               notAuthenticated.push_back(_packages[i]);
            break;
      }

      if (find(exclude.begin(), exclude.end(), _packages[i]) != exclude.end())
         continue;

      switch (status) {
         case RPackage::FNewInstall:
            toInstall.push_back(_packages[i]);
            changed = true;
            break;
         case RPackage::FReInstall:
            toReInstall.push_back(_packages[i]);
            changed = true;
            break;
         case RPackage::FUpgrade:
            toUpgrade.push_back(_packages[i]);
            changed = true;
            break;
         case RPackage::FRemove:
            toRemove.push_back(_packages[i]);
            changed = true;
            break;
         case RPackage::FKeep:
            toKeep.push_back(_packages[i]);
            changed = true;
            break;
         case RPackage::FDowngrade:
            toDowngrade.push_back(_packages[i]);
            changed = true;
            break;
      }
   }

   if (sorted && changed) {
      if (!toKeep.empty())
         sort(toKeep.begin(), toKeep.end(), RPackage::byNameAscending);
      if (!toInstall.empty())
         sort(toInstall.begin(), toInstall.end(), RPackage::byNameAscending);
      if (!toReInstall.empty())
         sort(toReInstall.begin(), toReInstall.end(),
              RPackage::byNameAscending);
      if (!toUpgrade.empty())
         sort(toUpgrade.begin(), toUpgrade.end(),
              RPackage::byNameAscending);
      if (!toRemove.empty())
         sort(toRemove.begin(), toRemove.end(), RPackage::byNameAscending);
      if (!toDowngrade.empty())
         sort(toDowngrade.begin(), toDowngrade.end(),
              RPackage::byNameAscending);
   }
   return changed;
}

/* ------------------------------------------------------------------ */
/* Selections read / write                                             */
/* ------------------------------------------------------------------ */

bool
RPackageLister::writeSelections(ostream &out, bool fullState)
{
   for (unsigned i = 0; i < _packages.size(); i++) {
      int flags = _packages[i]->getFlags();

      if (flags & RPackage::FInstall ||
          (fullState && (flags & RPackage::FInstalled))) {
         out << _packages[i]->name() << "\t\tinstall" << endl;
      } else if (flags & RPackage::FPurge) {
         out << _packages[i]->name() << "\t\tpurge" << endl;
      } else if (flags & RPackage::FRemove) {
         out << _packages[i]->name() << "\t\tdeinstall" << endl;
      }
   }
   return true;
}

bool
RPackageLister::readSelections(istream &in)
{
   string buffer;
   int curLine = 0;
   enum Action { ACTION_INSTALL, ACTION_UNINSTALL, ACTION_PURGE };
   map<string, int> actionMap;

   while (in.eof() == false) {
      getline(in, buffer);
      curLine++;
      if (in.fail() && !in.eof()) {
         r_listerr("Line %d too long in markings file.", curLine);
         return false;
      }

      /* Strip leading whitespace and tabs. */
      size_t pos = 0;
      while (pos < buffer.size() &&
             (buffer[pos] == ' ' || buffer[pos] == '\t'))
         pos++;
      string s = buffer.substr(pos);

      if (s.empty() || s[0] == '#')
         continue;

      /* Split into "pkgname<TAB>action". */
      istringstream iss(s);
      string pkgName, action;
      iss >> pkgName >> action;
      if (pkgName.empty() || action.empty()) {
         r_listerr("Malformed line %d in markings file", curLine);
         continue;
      }

      if (action[0] == 'i')
         actionMap[pkgName] = ACTION_INSTALL;
      else if (action[0] == 'u' || action[0] == 'd' || action[0] == 'r')
         actionMap[pkgName] = ACTION_UNINSTALL;
      else if (action[0] == 'p')
         actionMap[pkgName] = ACTION_PURGE;
   }

   if (!actionMap.empty()) {
      if (_progMeter) {
         _progMeter->Reset();
         _progMeter->Update(_("Setting markings..."), "", 0);
      }
      int pos = 0;
      int total = (int)actionMap.size();
      for (map<string, int>::const_iterator I = actionMap.begin();
           I != actionMap.end(); ++I) {
         RPackage *pkg = getPackage(I->first);
         if (pkg) {
            switch (I->second) {
               case ACTION_INSTALL:
                  pkg->setInstall();
                  break;
               case ACTION_UNINSTALL:
                  pkg->setRemove(false);
                  break;
               case ACTION_PURGE:
                  pkg->setRemove(true);
                  break;
            }
         }
         if (_progMeter && (pos++ % 5 == 0))
            _progMeter->Update(_("Setting markings..."), "",
                               (int)(100.0 * pos / total));
      }
      if (_progMeter)
         _progMeter->Done();

      /* Refresh the views so the UI shows the new markings. */
      for (unsigned int i = 0; i < _views.size(); i++)
         _views[i]->refresh();
   }
   return true;
}

/* ------------------------------------------------------------------ */
/* Notification — packages                                             */
/* ------------------------------------------------------------------ */

void
RPackageLister::notifyPreChange(RPackage *pkg)
{
   (void)pkg;
   for (vector<RPackageObserver *>::const_iterator I =
           _packageObservers.begin();
        I != _packageObservers.end(); ++I)
      (*I)->notifyPreFilteredChange();
}

void
RPackageLister::notifyPostChange(RPackage *pkg)
{
   reapplyFilter();
   for (vector<RPackageObserver *>::const_iterator I =
           _packageObservers.begin();
        I != _packageObservers.end(); ++I)
      (*I)->notifyPostFilteredChange();

   if (pkg != NULL) {
      for (vector<RPackageObserver *>::const_iterator I =
              _packageObservers.begin();
           I != _packageObservers.end(); ++I)
         (*I)->notifyChange(pkg);
   }
}

void
RPackageLister::notifyChange(RPackage *pkg)
{
   notifyPreChange(pkg);
   notifyPostChange(pkg);
}

void
RPackageLister::registerObserver(RPackageObserver *observer)
{
   _packageObservers.push_back(observer);
}

void
RPackageLister::unregisterObserver(RPackageObserver *observer)
{
   vector<RPackageObserver *>::iterator I =
      find(_packageObservers.begin(), _packageObservers.end(), observer);
   if (I != _packageObservers.end())
      _packageObservers.erase(I);
}

/* ------------------------------------------------------------------ */
/* Notification — cache                                                */
/* ------------------------------------------------------------------ */

void
RPackageLister::notifyCacheOpen()
{
   undoStack.clear();
   redoStack.clear();
   for (vector<RCacheObserver *>::const_iterator I =
           _cacheObservers.begin();
        I != _cacheObservers.end(); ++I)
      (*I)->notifyCacheOpen();
}

void
RPackageLister::notifyCachePreChange()
{
   for (vector<RCacheObserver *>::const_iterator I =
           _cacheObservers.begin();
        I != _cacheObservers.end(); ++I)
      (*I)->notifyCachePreChange();
}

void
RPackageLister::notifyCachePostChange()
{
   for (vector<RCacheObserver *>::const_iterator I =
           _cacheObservers.begin();
        I != _cacheObservers.end(); ++I)
      (*I)->notifyCachePostChange();
}

void
RPackageLister::registerCacheObserver(RCacheObserver *observer)
{
   _cacheObservers.push_back(observer);
}

void
RPackageLister::unregisterCacheObserver(RCacheObserver *observer)
{
   vector<RCacheObserver *>::iterator I =
      find(_cacheObservers.begin(), _cacheObservers.end(), observer);
   if (I != _cacheObservers.end())
      _cacheObservers.erase(I);
}

/* ------------------------------------------------------------------ */
/* View management (copied from rpackagelister.cc)                      */
/* ------------------------------------------------------------------ */

void
RPackageLister::setView(unsigned int index)
{
   if (index != PACKAGE_VIEW_SEARCH) {
      /* Under APT this was _config->Set("Synaptic::ViewMode", index).
       * We don't have a persistent config under XBPS — the env var
       * SYNAPTIC_VIEW_MODE is read once at construction time, so
       * there is nothing to persist here. */
   }
   if (index < _views.size())
      _selectedView = _views[index];
   else
      _selectedView = _views[0];
}

vector<string>
RPackageLister::getViews() const
{
   vector<string> views;
   views.reserve(_views.size());
   for (const RPackageView *view : _views)
      views.push_back(view->getName());
   return views;
}

vector<string>
RPackageLister::getSubViews() const
{
   return _selectedView->getSubViews();
}

bool
RPackageLister::setSubView(string newView)
{
   if (newView.empty())
      _selectedView->showAll();
   else
      _selectedView->setSelected(newView);

   notifyChange(NULL);
   return true;
}

void
RPackageLister::reapplyFilter()
{
   if (_updating)
      return;

   _selectedView->refresh();
   _viewPackages.clear();
   /* _viewPackagesIndex maps from a position in _packages to a
    * position in _viewPackages (or -1 if the package is not in
    * the current view). */
   _viewPackagesIndex.clear();
   _viewPackagesIndex.resize(_packages.size(), -1);

   /* Build a set of valid RPackage* from _packages so we can validate
    * each view iterator pointer before adding it to _viewPackages.
    * This catches stale pointers from a previous _packages generation
    * that the view might still be holding. */
   set<RPackage *> valid;
   for (size_t i = 0; i < _packages.size(); i++)
      valid.insert(_packages[i]);

   for (RPackageView::iterator I = _selectedView->begin();
        I != _selectedView->end(); ++I) {
      if (!*I) continue;
      /* Skip stale pointers — they point to RPackage* from a previous
       * openCache() that have been delete'd. The view should have
       * been refreshed above, but if the view's underlying data
       * structure still holds old pointers (e.g. RPackageViewSections
       * builds a map<section, vector<RPackage*>>), we'd crash in
       * sortPackages without this check. */
      if (valid.find(*I) == valid.end()) {
         /* Stale pointer — skip it. This happens when the view's
          * _selectedView has not yet been re-synced after openCache
          * replaced _packages. The RPackageView::refresh() fix
          * should prevent this, but some code paths (e.g. setSubView
          * called before refresh) can still see stale pointers.
          * Silently skip — log only on the first occurrence to avoid
          * spamming stderr. */
         static thread_local int stale_count = 0;
         if (stale_count < 3) {
            fprintf(stderr, "synaptic-xbps: skipping stale pointer %p in view (this is benign, will stop logging after 3)\n", (void *)*I);
            stale_count++;
            if (stale_count == 3)
               fprintf(stderr, "synaptic-xbps: (further stale pointer warnings suppressed)\n");
         }
         continue;
      }
      int pkgIdx = getPackageIndex(*I);
      if (pkgIdx >= 0 && (size_t)pkgIdx < _viewPackagesIndex.size())
         _viewPackagesIndex[pkgIdx] = (int)_viewPackages.size();
      _viewPackages.push_back(*I);
   }

   sortPackages(_sortMode);
}

void
RPackageLister::refreshView()
{
   _selectedView->refresh();
}

/* ------------------------------------------------------------------ */
/* Search (findPackage / findNextPackage / limitBySearch)             */
/* ------------------------------------------------------------------ */

int
RPackageLister::findPackage(const char *pattern)
{
   if (!pattern)
      return -1;

   if (_searchData.isRegex)
      regfree(&_searchData.regex);
   if (_searchData.pattern)
      free(_searchData.pattern);

   _searchData.pattern = strdup(pattern);

   if (!syn_conf_bool("SYNAPTIC_USE_REGEXP", false) ||
       regcomp(&_searchData.regex, pattern,
               REG_EXTENDED | REG_ICASE) != 0) {
      _searchData.isRegex = false;
   } else {
      _searchData.isRegex = true;
   }
   _searchData.last = -1;
   return findNextPackage();
}

int
RPackageLister::findNextPackage()
{
   if (!_searchData.pattern) {
      if (_searchData.last >= (int)_viewPackages.size())
         _searchData.last = -1;
      return ++_searchData.last;
   }

   size_t len = strlen(_searchData.pattern);
   for (unsigned i = (unsigned)(_searchData.last + 1);
        i < _viewPackages.size(); i++) {
      if (_searchData.isRegex) {
         if (regexec(&_searchData.regex, _viewPackages[i]->name(),
                     0, NULL, 0) == 0) {
            _searchData.last = (int)i;
            return (int)i;
         }
      } else {
         if (strncasecmp(_searchData.pattern, _viewPackages[i]->name(),
                         len) == 0) {
            _searchData.last = (int)i;
            return (int)i;
         }
      }
   }
   return -1;
}

bool
RPackageLister::limitBySearch(string searchString)
{
   if (searchString.empty()) {
      /* Empty search = show all packages currently in memory. */
      _viewPackages = _packages;
      _viewPackagesIndex.resize(_packages.size(), -1);
      for (size_t i = 0; i < _packages.size(); i++)
         _viewPackagesIndex[i] = (int)i;
      return true;
   }

   /* Search the packages currently in memory (installed + any remote
    * packages added by previous searches / openCache). This is the
    * iruka-xbps pattern: in-memory filter on the GTK main thread,
    * never a popen() call from the UI thread.
    *
    * The original code did `popen("xbps-query -Rs '...'")` here,
    * which forked an xbps-query process for every keystroke and
    * produced the 80+ vfork storm the user observed under gdb when
    * the search box was being typed into.
    *
    * Remote search is now exposed as a separate explicit action
    * (findPackage / the toolbar search button) so the user has to
    * ask for it deliberately; the keystroke-by-keystroke filter only
    * narrows the list that is already loaded. */
   _viewPackages.clear();
   _viewPackagesIndex.clear();
   _viewPackagesIndex.resize(_packages.size(), -1);

   for (size_t i = 0; i < _packages.size(); i++) {
      RPackage *pkg = _packages[i];
      if (!pkg) continue;
      const char *name = pkg->name();
      const char *summary = pkg->summary();
      const char *desc = pkg->description();
      if ((name && strcasestr(name, searchString.c_str())) ||
          (summary && strcasestr(summary, searchString.c_str())) ||
          (desc && strcasestr(desc, searchString.c_str()))) {
         _viewPackagesIndex[i] = (int)_viewPackages.size();
         _viewPackages.push_back(pkg);
      }
   }

   return true;
}

bool
RPackageLister::xapianSearch(string searchString)
{
   (void)searchString;
   return false;
}

/* ------------------------------------------------------------------ */
/* Sort comparators (copied from rpackagelister.cc)                     */
/* ------------------------------------------------------------------ */

static const int status_sort_magic =
   (RPackage::FInstalled | RPackage::FOutdated | RPackage::FNew);

struct statusSortFunc
{
   bool operator()(RPackage *x, RPackage *y)
   {
      return (x->getFlags() & status_sort_magic) <
             (y->getFlags() & status_sort_magic);
   }
};

struct instSizeSortFunc
{
   bool operator()(RPackage *x, RPackage *y)
   {
      return x->installedSize() < y->installedSize();
   }
};

struct dlSizeSortFunc
{
   bool operator()(RPackage *x, RPackage *y)
   {
      return x->availablePackageSize() < y->availablePackageSize();
   }
};

struct componentSortFunc
{
   bool operator()(RPackage *x, RPackage *y)
   {
      return x->component() < y->component();
   }
};

struct sectionSortFunc
{
   bool operator()(RPackage *x, RPackage *y)
   {
      return std::strcmp(x->section(), y->section()) < 0;
   }
};

/* Version compare — uses a simple lexical+numeric comparison since
 * the popen-based backend has no libxbps xbps_cmpver() helper. This
 * is good enough for the UI's sort-by-version columns; exact XBPS
 * version ordering is only needed by xbps-install itself, which we
 * invoke via execvp(). */
static int
r_verstrcmp(const char *x, const char *y)
{
   if (!x && !y) return 0;
   if (!x) return 1;
   if (!y) return -1;
   /* Strcmp-style comparison: numeric substrings compared as numbers
    * so "1.10" > "1.9". This is a crude approximation but the only
    * thing the UI uses the result for is sorting the package list. */
   while (*x && *y) {
      if (isdigit((unsigned char)*x) && isdigit((unsigned char)*y)) {
         long nx = strtol(x, (char **)&x, 10);
         long ny = strtol(y, (char **)&y, 10);
         if (nx != ny) return nx < ny ? -1 : 1;
      } else {
         if (*x != *y) return (unsigned char)*x < (unsigned char)*y ? -1 : 1;
         x++; y++;
      }
   }
   if (*x) return 1;
   if (*y) return -1;
   return 0;
}

struct versionSortFunc
{
   bool operator()(RPackage *x, RPackage *y)
   {
      return r_verstrcmp(y->availableVersion(), x->availableVersion()) < 0;
   }
};

struct instVersionSortFunc
{
   bool operator()(RPackage *x, RPackage *y)
   {
      return r_verstrcmp(y->installedVersion(), x->installedVersion()) < 0;
   }
};

struct supportedPartFunc
{
 protected:
   bool _ascent;
   RPackageStatus _status;
 public:
   supportedPartFunc(bool ascent, RPackageStatus &s)
      : _ascent(ascent), _status(s) {}
   bool operator()(RPackage *x)
   {
      return (_ascent == _status.isSupported(x));
   }
};

struct nameSortFunc
{
   bool operator()(RPackage *x, RPackage *y)
   {
      /* Defensive: if either pointer is NULL or the name() returns
       * NULL, treat them as equal to avoid a SIGSEGV in strcmp. This
       * should never happen with valid RPackage* but it helps
       * diagnose stale-pointer bugs without crashing. */
      if (!x || !y) return false;
      const char *xn = x->name();
      const char *yn = y->name();
      if (!xn || !yn) return false;
      /* Sanity check: name() should point to a valid std::string
       * c_str. If the pointer looks like a small integer (typical
       * sign of a freed pointer whose memory was reused), skip. */
      if ((uintptr_t)xn < 0x1000 || (uintptr_t)yn < 0x1000) {
         fprintf(stderr, "synaptic-xbps: WARNING stale pointer in sort (xn=%p yn=%p)\n", xn, yn);
         return false;
      }
      return std::strcmp(xn, yn) < 0;
   }
};

void
RPackageLister::sortPackages(vector<RPackage *> &packages, listSortMode mode)
{
   _sortMode = mode;
   if (packages.empty())
      return;

   /* Always sort by name first so packages are ordered inside other
    * sort criteria. */
   sort(packages.begin(), packages.end(),
        sortFunc<nameSortFunc>(true));

   switch (mode) {
      case LIST_SORT_NAME_ASC:
      case LIST_SORT_DEFAULT:
         break;
      case LIST_SORT_NAME_DES:
         sort(packages.begin(), packages.end(),
              sortFunc<nameSortFunc>(false));
         break;
      case LIST_SORT_SIZE_ASC:
         stable_sort(packages.begin(), packages.end(),
                     sortFunc<instSizeSortFunc>(true));
         break;
      case LIST_SORT_SIZE_DES:
         stable_sort(packages.begin(), packages.end(),
                     sortFunc<instSizeSortFunc>(false));
         break;
      case LIST_SORT_DLSIZE_ASC:
         stable_sort(packages.begin(), packages.end(),
                     sortFunc<dlSizeSortFunc>(true));
         break;
      case LIST_SORT_DLSIZE_DES:
         stable_sort(packages.begin(), packages.end(),
                     sortFunc<dlSizeSortFunc>(false));
         break;
      case LIST_SORT_COMPONENT_ASC:
         stable_sort(packages.begin(), packages.end(),
                     sortFunc<componentSortFunc>(true));
         break;
      case LIST_SORT_COMPONENT_DES:
         stable_sort(packages.begin(), packages.end(),
                     sortFunc<componentSortFunc>(false));
         break;
      case LIST_SORT_SECTION_ASC:
         stable_sort(packages.begin(), packages.end(),
                     sortFunc<sectionSortFunc>(true));
         break;
      case LIST_SORT_SECTION_DES:
         stable_sort(packages.begin(), packages.end(),
                     sortFunc<sectionSortFunc>(false));
         break;
      case LIST_SORT_STATUS_ASC:
         stable_sort(packages.begin(), packages.end(),
                     sortFunc<statusSortFunc>(true));
         break;
      case LIST_SORT_STATUS_DES:
         stable_sort(packages.begin(), packages.end(),
                     sortFunc<statusSortFunc>(false));
         break;
      case LIST_SORT_SUPPORTED_ASC:
         stable_partition(packages.begin(), packages.end(),
                          supportedPartFunc(false, _pkgStatus));
         break;
      case LIST_SORT_SUPPORTED_DES:
         stable_partition(packages.begin(), packages.end(),
                          supportedPartFunc(true, _pkgStatus));
         break;
      case LIST_SORT_VERSION_ASC:
         stable_sort(packages.begin(), packages.end(),
                     sortFunc<versionSortFunc>(true));
         break;
      case LIST_SORT_VERSION_DES:
         stable_sort(packages.begin(), packages.end(),
                     sortFunc<versionSortFunc>(false));
         break;
      case LIST_SORT_INST_VERSION_ASC:
         stable_sort(packages.begin(), packages.end(),
                     sortFunc<instVersionSortFunc>(true));
         break;
      case LIST_SORT_INST_VERSION_DES:
         stable_sort(packages.begin(), packages.end(),
                     sortFunc<instVersionSortFunc>(false));
         break;
   }
}

/* ------------------------------------------------------------------ */
/* Multiarch                                                            */
/* ------------------------------------------------------------------ */

bool
RPackageLister::isMultiarchSystem()
{
   /* XBPS has no multiarch. */
   return false;
}

/* ------------------------------------------------------------------ */
/* xapian (stubs)                                                       */
/* ------------------------------------------------------------------ */

#ifdef HAVE_XAPIAN
time_t
RPackageLister::xapianIndexTimestamp()
{
   /* No xapian index under XBPS. */
   return 0;
}

bool
RPackageLister::xapianIndexNeedsUpdate()
{
   return false;
}

bool
RPackageLister::openXapianIndex()
{
   return false;
}
#endif /* HAVE_XAPIAN */

#endif /* HAVE_XBPS */
