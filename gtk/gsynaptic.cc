/* gsynaptic.cc - main()
 *
 * Copyright (c) 2001-2003 Alfredo K. Kojima
 *
 * Author: Alfredo K. Kojima <kojima@conectiva.com.br>
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
 */

#include "config.h" // IWYU pragma: associated

#include "i18n.h"
#include "raptoptions.h"
#include "rconfiguration.h"
#include "rgmainwindow.h"
#include "rgpackagestatus.h"
#include "rgpkgdetails.h"
#include "rguserdialog.h"
#include "rgutils.h"
#include "rpackagelister.h"
#include "rpackageview.h"

#include <apt-pkg/configuration.h>
#include <apt-pkg/error.h>
#include <apt-pkg/fileutl.h>
#include <cassert>
#include <cerrno>
#include <clocale>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <gtk/gtk.h>
#include <iostream>
#include <libintl.h>
#include <string>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>

using namespace std;

typedef enum { UPDATE_ASK, UPDATE_CLOSE, UPDATE_AUTO } UpdateType;

static gint applicationHandleLocalOptions(GApplication *app,
                                          GVariantDict *options,
                                          gpointer user_data);
static void applicationStartup(GApplication *app, gpointer user_data);
static void applicationActivate(GApplication *app, gpointer user_data);


static GOptionEntry option_entries[] = {
   {"repositories",
    'r',
    G_OPTION_FLAG_NONE,
    G_OPTION_ARG_NONE,
    nullptr,
    N_("Open in the repository screen"),
    nullptr},
   {"filter-file",
    'f',
    G_OPTION_FLAG_NONE,
    G_OPTION_ARG_FILENAME,
    nullptr,
    N_("Give an alternative filter file"),
    N_("<filter-file>")},
   {"title",
    't',
    G_OPTION_FLAG_NONE,
    G_OPTION_ARG_STRING,
    nullptr,
    N_("Give an alternative main window title (e.g. hostname with `uname -n`)"),
    N_("title")},
   {"initial-filter",
    'i',
    G_OPTION_FLAG_NONE,
    G_OPTION_ARG_STRING,
    nullptr,
    N_("Start with the initial filter with given name"),
    N_("<filter-name>")},
   {"option",
    'o',
    G_OPTION_FLAG_NONE,
    G_OPTION_ARG_STRING_ARRAY,
    nullptr,
    N_("Set an arbitrary configuration option, eg -o dir::cache=/tmp"),
    N_("<option>")},
   {"upgrade-mode",
    '\0',
    G_OPTION_FLAG_NONE,
    G_OPTION_ARG_NONE,
    nullptr,
    N_("Call Upgrade and display changes"),
    nullptr},
   {"dist-upgrade-mode",
    '\0',
    G_OPTION_FLAG_NONE,
    G_OPTION_ARG_NONE,
    nullptr,
    N_("Call DistUpgrade and display changes"),
    nullptr},
   {"update-at-startup",
    '\0',
    G_OPTION_FLAG_NONE,
    G_OPTION_ARG_NONE,
    nullptr,
    N_("Call \"Reload\" on startup"),
    nullptr},
   {"non-interactive",
    '\0',
    G_OPTION_FLAG_NONE,
    G_OPTION_ARG_NONE,
    nullptr,
    N_("Never prompt for user input"),
    nullptr},
   {"task-window",
    '\0',
    G_OPTION_FLAG_NONE,
    G_OPTION_ARG_NONE,
    nullptr,
    N_("Open with task window"),
    nullptr},
   {"add-cdrom",
    '\0',
    G_OPTION_FLAG_NONE,
    G_OPTION_ARG_STRING,
    nullptr,
    N_("Add a cdrom at startup"),
    N_("<path-for-cdrom>")},
   {"ask-cdrom",
    '\0',
    G_OPTION_FLAG_NONE,
    G_OPTION_ARG_NONE,
    nullptr,
    N_("Ask for adding a cdrom and exit"),
    nullptr},
   {"test-me-harder",
    '\0',
    G_OPTION_FLAG_NONE,
    G_OPTION_ARG_NONE,
    nullptr,
    N_("Run test in a loop"),
    nullptr},
   {"set-selections",
    '\0',
    G_OPTION_FLAG_HIDDEN,
    G_OPTION_ARG_NONE,
    nullptr,
    nullptr,
    nullptr},
   {"set-selections-file",
    '\0',
    G_OPTION_FLAG_HIDDEN,
    G_OPTION_ARG_STRING,
    nullptr,
    nullptr,
    nullptr},
   {"progress-str",
    '\0',
    G_OPTION_FLAG_HIDDEN,
    G_OPTION_ARG_STRING,
    nullptr,
    nullptr,
    nullptr},
   {"finish-str",
    '\0',
    G_OPTION_FLAG_HIDDEN,
    G_OPTION_ARG_STRING,
    nullptr,
    nullptr,
    nullptr},
   {"hide-main-window",
    '\0',
    G_OPTION_FLAG_HIDDEN,
    G_OPTION_ARG_NONE,
    nullptr,
    nullptr,
    nullptr},
   {0}};


