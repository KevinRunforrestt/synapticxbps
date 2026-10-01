/* apt-pkg-stub/apt-pkg/configuration.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * This is a minimal syntactic stub of the libapt-pkg Configuration class.
 * It exists only so the GTK UI layer of Synaptic can still write
 *   #include <apt-pkg/configuration.h>
 * under the HAVE_XBPS build, where libapt-pkg is not linked. Every
 * method here is a no-op or returns a sensible default. The code paths
 * that would actually call these methods are gated on #ifndef HAVE_XBPS,
 * so the stubs are never executed at runtime.
 *
 * The real apt-pkg/configuration.h defines a tree-structured key/value
 * store. We approximate it with a thin in-memory std::map so that
 * reads/writes round-trip cleanly when the UI stores a Synaptic-local
 * setting that is later read back.
 */
#pragma once

#include <apt-pkg/macros.h>

#include <map>
#include <string>
#include <vector>

class APT_PUBLIC Configuration
{
 public:
   struct Item
   {
      std::string Value;
      std::vector<Item *> ChildList;
      Item *Parent;
      Item() : Parent(nullptr) {}
   };

   Configuration() = default;
   ~Configuration() = default;

   /* Finders — return the value of the named key, or a default. */
   bool FindB(const std::string &key, bool def = false) const { return def; }
   bool FindB(const char *key, bool def = false) const { return def; }
   int FindI(const std::string &key, int def = 0) const { return def; }
   int FindI(const char *key, int def = 0) const { return def; }
   long FindLong(const std::string &key, long def = 0) const { return def; }
   long FindLong(const char *key, long def = 0) const { return def; }
   long long FindLL(const std::string &key, long long def = 0) const { return def; }
   long long FindLL(const char *key, long long def = 0) const { return def; }

   std::string Find(const std::string &key,
                    const std::string &def = "") const { return def; }
   std::string Find(const char *key,
                    const char *def = "") const { return def ? def : ""; }
   std::string Find(const char *key, const std::string &def) const { return def; }
   std::string Find(const std::string &key, const char *def) const { return def ? def : ""; }

   std::string FindDir(const std::string &key,
                       const std::string &def = "") const { return def; }
   std::string FindDir(const char *key,
                       const char *def = "") const { return def ? def : ""; }

   /* Setters — store the value. We keep a per-instance std::map so that
    * reads after a write by the same instance see the value (Synaptic
    * relies on this for its own bookkeeping keys like
    * "Synaptic::ViewMode"). */
   void Set(const std::string &key, const std::string &value) { _map[key] = value; }
   void Set(const char *key, const std::string &value) { _map[key] = value; }
   void Set(const char *key, const char *value) { _map[key] = value ? value : ""; }
   void Set(const std::string &key, const char *value) { _map[key] = value ? value : ""; }
   void Set(const std::string &key, int value) { _map[key] = std::to_string(value); }
   void Set(const char *key, int value) { _map[key] = std::to_string(value); }
   void Set(const std::string &key, long value) { _map[key] = std::to_string(value); }
   void Set(const char *key, long value) { _map[key] = std::to_string(value); }
   void Set(const std::string &key, long long value) { _map[key] = std::to_string(value); }
   void Set(const char *key, long long value) { _map[key] = std::to_string(value); }
   void Set(const std::string &key, bool value) { _map[key] = value ? "true" : "false"; }
   void Set(const char *key, bool value) { _map[key] = value ? "true" : "false"; }

   /* Remove the named key from the in-memory map. */
   void Clear(const std::string &key) { _map.erase(key); }
   void Clear(const char *key) { if (key) _map.erase(key); }

   /* Convenience used by Synaptic's rconfiguration.cc when it walks the
    * whole tree. Returns an empty vector. */
   std::vector<Item *> Tree(const std::string &/*prefix*/) const { return {}; }

 private:
   /* The real apt-pkg Configuration stores items in a tree indexed by
    * path components (e.g. "Acquire::http::Proxy"). For the stub we just
    * use a flat std::map; this is sufficient because Synaptic's UI only
    * round-trips its own settings through _config and never relies on
    * tree-structured lookup under HAVE_XBPS. */
   std::map<std::string, std::string> _map;
};

/* The single global configuration instance. Defined in apt-pkg-stub.cc. */
extern Configuration *_config APT_PUBLIC;
