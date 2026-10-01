/* rsources_xbps.h - XBPS repository list editor
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * Synaptic uses this class to read/write /etc/xbps.d/*.conf and
 * /usr/share/xbps.d/*.conf files. The format is simple key=value lines
 * (see xbps.d(5)). The only key Synaptic cares about is
 * "repository=<url>".
 */

#pragma once

#include "config.h"

#ifdef HAVE_XBPS

#include <iostream>
#include <list>
#include <string>
#include <vector>

class SourcesListXbps
{
 public:
   enum SourceType {
      TYPE_XBPS = 1,    /* repository= entry */
      TYPE_COMMENT,     /* # ... comment (incl. commented-out repos) */
      TYPE_EMPTY,       /* blank line */
      TYPE_UNKNOWN      /* any other key=value line we don't recognize */
   };

   struct SourceRecord
   {
      SourceType type;
      std::string url;          /* repository URL (only for TYPE_XBPS) */
      std::string rawLine;       /* the original line, verbatim */
      std::string sourceFile;   /* which /etc/xbps.d/*.conf file this came from */
      bool enabled;
   };

   SourcesListXbps() = default;
   ~SourcesListXbps() = default;

   /* Parse /etc/xbps.d/*.conf and /usr/share/xbps.d/*.conf. Returns
    * true if at least one file was successfully parsed. */
   bool ReadSources();

   /* Write all records back to their original files. Returns true
    * if all writes succeeded. */
   bool UpdateSources();

   /* Append `repository=<url>` to /etc/xbps.d/99-synaptic.conf,
    * creating the file if it doesn't exist. Returns true on success. */
   bool AddSource(const std::string &url);

   /* Remove the matching record(s). Returns true on success. */
   bool RemoveSource(const std::string &url);

   /* Comment out / uncomment the matching record. */
   bool ToggleSource(const std::string &url, bool enabled);

   /* Returns the records parsed on the last ReadSources() call. */
   const std::vector<SourceRecord> &getRecords() const { return _records; }

   /* Returns only the enabled repository URLs. */
   std::vector<std::string> getEnabledUrls() const;

 private:
   std::vector<SourceRecord> _records;

   bool parseFile(const std::string &path);
};

#endif /* HAVE_XBPS */