static void SetLanguages()
{
   string LangList;
   if (_config->FindB("Synaptic::DynamicLanguages", true) == false) {
      LangList = _config->Find("Synaptic::Languages", "");
   } else {
      char *lang = getenv("LANG");
      if (lang == NULL) {
         lang = getenv("LC_MESSAGES");
         if (lang == NULL) {
            lang = getenv("LC_ALL");
         }
      }
      if (lang != NULL && strcmp(lang, "C") != 0)
         LangList = lang;
   }

   _config->Set("Volatile::Languages", LangList);
}

void welcome_dialog(RGMainWindow *mainWindow)
{
   bool show_welcome = true;
   char *settings_path = g_strdup_printf("%s/settings", RConfDir().c_str());
   char *contents = NULL;
   GError *err = NULL;
   if (g_file_get_contents(settings_path, &contents, NULL, &err)) {
      if (strstr(contents, "showWelcomeDialog=false"))
         show_welcome = false;
      g_free(contents);
   }
   g_clear_error(&err);

   if (show_welcome && !_config->FindB("Volatile::Upgrade-Mode", false)) {
      RGGtkBuilderUserDialog dia(mainWindow);
      dia.run("welcome");
      GtkWidget *cb = GTK_WIDGET(
         gtk_builder_get_object(dia.getGtkBuilder(), "checkbutton_show_again"));
      if (cb) {
         bool show_again = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb));
         _config->Set("Synaptic::showWelcomeDialog", show_again);
         char *new_contents = g_strdup_printf("showWelcomeDialog=%s\n",
            show_again ? "true" : "false");
         g_file_set_contents(settings_path, new_contents, -1, NULL);
         g_free(new_contents);
      }
   }
   g_free(settings_path);
}

// lock stuff
static int sigterm_unix_signal_pipe_fds[2];
static GIOChannel *sigterm_iochn;

static void handle_sigusr1(int value)
{
   static char marker[1] = {'S'};

   /* write a 'S' character to the other end to tell about
    * the signal. Note that 'the other end' is a GIOChannel thingy
    * that is only called from the mainloop - thus this is how we
    * defer this since UNIX signal handlers are evil
    *
    * Oh, and write(2) is indeed reentrant */
   write(sigterm_unix_signal_pipe_fds[1], marker, 1);
}

static gboolean sigterm_iochn_data(GIOChannel *source,
                                   GIOCondition condition,
                                   gpointer user_data)
{
   GError *err = NULL;
   gchar data[1];
   gsize bytes_read;

   RGMainWindow *me = (RGMainWindow *)user_data;

   /* Empty the pipe */
   if (G_IO_STATUS_NORMAL !=
       g_io_channel_read_chars(source, data, 1, &bytes_read, &err)) {
      g_warning("Error emptying callout notify pipe: %s", err->message);
      g_error_free(err);
      return TRUE;
   }
   if (data[0] == 'S')
      me->activeWindowToForeground();

   return TRUE;
}

// test if a lock is aquired already, return 0 if no lock is found,
// the pid of the locking application or -1 on error
pid_t TestLock(string File)
{
   int FD = open(File.c_str(), 0);
   if (FD < 0) {
      if (errno == ENOENT) {
         // File does not exist, no there can't be a lock
         return 0;
      } else {
         // cout << "open, errno: " << errno << endl;
         // perror("open");
         return (-1);
      }
   }
   struct flock fl;
   fl.l_type = F_WRLCK;
   fl.l_whence = SEEK_SET;
   fl.l_start = 0;
   fl.l_len = 0;
   if (fcntl(FD, F_GETLK, &fl) < 0) {
      int Tmp = errno;
      close(FD);
      cerr << "fcntl error" << endl;
      errno = Tmp;
      return (-1);
   }
   close(FD);
   // lock is available
   if (fl.l_type == F_UNLCK)
      return (0);
   // file is locked by another process
   return (fl.l_pid);
}

