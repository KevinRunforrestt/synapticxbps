/* apt-pkg-stub/apt-pkg/fileutl.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg FileFd class and the fileutl free
 * functions. The Synaptic UI uses FileFd, GetLock, FileExists, flNotFile
 * and SafeGetCwd in several places. Under HAVE_XBPS these are no-ops
 * or return sensible defaults.
 */
#pragma once

#include <apt-pkg/macros.h>

#include <cstdarg>
#include <string>

/* Forward-declaration for the RProgress base used by some signatures
 * (FileFd itself doesn't depend on it, but `Open(Progress*)` does). */
class RProgress;

class APT_PUBLIC FileFd
{
 public:
   enum OpenMode
   {
      ReadOnly,
      WriteOnly,
      ReadWrite,
      Empty,
      WriteEmpty,
      ReadOnlyGzip,
      WriteAtomic
   };
   enum CompressMode
   {
      AutoExtension,
      Auto,
      None,
      Gzip,
      Bzip2,
      Lzma,
      Xz
   };

   FileFd() = default;
   FileFd(const std::string &/*file*/, OpenMode /*mode*/, CompressMode /*comp*/ = AutoExtension) {}
   FileFd(const char * /*file*/, OpenMode /*mode*/, CompressMode /*comp*/ = AutoExtension) {}
   ~FileFd() = default;

   bool Open(const std::string &/*file*/, OpenMode /*mode*/, CompressMode /*comp*/ = AutoExtension) { return false; }
   bool Open(const char * /*file*/, OpenMode /*mode*/, CompressMode /*comp*/ = AutoExtension) { return false; }
   bool Close() { return true; }
   bool IsOpen() const { return false; }
   int Fd() const { return -1; }

   /* I/O stubs — reads return 0 / EOF, writes pretend success. */
   bool Read(void * /*buf*/, unsigned long long /*len*/, unsigned long long * /*actual*/ = nullptr) { return false; }
   bool Write(const void * /*buf*/, unsigned long long /*len*/, unsigned long long * /*actual*/ = nullptr) { return true; }
   bool ReadLine(std::string &/*line*/) { return false; }

   bool Seek(unsigned long long /*to*/) { return false; }
   unsigned long long Tell() { return 0; }
   unsigned long long Size() { return 0; }

   bool Failed() const { return false; }
   bool Eof() const { return true; }
   std::string Name() const { return ""; }
};

/* Free functions used by the Synaptic UI. */

/* Tries to acquire an exclusive lock on the given file (like flock(2)).
 * The stub returns 0 (success) so gsynaptic.cc's check_and_aquire_lock()
 * can complete without an APT config. The real lock under HAVE_XBPS is
 * taken by xbps_pkgdb_lock() inside RPackageCacheXbps::open(). */
APT_PUBLIC int GetLock(std::string /*file*/, bool /*errors*/ = true);
APT_PUBLIC int GetLock(const char * /*file*/, bool /*errors*/ = true);

/* True if the given path exists and is a regular file. The stub returns
 * false unconditionally; under HAVE_XBPS Synaptic uses its own checks
 * (e.g. FileExists from rgutils.cc) where the result actually matters. */
APT_PUBLIC bool FileExists(std::string /*file*/);
APT_PUBLIC bool FileExists(const char * /*file*/);

/* Strip the trailing filename component from a path, leaving the
 * directory. The stub returns the input verbatim with any trailing
 * '/' preserved — sufficient for gsynaptic.cc's use as a "dirname"
 * of "/var/db/xbps/lock" to obtain "/var/db/xbps/". */
APT_PUBLIC std::string flNotFile(std::string file);
APT_PUBLIC std::string flNotFile(const char *file);

/* Strip the trailing directory part, leaving the filename. */
APT_PUBLIC std::string flNotDir(std::string file);
APT_PUBLIC std::string flNotDir(const char *file);

/* Resolve the cwd safely. */
APT_PUBLIC std::string SafeGetCwd();

/* flCombine / flExtension are tiny utility functions used by rconfiguration. */
APT_PUBLIC std::string flCombine(std::string /*a*/, std::string /*b*/);
APT_PUBLIC std::string flExtension(std::string /*file*/);
