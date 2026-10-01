/* apt-pkg-stub/apt-pkg/pkgcache.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg pkgCache class and its nested
 * iterators / dep / flag / state types. The Synaptic UI refers to these
 * types in a few places (rpackagelister.h forward-declares
 * pkgCache::PkgIterator, rpackagefilter.h uses pkgCache::Dep::DepType).
 *
 * IMPORTANT: State_SaveRestore (a class) and State (a struct with a
 * PkgState enum) are DIFFERENT types. The real apt-pkg header defines
 * both; the stub reproduces that exactly so any code that uses either
 * resolves correctly.
 *
 * The PkgState enum values match the real apt-pkg numeric values so the
 * GUI code that compares against them (NotInstalled, Installed, ...)
 * keeps working.
 */
#pragma once

#include <apt-pkg/macros.h>

#include <cstddef>
#include <string>

class APT_PUBLIC pkgCache
{
 public:
   /* ---- Dependency type enum. The numeric values match the real
    * apt-pkg enum so RDepType casts are well-defined. The GUI's
    * rpackagefilter.h uses pkgCache::Dep::DepType. */
   class APT_PUBLIC Dep
   {
    public:
     enum DepType
     {
        Depends = 1,
        PreDepends = 2,
        Suggests = 3,
        Recommends = 4,
        Conflicts = 5,
        Replaces = 6,
        Obsoletes = 7,
        Breaks = 8,
        Enhances = 9
     };
     enum DepCompareOp
     {
        NoOp = 0,
        LessThan = 0x100,
        GreaterThan = 0x200,
        LessEq = 0x400,
        GreaterEq = 0x800,
        Equals = 0x1000,
        NotEquals = 0x2000
     };
   };

   /* ---- Package flag bits. */
   class APT_PUBLIC Flag
   {
    public:
     enum Flags
     {
        Essential = 1,
        Important = 2,
        Auto = 4,
        AutoInst = 8
     };
   };

   /* ---- The package-state struct (NOT the same as State_SaveRestore!).
    *
    * The GUI reads pkgCache::State::PkgState values to compare a
    * package's installed/unpacked/broken state. */
   struct APT_PUBLIC State
   {
      enum PkgState
      {
         NotInstalled = 0,
         UnPacked = 1,
         HalfInstalled = 2,
         HalfConfigured = 3,
         Installed = 4,
         ConfigFiles = 5
      };
   };

   /* ---- State_SaveRestore is a separate class (used by pkgDepCache to
    * snapshot/restore the depcache state). The GUI refers to it as
    * pkgDepCache::State, which is a typedef for State_SaveRestore. */
   class APT_PUBLIC State_SaveRestore
   {
    public:
     State_SaveRestore() = default;
     ~State_SaveRestore() = default;
   };

   /* ---- Nested iterators. The real apt-pkg provides a rich iterator
    * API; the stub provides only enough of the API for the GUI to name
    * the types (it never actually dereferences them under HAVE_XBPS). */
   class APT_PUBLIC PkgIterator
   {
    public:
     PkgIterator() = default;
     PkgIterator(const PkgIterator &) = default;
     PkgIterator &operator=(const PkgIterator &) = default;
     bool operator==(const PkgIterator &other) const { return this == &other; }
     bool operator!=(const PkgIterator &other) const { return this != &other; }
     bool end() const { return true; }
     PkgIterator &operator++() { return *this; }
     const char *Name() const { return ""; }
     unsigned int ID() const { return 0; }
   };

   class APT_PUBLIC VerIterator
   {
    public:
     VerIterator() = default;
     bool end() const { return true; }
     VerIterator &operator++() { return *this; }
     const char *VerStr() const { return ""; }
     const char *Arch() const { return ""; }
   };

   class APT_PUBLIC VerFileIterator
   {
    public:
     VerFileIterator() = default;
     bool end() const { return true; }
     VerFileIterator &operator++() { return *this; }
   };

   class APT_PUBLIC PkgFileIterator
   {
    public:
     PkgFileIterator() = default;
     bool end() const { return true; }
     PkgFileIterator &operator++() { return *this; }
   };

   class APT_PUBLIC DepIterator
   {
    public:
     DepIterator() = default;
     bool end() const { return true; }
     DepIterator &operator++() { return *this; }
   };

   class APT_PUBLIC PrvIterator
   {
    public:
     PrvIterator() = default;
     bool end() const { return true; }
     PrvIterator &operator++() { return *this; }
   };

   class APT_PUBLIC DescIterator
   {
    public:
     DescIterator() = default;
     bool end() const { return true; }
     DescIterator &operator++() { return *this; }
   };

   /* The real pkgCache exposes Header data; the stub exposes only what
    * the GUI reads (basically nothing). */
   struct APT_PUBLIC Header
   {
      unsigned int Version = 0;
      unsigned int PackageCount = 0;
      unsigned int VersionCount = 0;
   };

   pkgCache() = default;
   ~pkgCache() = default;

   /* PkgFindByName returns end() under the stub (no real cache). */
   PkgIterator FindPkg(const std::string & /*name*/) { return PkgIterator(); }
   PkgIterator FindPkg(const char * /*name*/) { return PkgIterator(); }
};