// check if we can get a lock, must be done after we read the configuration
// if a lock is found and the app is synaptic send it a "come to foreground"
// signal (USR1) or if not synaptic display a error and exit
// 1. check if there is another synaptic running
//    a) if so, check if it runs interactive and we are not running interactive
//       *) if so, send signal and show message
//       *) if not, send signal
// 2. check if we can get a /var/lib/dpkg/lock
//    *) if not, show message and fail
// (Under HAVE_XBPS the lockfile is /var/db/xbps/lock instead.)
void check_and_aquire_lock()
{
   if (getuid() != 0)
      return;

   GtkWidget *dia;
   gchar *msg = NULL;
   pid_t LockedApp, runsNonInteractive;
   bool weNonInteractive;

   string SynapticLock = RConfDir() + "/lock";
   string SynapticNonInteractiveLock = RConfDir() + "/lock.non-interactive";
   weNonInteractive = _config->FindB("Volatile::Non-Interactive", false);

   // 1. test for another synaptic
   LockedApp = TestLock(SynapticLock);
   if (LockedApp > 0) {
      runsNonInteractive = TestLock(SynapticNonInteractiveLock);
      // cout << "runsNonIteractive: " << runsNonInteractive << endl;
      // cout << "weNonIteractive: " << weNonInteractive << endl;
      if (weNonInteractive && runsNonInteractive <= 0) {
         // message that we can't turn a non-interactive into a interactive
         // one
         msg = g_strdup_printf("<big><b>%s</b></big>\n\n%s",
                               _("Another SynapticXBPS is running"),
                               _("There is another SynapticXBPS running in "
                                 "interactive mode. Please close it first. "));
      } else if (runsNonInteractive > 0) {
         msg = g_strdup_printf("<big><b>%s</b></big>\n\n%s",
                               _("Another SynapticXBPS is running"),
                               _("There is another SynapticXBPS running in "
                                 "non-interactive mode. Please wait for it "
                                 "to finish first."));
      }

      if (msg != NULL) {
         dia = gtk_message_dialog_new(
            NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, NULL);

         gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dia), msg);
         gtk_dialog_run(GTK_DIALOG(dia));
         gtk_widget_destroy(dia);
      }
      g_free(msg);

      cout
         << "Another SynapticXBPS is running. Trying to bring it to the foreground"
         << endl;
      kill(LockedApp, SIGUSR1);
      exit(0);
   }

   // 2. test if we can get a lock
#ifdef HAVE_XBPS
   // Under HAVE_XBPS the package database lock is /var/db/xbps/lock.
   // We hardcode the path here instead of going through APT's
   // _config->Find("Dir::State::status") which has no meaning under
   // the XBPS backend.
   string AdminDir = "/var/db/xbps/";
#else
   string AdminDir = flNotFile(_config->Find("Dir::State::status"));
#endif
   LockedApp = TestLock(AdminDir + "lock");
   if (LockedApp > 0) {
      msg = g_strdup_printf("<big><b>%s</b></big>\n\n%s",
                            _("Unable to get exclusive lock"),
#ifdef HAVE_XBPS
                            _("This usually means that another "
                              "package management application "
                              "(like xbps-install or xbps-remove) is "
                              "already running. Please close that "
                              "application first."));
#else
                            _("This usually means that another "
                              "package management application "
                              "(like apt-get or aptitude) is "
                              "already running. Please close that "
                              "application first."));
#endif
      dia = gtk_message_dialog_new(
         NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, NULL);

      gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dia), msg);
      gtk_dialog_run(GTK_DIALOG(dia));
      g_free(msg);
      exit(1);
   }

   // we can't get a lock?!?
   if (GetLock(SynapticLock, true) < 0) {
      _error->DumpErrors();
      exit(1);
   }
   // if we run nonInteracitvely, get a seond lock
   if (weNonInteractive && GetLock(SynapticNonInteractiveLock, true) < 0) {
      _error->DumpErrors();
      exit(1);
   }
}

