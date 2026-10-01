/* apt-pkg-stub/apt-pkg/error.h - inert stub
 *
 * Copyright (c) 2025 Synaptic-XBPS port for Void Linux
 *
 * Minimal stub of the libapt-pkg GlobalError class. The Synaptic UI
 * calls _error->Error(...), _error->Warning(...), _error->PopMessage(...),
 * _error->Discard(), _error->empty(), _error->size() and
 * _error->DumpErrors() in many places. Under HAVE_XBPS these are
 * no-ops so the GUI compiles and links cleanly.
 *
 * The InsertErr / Push variants use the same va_list style as the
 * original. We silently discard the messages — Synaptic's UI shows its
 * own XBPS-flavoured error messages via RUserDialog and rgrepositorywin
 * already.
 */
#pragma once

#include <apt-pkg/macros.h>

#include <cstdarg>
#include <string>

class APT_PUBLIC GlobalError
{
 public:
   enum MsgType
   {
      FATAL = 0,
      ERROR = 1,
      WARNING = 2,
      NOTICE = 3,
      DEBUG = 4
   };

   GlobalError() = default;
   ~GlobalError() = default;

   /* Error() and Warning() accept printf-style format strings. The
    * stubs ignore them; the variadic arguments are simply consumed
    * without parsing. */
   bool Error(const char *msgfmt, ...) APT_FORMAT_PRINTF(2, 3)
   {
      (void)msgfmt;
      return false;
   }
   bool Warning(const char *msgfmt, ...) APT_FORMAT_PRINTF(2, 3)
   {
      (void)msgfmt;
      return false;
   }
   bool Notice(const char *msgfmt, ...) APT_FORMAT_PRINTF(2, 3)
   {
      (void)msgfmt;
      return false;
   }
   bool Debug(const char *msgfmt, ...) APT_FORMAT_PRINTF(2, 3)
   {
      (void)msgfmt;
      return false;
   }

   /* InsertErr is the underlying entry point used by some Synaptic
    * code paths. */
   bool InsertErr(MsgType /*type*/, const char *msgfmt, ...) APT_FORMAT_PRINTF(3, 4)
   {
      (void)msgfmt;
      return false;
   }

   /* Discard all pending messages. */
   void Discard() {}

   /* Pop the oldest pending message into `msg`. Returns false if there
    * are no pending messages. */
   bool PopMessage(std::string &/*msg*/) { return false; }

   /* True if no messages are pending. */
   bool empty() const { return true; }

   /* Number of pending messages. */
   std::size_t size() const { return 0; }

   /* Write all pending messages to the given FILE*. A NULL `f` defaults
    * to stderr (matching the real apt-pkg behaviour when called with no
    * arguments). */
   void DumpErrors(std::FILE * /*f*/ = nullptr) {}

 private:
   /* The real GlobalError stores messages in a std::list<Item>; the
    * stub doesn't store anything. */
};

/* The single global error instance. Defined in apt-pkg-stub.cc. */
extern GlobalError *_error APT_PUBLIC;
