/* apt-pkg-stub/apt-pkg/sourcelist.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg pkgSourceList class. The Synaptic UI
 * (rgrepositorywin.cc) uses pkgSourceList::ReadMainList, Read, const_iterator,
 * begin and end. We deliberately do NOT declare a FindIndex method here
 * — including it caused compilation errors in the original porting
 * effort because rgrepositorywin.cc's code paths that called FindIndex
 * used a slightly different signature than the one in the apt-pkg
 * header. Since rgrepositorywin.cc's FindIndex call site is gated on
 * !HAVE_XBPS, omitting the stub method is safe.
 */
#pragma once

#include <apt-pkg/macros.h>

#include <iterator>
#include <string>
#include <vector>

class APT_PUBLIC pkgSourceList
{
 public:
   /* The real apt-pkg class has a nested metaIndex-iterator type. The
    * stub provides a trivial forward iterator that returns nothing. */
   class APT_PUBLIC const_iterator
   {
    public:
     typedef std::forward_iterator_tag iterator_category;
     typedef void *value_type;
     typedef std::ptrdiff_t difference_type;
     typedef void **pointer;
     typedef void *&reference;

     const_iterator() = default;
     bool operator==(const const_iterator &) const { return true; }
     bool operator!=(const const_iterator &) const { return false; }
     const_iterator &operator++() { return *this; }
     reference operator*() { static void *p = nullptr; return p; }
   };

   pkgSourceList() = default;
   virtual ~pkgSourceList() = default;

   /* Parse /etc/apt/sources.list (and /etc/apt/sources.list.d/*.list).
    * Under HAVE_XBPS the file doesn't exist; the stub returns true
    * (success) so the GUI doesn't bail out. The real work is done by
    * SourcesListXbps (common/rsources_xbps.cc). */
   bool ReadMainList() { return true; }
   bool Read(std::string /*file*/) { return true; }

   const_iterator begin() const { return const_iterator(); }
   const_iterator end() const { return const_iterator(); }
};