int main(int argc, char **argv)
{
   /* Build-identifier so the user can verify which binary is running.
    * Print to stderr at startup so it shows up in gdb output and
    * terminal sessions. Also searchable via `strings binary | grep BUILD_ID`. */
   static const char build_id[] = "BUILD_ID_SYNAPTIC_XBPS_2026_10_01_FIX35_CLEAN_CODE";
   fprintf(stderr, "synaptic-xbps: %s\n", build_id);
   fflush(stderr);
   /* Mark build_id as "used" so the compiler doesn't optimize it out. */
   (void)build_id;

   /* The program runs as the regular user. It does NOT auto-elevate
    * at startup. Operations that require root (install / remove /
    * upgrade / repository changes) call pkexec themselves at the
    * point of need — same model as the original Synaptic. This avoids
    * the LibreWolf "running as root" problem when opening the browser
    * and respects the principle of least privilege. */
   /* (No auto-pkexec block here.) */

   /* If we ARE root (user ran sudo/pkexec manually), recover env vars
    * and theme from the original user session. */
   if (getuid() == 0) {
      const char *orig_uid = getenv("PKEXEC_UID");
      if (!orig_uid) orig_uid = getenv("SUDO_UID");
      if (orig_uid) {
         char *rt = g_strdup_printf("/run/user/%s", orig_uid);
         if (!getenv("XDG_RUNTIME_DIR"))
            setenv("XDG_RUNTIME_DIR", rt, 1);
         g_free(rt);
         struct passwd *pw = getpwuid(atoi(orig_uid));
         if (pw && pw->pw_dir) {
            if (!getenv("HOME"))
               setenv("HOME", pw->pw_dir, 1);
            char *conf = g_strdup_printf("%s/.config", pw->pw_dir);
            if (!getenv("XDG_CONFIG_HOME"))
               setenv("XDG_CONFIG_HOME", conf, 1);
            g_free(conf);
            char *data = g_strdup_printf("%s/.local/share", pw->pw_dir);
            if (!getenv("XDG_DATA_HOME"))
               setenv("XDG_DATA_HOME", data, 1);
            g_free(data);
         }
         /* Recover DBUS_SESSION_BUS_ADDRESS so xdg-open and other
          * D-Bus clients can connect to the user's session bus.
          * Without this, xdg-open can't tell the running browser
          * to open a new tab — it launches a new browser instance. */
         if (!getenv("DBUS_SESSION_BUS_ADDRESS")) {
            char *dbus_addr = g_strdup_printf("unix:path=%s/bus",
               getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "/run/user/0");
            setenv("DBUS_SESSION_BUS_ADDRESS", dbus_addr, 1);
            g_free(dbus_addr);
         }
      }
      if (!getenv("DISPLAY"))
         setenv("DISPLAY", ":0", 1);
      if (!getenv("WAYLAND_DISPLAY"))
         setenv("WAYLAND_DISPLAY", "wayland-0", 1);
      if (!getenv("XAUTHORITY")) {
         const char *home = getenv("HOME");
         if (!home) home = "/root";
         char *xa = g_strdup_printf("%s/.Xauthority", home);
         setenv("XAUTHORITY", xa, 1);
         g_free(xa);
      }
      if (!getenv("GDK_BACKEND"))
         setenv("GDK_BACKEND", "wayland,x11", 1);
      g_mkdir_with_parents("/etc/synaptic", 0755);
   }

#ifdef ENABLE_NLS
   setlocale(LC_ALL, "");
   /* Under pkexec the root environment often has LANG=C or empty,
    * which would leave the UI untranslated. We detect this case and
    * fall back to Spanish (the only locale we ship a .mo for).
    * If the user has LANG=en_US.UTF-8 or similar, we leave it alone
    * — the program will show in English (or whatever the locale says).
    * This respects the user's system language choice. */
   const char *lang_env = getenv("LANG");
   const char *language_env = getenv("LANGUAGE");
   if (!lang_env || !*lang_env ||
       (strstr(lang_env, "C") && !strstr(lang_env, "es") && !strstr(lang_env, "en"))) {
      /* LANG is C or empty — fall back to Spanish since that's the
       * only .mo we ship. The user can override with LANGUAGE=xx. */
      if (!language_env || !*language_env) {
         setenv("LANGUAGE", "es", 1);
      }
      setlocale(LC_ALL, "es_ES.UTF-8");
   }
   fprintf(stderr, "synaptic-xbps: LANG=%s LANGUAGE=%s locale=%s\n",
           lang_env ? lang_env : "(null)",
           getenv("LANGUAGE") ? getenv("LANGUAGE") : "(null)",
           setlocale(LC_ALL, NULL) ? setlocale(LC_ALL, NULL) : "(null)");
   fflush(stderr);

#ifdef HAVE_XBPS
   {
      GBytes *mo = g_resources_lookup_data(
         "/io/github/mvo5/synaptic/locale/es/LC_MESSAGES/synaptic.mo",
         G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
      if (mo) {
         const char *td = getenv("TMPDIR");
         if (!td) td = "/tmp";
         char *base = g_strdup_printf("%s/sxbps-XXXXXX", td);
         char *dir = g_mkdtemp(base);
         if (dir) {
            char *ld = g_strdup_printf("%s/es/LC_MESSAGES", dir);
            g_mkdir_with_parents(ld, 0755);
            char *path = g_strdup_printf("%s/synaptic.mo", ld);
            g_file_set_contents(path,
               (const gchar *)g_bytes_get_data(mo, NULL),
               (gsize)g_bytes_get_size(mo), NULL);
            bindtextdomain(GETTEXT_PACKAGE, dir);
            g_free(path); g_free(ld);
         }
         g_free(base);
         g_bytes_unref(mo);
      }
   }
#else
   bindtextdomain(GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
#endif
   bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
   textdomain(GETTEXT_PACKAGE);
#   ifdef HAVE_RPM
   bind_textdomain_codeset("rpm", "UTF-8");
#   endif
#endif

   GtkApplication *app = gtk_application_new("io.github.mvo5.synaptic",
                                             G_APPLICATION_DEFAULT_FLAGS);

   g_application_add_main_option_entries(G_APPLICATION(app), option_entries);

   g_signal_connect(app, "startup", G_CALLBACK(applicationStartup), nullptr);
   g_signal_connect(app,
                    "handle-local-options",
                    G_CALLBACK(applicationHandleLocalOptions),
                    nullptr);
   g_signal_connect(app, "activate", G_CALLBACK(applicationActivate), nullptr);

   int status = g_application_run(G_APPLICATION(app), argc, argv);
   g_object_unref(app);

   return status;
}

static gint applicationHandleLocalOptions(GApplication *app,
                                          GVariantDict *options,
                                          gpointer user_data)
{
   gboolean flag;
   gchar *arg;
   gchar **args;

   if (g_variant_dict_lookup(options, "repositories", "b", &flag))
      _config->Set("Volatile::startInRepositories", flag ? "true" : "false");
   if (g_variant_dict_lookup(options, "filter-file", "&s", &arg))
      _config->Set("Volatile::filterFile", arg);
   if (g_variant_dict_lookup(options, "title", "&s", &arg))
      _config->Set("Volatile::MyName", arg);
   if (g_variant_dict_lookup(options, "initial-filter", "&s", &arg))
      _config->Set("Volatile::initialFilter", arg);
   if (g_variant_dict_lookup(options, "option", "^a&s", &args)) {
      for (int i = 0; args[i] != nullptr; ++i) {
         arg = args[i];
         const char *p = strchr(arg, '=');
         if (p)
            _config->Set(string(arg, p - arg), p + 1);
         else
            g_warning(_("Option %s: Configuration item specification must have "
                        "an =<val>."),
                      arg);
      }
      g_free(args);
   }
   if (g_variant_dict_lookup(options, "upgrade-mode", "b", &flag))
      _config->Set("Volatile::Upgrade-Mode", flag ? "true" : "false");
   if (g_variant_dict_lookup(options, "dist-upgrade-mode", "b", &flag))
      _config->Set("Volatile::DistUpgrade-Mode", flag ? "true" : "false");
   if (g_variant_dict_lookup(options, "update-at-startup", "b", &flag))
      _config->Set("Volatile::Update-Mode", flag ? "true" : "false");
   if (g_variant_dict_lookup(options, "non-interactive", "b", &flag))
      _config->Set("Volatile::Non-Interactive", flag ? "true" : "false");
   if (g_variant_dict_lookup(options, "task-window", "b", &flag))
      _config->Set("Volatile::TaskWindow", flag ? "true" : "false");
   if (g_variant_dict_lookup(options, "add-cdrom", "&s", &arg))
      _config->Set("Volatile::AddCdrom-Mode", arg);
   if (g_variant_dict_lookup(options, "ask-cdrom", "b", &flag))
      _config->Set("Volatile::AskCdrom-Mode", flag ? "true" : "false");
   if (g_variant_dict_lookup(options, "test-me-harder", "b", &flag))
      _config->Set("Volatile::TestMeHarder", flag ? "true" : "false");
   if (g_variant_dict_lookup(options, "set-selections", "b", &flag))
      _config->Set("Volatile::Set-Selections", flag ? "true" : "false");
   if (g_variant_dict_lookup(options, "set-selections-file", "&s", &arg))
      _config->Set("Volatile::Set-Selections-File", arg);
   if (g_variant_dict_lookup(options, "progress-str", "&s", &arg))
      _config->Set("Volatile::InstallProgressStr", arg);
   if (g_variant_dict_lookup(options, "finish-str", "&s", &arg))
      _config->Set("Volatile::InstallFinishedStr", arg);
   if (g_variant_dict_lookup(options, "hide-main-window", "b", &flag))
      _config->Set("Volatile::HideMainwindow", flag ? "true" : "false");

   return -1;
}

static void applicationStartup(GApplication *app, gpointer user_data)
{
   gtk_icon_theme_append_search_path(gtk_icon_theme_get_default(),
                                     PACKAGE_DATA_DIR "/icons");

   if (!RInitConfiguration("synaptic.conf")) {
      RGUserDialog userDialog;
      userDialog.showErrors();
      exit(1);
   }

   // check if there is another application runing and
   // act accordingly
   check_and_aquire_lock();

   // read configuration early
   _roptions->restore();

   SetLanguages();

   // init the static pkgStatus class. this loads the status pixmaps
   // and colors
   RGPackageStatus::pkgStatus.init();
}

static void applicationActivate(GApplication *app, gpointer user_data)
{
   if (auto window = gtk_application_get_active_window(GTK_APPLICATION(app))) {
      gtk_window_present(window);
      return;
   }

   bool UpdateMode = _config->FindB("Volatile::Update-Mode", false);
   bool NonInteractive = _config->FindB("Volatile::Non-Interactive", false);

   RPackageLister *packageLister = new RPackageLister();

   /* Register a UI pump callback so the backend can process GTK
    * events while doing blocking operations (xbps_rpool_sync,
    * xbps_transaction_commit). */
#ifdef HAVE_XBPS
   packageLister->setUiPumpCallback([]() {
      while (gtk_events_pending())
         gtk_main_iteration_do(FALSE);
      usleep(20000); /* 20ms */
   });
#endif

   RGMainWindow *mainWindow =
      new RGMainWindow(GTK_APPLICATION(app), packageLister, "main");

   // install a sigusr1 signal handler and put window into
   // foreground when called. use the io_watch trick because gtk is not
   // reentrant
   // SIGUSR1 handling via pipes
   if (pipe(sigterm_unix_signal_pipe_fds) != 0) {
      g_warning("Could not setup pipe, errno=%d", errno);
      exit(1);
   }
   sigterm_iochn = g_io_channel_unix_new(sigterm_unix_signal_pipe_fds[0]);
   if (sigterm_iochn == NULL) {
      g_warning("Could not create GIOChannel");
      exit(1);
   }
   g_io_add_watch(sigterm_iochn, G_IO_IN, sigterm_iochn_data, mainWindow);
   signal(SIGUSR1, handle_sigusr1);
   // -------------------------------------------------------------

   // read which default distro to use
   string s = _config->Find("Synaptic::DefaultDistro", "");
   if (s != "")
      _config->Set("APT::Default-Release", s);

#ifndef HAVE_RPM
   mainWindow->setTitle(_("SynapticXBPS Package Manager "));
#else
   mainWindow->setTitle(_config->Find("Synaptic::MyName", "SynapticXBPS"));
#endif
   // this is for stuff like "synaptic -t `uname -n`"
   s = _config->Find("Volatile::MyName", "");
   if (s.size() > 0)
      mainWindow->setTitle(s);

   if (_config->FindB("Volatile::HideMainwindow", false))
      mainWindow->hide();
   else
      mainWindow->show();

   RGFlushInterface();

   mainWindow->setInterfaceLocked(true);

   string cd_mount_point = _config->Find("Volatile::AddCdrom-Mode", "");
   if (!cd_mount_point.empty()) {
      _config->Set("Acquire::cdrom::mount", cd_mount_point);
      _config->Set("APT::CDROM::NoMount", true);
      mainWindow->cbAddCDROM(nullptr, nullptr, mainWindow);
   } else if (_config->FindB("Volatile::AskCdrom-Mode", false)) {
      mainWindow->cbAddCDROM(nullptr, nullptr, mainWindow);
      exit(0);
   }

   /* ----- continuation after async openCache finishes -----
    * Mirrors iruka-xbps's load_done_cb + applyLoadedPackages pattern.
    * The synchronous flow that used to live below the openCache() call
    * (selections, UpdateMode, UpgradeMode, welcome dialog) is now a
    * g_timeout_add poll that fires on the GTK main thread once the
    * worker thread completes. */
   struct AfterOpenCtx {
      RPackageLister *lister;
      RGMainWindow   *win;
      bool            updateMode;
      bool            nonInteractive;
   };
   AfterOpenCtx *ctx = new AfterOpenCtx{packageLister, mainWindow,
                                        UpdateMode, NonInteractive};

   auto after_open_poll = [](gpointer data) -> gboolean {
      AfterOpenCtx *c = static_cast<AfterOpenCtx *>(data);
      if (!c->lister->openCacheAsyncIsDone())
         return G_SOURCE_CONTINUE;

      if (c->lister->openCacheAsyncResult() != 0) {
         c->win->showErrors();
         delete c;
         exit(1);
      }

      /* Cache is now valid and was built off-thread. Finalize must
       * run on the GTK main thread because it mutates the same
       * _packages / _viewPackages vectors the treeview reads. */
      c->lister->openCacheFinalize();

      c->win->restoreState();
      c->win->showErrors();
      c->win->setTreeLocked(false);

      if (_config->FindB("Volatile::startInRepositories", false)) {
         c->win->cbShowSourcesWindow(NULL, NULL, c->win);
      }

      // selections from stdin
      if (_config->FindB("Volatile::Set-Selections", false) == true) {
         c->lister->unregisterObserver(c->win);
         c->lister->readSelections(cin);
         c->lister->registerObserver(c->win);
      }

      // selections from a file
      string selections_filename;
      selections_filename = _config->Find("Volatile::Set-Selections-File", "");
      if (selections_filename != "") {
         c->lister->unregisterObserver(c->win);
         ifstream selfile(selections_filename.c_str());
         c->lister->readSelections(selfile);
         selfile.close();
         c->lister->registerObserver(c->win);
      }

      c->win->setInterfaceLocked(false);

      if (c->updateMode) {
         c->win->cbUpdateClicked(nullptr, nullptr, c->win);
         /* cbUpdateClicked already triggers an async reload via
          * updateCacheStart + openCacheAsync; we don't need a second
          * blocking openCache here. */
         c->win->setTreeLocked(false);
         c->win->showErrors();
         c->win->changeView(PACKAGE_VIEW_STATUS, _("Installed (upgradable)"));
      }

      if (_config->FindB("Volatile::TestMeHarder", false)) {
         _config->Set("Volatile::Non-Interactive", "true");
         _config->Set("Synaptic::closeZvt", "true");

         while (true) {
            c->win->cbUpdateClicked(nullptr, nullptr, c->win);
            c->win->changeView(PACKAGE_VIEW_STATUS, _("Installed"));
            GtkTreePath *p = gtk_tree_path_new_from_string("0");
            c->win->cbPackageListRowActivated(NULL, p, NULL, c->win);

            GObject *o = (GObject *)g_object_new(G_TYPE_OBJECT, NULL);
            g_object_set_data(o, "me", c->win);
            c->win->cbPkgAction(PKG_REINSTALL);
            c->win->cbProceedClicked(nullptr, nullptr, c->win);
         }
      }

      if (_config->FindB("Volatile::Upgrade-Mode", false) ||
          _config->FindB("Volatile::DistUpgrade-Mode", false)) {
         c->win->cbUpgradeClicked(nullptr, nullptr, c->win);
         c->win->changeView(PACKAGE_VIEW_CUSTOM, _("Marked Changes"));
      }

      if (_config->FindB("Volatile::TaskWindow", false)) {
         c->win->cbTasksClicked(nullptr, nullptr, c->win);
      }

      string filter = _config->Find("Volatile::initialFilter", "");
      if (filter != "")
         c->win->changeView(PACKAGE_VIEW_CUSTOM, filter);

      if (c->nonInteractive) {
         c->win->cbProceedClicked(nullptr, nullptr, c->win);
         exit(0);
      } else {
         welcome_dialog(c->win);
         gtk_widget_grab_focus(GTK_WIDGET(gtk_builder_get_object(
            c->win->getGtkBuilder(), "entry_fast_search")));
      }

      delete c;
      return G_SOURCE_REMOVE;
   };

#ifdef HAVE_XBPS
   /* Fire the async openCache and poll for completion. The worker
    * thread does the popen("xbps-query -l") + popen("xbps-install -un")
    * calls so the GTK main loop stays responsive (no 80+ vfork storm
    * on the UI thread). */
   if (!UpdateMode) {
      mainWindow->setTreeLocked(true);
      if (!packageLister->openCacheAsync()) {
         /* Fallback to synchronous open if the thread couldn't be
          * spawned (extremely unlikely). */
         if (!packageLister->openCache()) {
            mainWindow->showErrors();
            exit(1);
         }
         /* Run the continuation synchronously. */
         mainWindow->restoreState();
         mainWindow->showErrors();
         mainWindow->setTreeLocked(false);
         mainWindow->setInterfaceLocked(false);
         welcome_dialog(mainWindow);
         gtk_widget_grab_focus(GTK_WIDGET(gtk_builder_get_object(
            mainWindow->getGtkBuilder(), "entry_fast_search")));
         return;
      }
      g_timeout_add(50, after_open_poll, ctx);
      return;
   }

   /* UpdateMode: same as before — kick cbUpdateClicked which itself
    * calls updateCacheStart + openCacheAsync. We still need an initial
    * async open so the package list is non-empty before the user
    * interacts. */
   mainWindow->setTreeLocked(true);
   if (!packageLister->openCacheAsync()) {
      if (!packageLister->openCache()) {
         mainWindow->showErrors();
         exit(1);
      }
   }
   g_timeout_add(50, after_open_poll, ctx);
#else
   /* APT path — unchanged. */
   if (!UpdateMode) {
      mainWindow->setTreeLocked(true);
      if (!packageLister->openCache()) {
         mainWindow->showErrors();
         exit(1);
      }
      mainWindow->restoreState();
      mainWindow->showErrors();
      mainWindow->setTreeLocked(false);
   }

   if (_config->FindB("Volatile::startInRepositories", false)) {
      mainWindow->cbShowSourcesWindow(NULL, NULL, mainWindow);
   }

   // selections from stdin
   if (_config->FindB("Volatile::Set-Selections", false) == true) {
      packageLister->unregisterObserver(mainWindow);
      packageLister->readSelections(cin);
      packageLister->registerObserver(mainWindow);
   }

   // selections from a file
   string selections_filename;
   selections_filename = _config->Find("Volatile::Set-Selections-File", "");
   if (selections_filename != "") {
      packageLister->unregisterObserver(mainWindow);
      ifstream selfile(selections_filename.c_str());
      packageLister->readSelections(selfile);
      selfile.close();
      packageLister->registerObserver(mainWindow);
   }

   mainWindow->setInterfaceLocked(false);

   if (UpdateMode) {
      mainWindow->cbUpdateClicked(nullptr, nullptr, mainWindow);
      mainWindow->setTreeLocked(true);
      if (!packageLister->openCache()) {
         mainWindow->showErrors();
         exit(1);
      }
      mainWindow->restoreState();
      mainWindow->setTreeLocked(false);
      mainWindow->showErrors();
      mainWindow->changeView(PACKAGE_VIEW_STATUS, _("Installed (upgradable)"));
   }

   if (_config->FindB("Volatile::TestMeHarder", false)) {
      _config->Set("Volatile::Non-Interactive", "true");
      _config->Set("Synaptic::closeZvt", "true");

      while (true) {
         mainWindow->cbUpdateClicked(nullptr, nullptr, mainWindow);
         mainWindow->changeView(PACKAGE_VIEW_STATUS, _("Installed"));
         GtkTreePath *p = gtk_tree_path_new_from_string("0");
         mainWindow->cbPackageListRowActivated(NULL, p, NULL, mainWindow);

         GObject *o = (GObject *)g_object_new(G_TYPE_OBJECT, NULL);
         g_object_set_data(o, "me", mainWindow);
         mainWindow->cbPkgAction(PKG_REINSTALL);
         mainWindow->cbProceedClicked(nullptr, nullptr, mainWindow);
      }
   }

   if (_config->FindB("Volatile::Upgrade-Mode", false) ||
       _config->FindB("Volatile::DistUpgrade-Mode", false)) {
      mainWindow->cbUpgradeClicked(nullptr, nullptr, mainWindow);
      mainWindow->changeView(PACKAGE_VIEW_CUSTOM, _("Marked Changes"));
   }

   if (_config->FindB("Volatile::TaskWindow", false)) {
      mainWindow->cbTasksClicked(nullptr, nullptr, mainWindow);
   }

   string filter = _config->Find("Volatile::initialFilter", "");
   if (filter != "")
      mainWindow->changeView(PACKAGE_VIEW_CUSTOM, filter);

   if (NonInteractive) {
      mainWindow->cbProceedClicked(nullptr, nullptr, mainWindow);
      exit(0);
   } else {
      welcome_dialog(mainWindow);
      gtk_widget_grab_focus(GTK_WIDGET(gtk_builder_get_object(
         mainWindow->getGtkBuilder(), "entry_fast_search")));
   }
#endif
}
