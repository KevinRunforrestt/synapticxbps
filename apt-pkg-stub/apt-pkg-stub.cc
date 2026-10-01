/* apt-pkg-stub/apt-pkg-stub.cc - global APT object + free-function stubs
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Provides the global APT object definitions and the inert free
 * functions declared in the apt-pkg-stub/apt-pkg/ headers. Under
 * HAVE_XBPS we don't link against libapt-pkg, so the linker needs these
 * symbols to be defined somewhere. The headers in
 * apt-pkg-stub/apt-pkg/ declare them; this file is what
 * common/meson.build appends to the libsynaptic static library.
 *
 * Globals:
 *   - _config is a Configuration instance whose methods round-trip
 *     writes/reads for Synaptic's own config keys (e.g.
 *     "Synaptic::ViewMode"). APT-specific keys (e.g. "Acquire::http::Proxy")
 *     are silently discarded by the GUI's HAVE_XBPS code paths.
 *   - _error is a GlobalError whose methods silently discard messages;
 *     the GUI surfaces its own XBPS-flavoured error text via RUserDialog.
 *   - _system is a NULL pkgSystem* because the gsynaptic.cc Lock path
 *     and the rgmainwindow.cc CreatePM path are both gated on
 *     !HAVE_XBPS. The NULL pointer is never dereferenced at runtime.
 *
 * Free functions:
 *   - GetLock(string, bool)  — implemented as a thin wrapper around
 *     flock(2). gsynaptic.cc calls GetLock to grab /etc/synaptic/lock
 *     and /etc/synaptic/lock.non-interactive. The real apt-pkg GetLock
 *     uses fcntl(2) with F_SETLK; we use flock(2) which is functionally
 *     equivalent on Linux and simpler to call.
 *   - FileExists(string)    — stat()-based existence check.
 *   - flNotFile(string)     — strip trailing filename, return directory.
 *   - flNotDir(string)      — strip trailing directory, return filename.
 *   - SafeGetCwd()          — getcwd(3) wrapper that never returns "".
 *   - flCombine(a, b)       — join two path components with a '/'.
 *   - flExtension(file)    — return the part after the last '.'.
 *   - ioprintf(fmt, ...)    — printf-style string formatter.
 *   - ioprintf(out, fmt, ...) — printf-style string formatter that also
 *     writes the result to the given ostream.
 *   - SizeToStr(double)    — bytes-to-human-readable.
 *   - stringcasecmp(a, b)  — case-insensitive compare for std::string.
 *   - ParseQuoteWord(...)  — parse a quoted word out of a string.
 *   - SubstVar(...)        — substring substitution.
 *   - pkgVersionCompare(a, b), pkgVersionCheckDep(...) — version ops.
 *   - ListUpdate(...)      — refresh package lists.
 *   - APT::Upgrade::Upgrade(...) — APT-style upgrade.
 *   - ReadPinFile(...)     — read APT pin file.
 *   - RFC1123StrToTime(...) — RFC1123 date parser.
 */
#include "apt-pkg/configuration.h"
#include "apt-pkg/error.h"
#include "apt-pkg/fileutl.h"
#include "apt-pkg/init.h"
#include "apt-pkg/macros.h"
#include "apt-pkg/pkgsystem.h"
#include "apt-pkg/strutl.h"
#include "apt-pkg/update.h"
#include "apt-pkg/upgrade.h"
#include "apt-pkg/version.h"

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include "apt-pkg/policy.h"
#include "apt-pkg/hashes.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <strings.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Globals                                                            */
/* ------------------------------------------------------------------ */

Configuration *_config = new Configuration();
GlobalError *_error = new GlobalError();
pkgSystem *_system = nullptr;

/* ------------------------------------------------------------------ */
/* fileutl free functions                                             */
/* ------------------------------------------------------------------ */

int GetLock(std::string file, bool /*errors*/)
{
   /* Open (or create) the file, then flock(LOCK_EX|LOCK_NB). On success
    * return the FD; on failure return -1. The real apt-pkg returns the
    * FD on success; the GUI only checks `< 0`. */
   int fd = open(file.c_str(), O_RDWR | O_CREAT, 0640);
   if (fd < 0)
      return -1;
   if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
      close(fd);
      return -1;
   }
   /* Intentionally leak the fd so the lock stays held for the lifetime
    * of the process — the real apt-pkg does the same. */
   return fd;
}

int GetLock(const char *file, bool errors)
{
   if (!file)
      return -1;
   return GetLock(std::string(file), errors);
}

