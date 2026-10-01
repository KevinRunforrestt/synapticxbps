/* rsources_xbps.cc - XBPS repository list editor implementation
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "config.h"

#ifdef HAVE_XBPS

#include "rsources_xbps.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <glob.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* rsources_xbps is now pure /etc/xbps.d file parsing — no libxbps API.
 * Keep the include guard compatible with the rest of the build but
 * do not pull in <xbps.h>. */

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static inline std::string
_trim(const std::string &s)
{
   size_t a = s.find_first_not_of(" \t\r\n");
   if (a == std::string::npos) return "";
   size_t b = s.find_last_not_of(" \t\r\n");
   return s.substr(a, b - a + 1);
}

static inline bool
_starts_with(const std::string &s, const char *p)
{
   size_t pl = strlen(p);
   return s.size() >= pl && s.compare(0, pl, p) == 0;
}

static inline bool
_write_file(const std::string &path, const std::string &content)
{
   /* If we're root, write directly. If not, use pkexec to elevate. */
   if (getuid() == 0) {
      std::ofstream out(path, std::ios::out | std::ios::trunc);
      if (!out) return false;
      out << content;
      return out.good();
   }
   /* Write content to a temp file, then pkexec cp + tee to write
    * it to the target path. This avoids shell quoting issues. */
   char tmppath[] = "/tmp/synaptic-xbps-repo-XXXXXX";
   int fd = mkstemp(tmppath);
   if (fd < 0) return false;
   FILE *tf = fdopen(fd, "w");
   if (!tf) { close(fd); unlink(tmppath); return false; }
   fwrite(content.c_str(), 1, content.size(), tf);
   fclose(tf);
   /* pkexec sh -c 'cat <tmpfile> > <target>' */
   std::string cmd = "pkexec sh -c 'cat \"";
   cmd += tmppath;
   cmd += "\" > \"";
   cmd += path;
   cmd += "\"'";
   int rv = system(cmd.c_str());
   unlink(tmppath);
   return rv == 0;
}

/* ------------------------------------------------------------------ */
/* ReadSources / parseFile                                            */
/* ------------------------------------------------------------------ */

bool
SourcesListXbps::parseFile(const std::string &path)
{
   std::ifstream in(path);
   if (!in) return false;

   std::string line;
   int lineno = 0;
   while (std::getline(in, line)) {
      lineno++;
      /* strip trailing \r if present (CRLF safety) */
      if (!line.empty() && line.back() == '\r')
         line.pop_back();

      SourceRecord r;
      r.sourceFile = path;
      r.rawLine = line;
      r.enabled = false;
      r.type = TYPE_UNKNOWN;
      r.url.clear();

      std::string trimmed = _trim(line);
      if (trimmed.empty()) {
         r.type = TYPE_EMPTY;
      } else if (trimmed[0] == '#') {
         r.type = TYPE_COMMENT;
         /* if the comment is "#repository=...", extract the URL so
          * ToggleSource can re-enable it later. */
         std::string rest = _trim(trimmed.substr(1));
         if (_starts_with(rest, "repository=")) {
            r.url = _trim(rest.substr(strlen("repository=")));
            /* leave enabled=false; the comment makes it disabled */
         }
      } else if (_starts_with(trimmed, "repository=")) {
         r.type = TYPE_XBPS;
         r.url = _trim(trimmed.substr(strlen("repository=")));
         r.enabled = true;
      } else {
         r.type = TYPE_UNKNOWN;
      }

      _records.push_back(r);
   }
   return true;
}

bool
SourcesListXbps::ReadSources()
{
   _records.clear();

   /* Read in alphabetical order; /usr/share/xbps.d first (vendor
    * defaults), then /etc/xbps.d (user overrides). Both are valid
    * locations per xbps.d(5). */
   const char *dirs[] = {"/usr/share/xbps.d", "/etc/xbps.d", NULL};
   for (int d = 0; dirs[d]; d++) {
      std::string pat = std::string(dirs[d]) + "/*.conf";
      glob_t g;
      memset(&g, 0, sizeof g);
      if (glob(pat.c_str(), 0, NULL, &g) == 0) {
         for (size_t i = 0; i < g.gl_pathc; i++)
            (void)parseFile(g.gl_pathv[i]);
         globfree(&g);
      }
   }
   return !_records.empty();
}

/* ------------------------------------------------------------------ */
/* UpdateSources                                                      */
/* ------------------------------------------------------------------ */

