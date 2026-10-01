/* apt-pkg-stub/apt-pkg/strutl.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg string utilities. The Synaptic UI uses
 * ioprintf, SizeToStr, stringcasecmp, ParseQuoteWord and SubstVar in
 * several places. Under HAVE_XBPS these are no-ops or trivial inlines.
 */
#pragma once

#include <apt-pkg/macros.h>

#include <cstdarg>
#include <ostream>
#include <sstream>
#include <string>
#include <strings.h>

APT_PUBLIC std::string ioprintf(const char *fmt, ...) APT_FORMAT_PRINTF(1, 2);
APT_PUBLIC std::string ioprintf(std::ostream &out, const char *fmt, ...) APT_FORMAT_PRINTF(2, 3);

/* Convert a byte size to a human-readable string (1.5 MB, etc.). */
APT_PUBLIC std::string SizeToStr(double /*bytes*/);

/* strcasecmp wrapper that takes std::string. */
APT_PUBLIC int stringcasecmp(const std::string &a, const std::string &b);

/* Parse a quoted word out of a string, advancing the cursor. */
APT_PUBLIC bool ParseQuoteWord(std::string &/*cursor*/, std::string &/*word*/);

/* Substitute all occurrences of "from" in "input" with "to". */
APT_PUBLIC std::string SubstVar(std::string input,
                               const std::string &from,
                               const std::string &to);
APT_PUBLIC std::string SubstVar(std::string input,
                               const char *from,
                               const char *to);