bool FileExists(std::string file)
{
   if (file.empty())
      return false;
   struct stat st;
   return stat(file.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool FileExists(const char *file)
{
   if (!file)
      return false;
   return FileExists(std::string(file));
}

std::string flNotFile(std::string file)
{
   /* Strip the trailing filename component, leaving the directory.
    * Returns the directory with a trailing '/' if the input had one. */
   if (file.empty())
      return "";
   std::string::size_type pos = file.find_last_of('/');
   if (pos == std::string::npos)
      return "./";
   return file.substr(0, pos + 1);
}

std::string flNotFile(const char *file)
{
   if (!file)
      return "";
   return flNotFile(std::string(file));
}

std::string flNotDir(std::string file)
{
   if (file.empty())
      return "";
   std::string::size_type pos = file.find_last_of('/');
   if (pos == std::string::npos)
      return file;
   return file.substr(pos + 1);
}

std::string flNotDir(const char *file)
{
   if (!file)
      return "";
   return flNotDir(std::string(file));
}

std::string SafeGetCwd()
{
   char buf[4096];
   if (getcwd(buf, sizeof buf) == NULL)
      return "/";
   return std::string(buf);
}

std::string flCombine(std::string a, std::string b)
{
   if (a.empty())
      return b;
   if (b.empty())
      return a;
   if (b[0] == '/')
      return b;
   if (a[a.size() - 1] == '/')
      return a + b;
   return a + "/" + b;
}

std::string flExtension(std::string file)
{
   std::string::size_type pos = file.find_last_of('.');
   if (pos == std::string::npos)
      return "";
   return file.substr(pos + 1);
}

/* ------------------------------------------------------------------ */
/* strutl free functions                                              */
/* ------------------------------------------------------------------ */

std::string ioprintf(const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);
   char buf[1024];
   vsnprintf(buf, sizeof buf, fmt, ap);
   va_end(ap);
   return std::string(buf);
}

std::string ioprintf(std::ostream &out, const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);
   char buf[1024];
   vsnprintf(buf, sizeof buf, fmt, ap);
   va_end(ap);
   out << buf;
   return std::string(buf);
}

/* SizeToStr: rgutils.cc has its own implementation; do NOT define
 * it here to avoid a multiple-definition link error. */

int stringcasecmp(const std::string &a, const std::string &b)
{
   return strcasecmp(a.c_str(), b.c_str());
}

bool ParseQuoteWord(std::string &cursor, std::string &word)
{
   word.clear();
   std::string::size_type pos = cursor.find_first_not_of(" \t");
   if (pos == std::string::npos) {
      cursor.clear();
      return false;
   }
   cursor = cursor.substr(pos);
   if (cursor[0] == '"') {
      std::string::size_type end = cursor.find('"', 1);
      if (end == std::string::npos) {
         word = cursor.substr(1);
         cursor.clear();
      } else {
         word = cursor.substr(1, end - 1);
         cursor = cursor.substr(end + 1);
      }
   } else {
      std::string::size_type end = cursor.find_first_of(" \t");
      if (end == std::string::npos) {
         word = cursor;
         cursor.clear();
      } else {
         word = cursor.substr(0, end);
         cursor = cursor.substr(end);
      }
   }
   return true;
}

std::string SubstVar(std::string input, const std::string &from, const std::string &to)
{
   if (from.empty())
      return input;
   std::string result;
   std::string::size_type pos = 0, last = 0;
   while ((pos = input.find(from, last)) != std::string::npos) {
      result += input.substr(last, pos - last);
      result += to;
      last = pos + from.size();
   }
   result += input.substr(last);
   return result;
}

std::string SubstVar(std::string input, const char *from, const char *to)
{
   if (!from || !to)
      return input;
   return SubstVar(input, std::string(from), std::string(to));
}

/* ------------------------------------------------------------------ */
/* init free functions                                                */
/* ------------------------------------------------------------------ */

bool pkgInitConfig(Configuration & /*config*/)
{
   return true;
}

bool pkgInitSystem(Configuration & /*config*/, pkgSystem *& /*system*/)
{
   return true;
}

/* ------------------------------------------------------------------ */
/* version free functions                                             */
/* ------------------------------------------------------------------ */

int pkgVersionCompare(const std::string & /*a*/, const std::string & /*b*/)
{
   return 0;
}

int pkgVersionCompare(const char *a, const char *b)
{
   if (!a && !b) return 0;
   if (!a) return -1;
   if (!b) return 1;
   return strcmp(a, b);
}

bool pkgVersionCheckDep(const std::string & /*pkg_ver*/,
                        const std::string & /*dep_op*/,
                        const std::string & /*dep_ver*/)
{
   return true;
}

/* ------------------------------------------------------------------ */
/* update free function                                               */
/* ------------------------------------------------------------------ */

bool ListUpdate(pkgAcquireStatus & /*progress*/,
                pkgSourceList & /*sources*/,
                unsigned int /*pulseInterval*/)
{
   return true;
}

/* ------------------------------------------------------------------ */
/* upgrade free function                                              */
/* ------------------------------------------------------------------ */

namespace APT {
namespace Upgrade {
bool Upgrade(pkgDepCache & /*cache*/, UpgradeMode /*mode*/)
{
   return true;
}
}; // namespace Upgrade
}; // namespace APT

/* ------------------------------------------------------------------ */
/* policy free function                                               */
/* ------------------------------------------------------------------ */

bool ReadPinFile(pkgPolicy & /*policy*/, std::string /*file*/)
{
   return true;
}

/* ------------------------------------------------------------------ */
/* hashes free function                                               */
/* ------------------------------------------------------------------ */

bool RFC1123StrToTime(const char * /*str*/, time_t & /*t*/)
{
   return false;
}