bool
SourcesListXbps::UpdateSources()
{
   /* Group records by sourceFile, write each group back. */
   std::sort(_records.begin(), _records.end(),
             [](const SourceRecord &a, const SourceRecord &b) {
                return a.sourceFile < b.sourceFile;
             });

   std::string current_file;
   std::string content;
   bool ok = true;
   for (size_t i = 0; i <= _records.size(); i++) {
      bool flush = (i == _records.size()) ||
                   (!current_file.empty() && _records[i].sourceFile != current_file);
      if (flush && !current_file.empty()) {
         if (!_write_file(current_file, content))
            ok = false;
         content.clear();
      }
      if (i == _records.size())
         break;

      const SourceRecord &r = _records[i];
      if (r.sourceFile != current_file)
         current_file = r.sourceFile;

      switch (r.type) {
      case TYPE_XBPS:
         if (r.enabled)
            content += "repository=" + r.url + "\n";
         else
            content += "#repository=" + r.url + "\n";
         break;
      case TYPE_COMMENT:
         /* Preserve commented-out repos and other comments verbatim. */
         content += r.rawLine + "\n";
         break;
      case TYPE_EMPTY:
         content += "\n";
         break;
      case TYPE_UNKNOWN:
         content += r.rawLine + "\n";
         break;
      }
   }
   return ok;
}

/* ------------------------------------------------------------------ */
/* AddSource / RemoveSource / ToggleSource                            */
/* ------------------------------------------------------------------ */

bool
SourcesListXbps::AddSource(const std::string &url)
{
   std::string trimmed = _trim(url);
   if (trimmed.empty())
      return false;

   /* Ensure /etc/xbps.d exists. */
   (void)mkdir("/etc/xbps.d", 0755);

   /* Append to /etc/xbps.d/99-synaptic.conf. Skip if already present
    * (in any state — enabled or commented-out). */
   const std::string path = "/etc/xbps.d/99-synaptic.conf";
   std::ifstream in(path);
   std::string existing;
   if (in) {
      std::string line;
      while (std::getline(in, line)) {
         existing += line + "\n";
         std::string t = _trim(line);
         if (!t.empty() && t[0] != '#') {
            if (_starts_with(t, "repository=")) {
               std::string u = _trim(t.substr(strlen("repository=")));
               if (u == trimmed) {
                  in.close();
                  return true; /* already present */
               }
            }
         }
      }
      in.close();
   }

   /* If root, append directly. If not, use pkexec. */
   if (getuid() == 0) {
      std::ofstream out(path, std::ios::app);
      if (!out) return false;
      out << "repository=" << trimmed << "\n";
      out.close();
   } else {
      /* pkexec sh -c 'echo "repository=<url>" >> <path>' */
      std::string cmd = "pkexec sh -c 'echo \"repository=";
      cmd += trimmed;
      cmd += "\" >> \"";
      cmd += path;
      cmd += "\"'";
      int rv = system(cmd.c_str());
      if (rv != 0) return false;
   }

   return ReadSources();
}

bool
SourcesListXbps::RemoveSource(const std::string &url)
{
   std::string trimmed = _trim(url);
   if (trimmed.empty())
      return false;

   std::vector<SourceRecord> kept;
   kept.reserve(_records.size());
   for (const auto &r : _records) {
      bool match = (r.type == TYPE_XBPS && r.enabled && r.url == trimmed) ||
                   (r.type == TYPE_COMMENT && r.url == trimmed);
      if (!match)
         kept.push_back(r);
   }
   _records.swap(kept);

   return UpdateSources();
}

bool
SourcesListXbps::ToggleSource(const std::string &url, bool enabled)
{
   std::string trimmed = _trim(url);
   if (trimmed.empty())
      return false;

   for (auto &r : _records) {
      if (r.type == TYPE_XBPS && r.url == trimmed) {
         r.enabled = enabled;
      } else if (r.type == TYPE_COMMENT && r.url == trimmed) {
         /* Promote a commented-out repo to an enabled XBPS record. */
         if (enabled) {
            r.type = TYPE_XBPS;
            r.enabled = true;
         }
      }
   }
   return UpdateSources();
}

/* ------------------------------------------------------------------ */
/* getEnabledUrls                                                     */
/* ------------------------------------------------------------------ */

std::vector<std::string>
SourcesListXbps::getEnabledUrls() const
{
   std::vector<std::string> v;
   for (const auto &r : _records) {
      if (r.type == TYPE_XBPS && r.enabled)
         v.push_back(r.url);
   }
   /* deduplicate */
   std::sort(v.begin(), v.end());
   v.erase(std::unique(v.begin(), v.end()), v.end());
   return v;
}

#endif /* HAVE_XBPS */
