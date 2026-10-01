/* rgpkgdetails.cc - show details of a pkg
 *
 * Copyright (c) 2004 Michael Vogt
 *
 * Author: Michael Vogt <mvo@debian.org>
 *         Gustavo Niemeyer <niemeyer@conectiva.com>
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

#include "rgpkgdetails.h"

#include "i18n.h"
#include "rgfetchprogress.h"
#include "rggtkbuilderwindow.h"
#include "rgmainwindow.h"
#include "rgpackagestatus.h"
#include "rgutils.h"
#include "rpackage.h"
#include "sections_trans.h"

#include <apt-pkg/acquire.h>
#include <apt-pkg/configuration.h>
#include <cassert>
#include <cstring>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <glib-object.h>
#include <glib.h>
#include <gobject/gclosure.h>
#include <gtk/gtk.h>
#include <sstream>
#include <pango/pango-font.h>
#include <pwd.h>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

class RGWindow;

using namespace std;

RGPkgDetailsWindow::RGPkgDetailsWindow(RGWindow *parent)
   : RGGtkBuilderWindow(parent, "details")
{
   g_signal_connect(gtk_builder_get_object(_builder, "button_close"),
                    "clicked",
                    G_CALLBACK(cbCloseClicked),
                    this);

   GtkWidget *comboDepends =
      GTK_WIDGET(gtk_builder_get_object(_builder, "combobox_depends"));
   g_signal_connect(G_OBJECT(comboDepends),
                    "changed",
                    G_CALLBACK(cbDependsMenuChanged),
                    this);
   GtkListStore *relTypes = gtk_list_store_new(1, G_TYPE_STRING);
   GtkTreeIter relIter;

   // HACK: the labels for the combo box items are defined as
   //       the relOptions array in gtk/rgmainwindow.h.
   //       We already include it, so we get those for free.
   for (int i = 0; relOptions[i] != NULL; i++) {
      gtk_list_store_append(relTypes, &relIter);
      gtk_list_store_set(relTypes, &relIter, 0, _(relOptions[i]), -1);
   }
   gtk_combo_box_set_model(GTK_COMBO_BOX(comboDepends),
                           GTK_TREE_MODEL(relTypes));
   GtkCellRenderer *relRenderText = gtk_cell_renderer_text_new();
   gtk_cell_layout_clear(GTK_CELL_LAYOUT(comboDepends));
   gtk_cell_layout_pack_start(
      GTK_CELL_LAYOUT(comboDepends), relRenderText, FALSE);
   gtk_cell_layout_add_attribute(
      GTK_CELL_LAYOUT(comboDepends), relRenderText, "text", 0);
   gtk_combo_box_set_active(GTK_COMBO_BOX(comboDepends), 0);

   GtkWidget *label;
   label = GTK_WIDGET(gtk_builder_get_object(_builder, "label_maintainer"));
   g_signal_connect(
      G_OBJECT(label), "activate-link", G_CALLBACK(cbOpenLink), NULL);
}

void RGPkgDetailsWindow::cbCloseClicked(GtkWidget *self, void *data)
{
   RGPkgDetailsWindow *me = static_cast<RGPkgDetailsWindow *>(data);

   me->hide();
}

vector<string> RGPkgDetailsWindow::formatDepInformation(
   vector<DepInformation> deps)
{
   vector<string> depStrings;
   string depStr;

   for (unsigned int i = 0; i < deps.size(); i++) {
      depStr = "";

      // type in bold (Depends, PreDepends)
      depStr += string("<b>") + _(DepTypeStr[deps[i].type]) + string(":</b> ");

      // virutal is italic
      if (deps[i].isVirtual) {
         depStr += string("<i>") + deps[i].name + string("</i>");
      } else {
         // is real pkg
         depStr += deps[i].name;
         // additional version information
         if (deps[i].version != NULL) {
            gchar *s = g_markup_escape_text(deps[i].versionComp, -1);
            depStr += string(" (") + s + deps[i].version + string(")");
            g_free(s);
         }
      }

      // this is for or-ed dependencies (make them one-line)
      while (deps[i].isOr) {
         depStr += " | ";
         // next dep
         i++;
         depStr += deps[i].name;
         // additional version information
         if (deps[i].version != NULL) {
            gchar *s = g_markup_escape_text(deps[i].versionComp, -1);
            depStr += string(" (") + s + deps[i].version + string(")");
            g_free(s);
         }
      }

      depStrings.push_back(depStr);
   }
   return depStrings;
}

void RGPkgDetailsWindow::cbShowBigScreenshot(GtkWidget *box,
                                             GdkEventButton *event,
                                             void *data)
{
   // cerr << "cbShowBigScreenshot" << endl;
   RPackage *pkg = (RPackage *)data;

   doShowBigScreenshot(pkg);
}

void RGPkgDetailsWindow::doShowBigScreenshot(RPackage *pkg)
{
#ifdef HAVE_XBPS
   // XBPS has no screenshot service. The original APT path fetched a
   // screenshot from screenshots.debian.net via a pkgAcquire object
   // and popped up a GTK dialog showing it. Under HAVE_XBPS we
   // simply do nothing — the "Get Screenshot" button is also hidden
   // (see fillInValues), so this code path is unreachable.
   (void)pkg;
#else
   RGFetchProgress *status = new RGFetchProgress(NULL);
   ;
   pkgAcquire fetcher(status);
   string filename = pkg->getScreenshotFile(&fetcher, false);
   GtkWidget *img = gtk_image_new_from_file(filename.c_str());
   GtkWidget *win = gtk_dialog_new();
   gtk_window_set_default_size(GTK_WINDOW(win), 500, 400);
   gtk_dialog_add_button(GTK_DIALOG(win), _("_Close"), GTK_RESPONSE_CLOSE);
   gtk_widget_show(img);
   GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(win));
   gtk_container_add(GTK_CONTAINER(content_area), img);
   gtk_dialog_run(GTK_DIALOG(win));
   gtk_widget_destroy(win);
#endif
}

void RGPkgDetailsWindow::cbShowScreenshot(GtkWidget *button, void *data)
{
   struct screenshot_info *si = (struct screenshot_info *)data;

#ifdef HAVE_XBPS
   // No screenshot service under XBPS — silently ignore the click.
   // The button itself is also conditionally compiled out (see
   // fillInValues), so this branch is unreachable in practice.
   (void)button;
   (void)si;
#else
   if (_config->FindB("Synaptic::InlineScreenshots") == false) {
      doShowBigScreenshot(si->pkg);
      return;
   } else {

      // hide button
      gtk_widget_hide(button);

      // get screenshot
      RGFetchProgress *status = new RGFetchProgress(NULL);
      ;
      pkgAcquire fetcher(status);
      string filename = si->pkg->getScreenshotFile(&fetcher);
      GtkWidget *event = gtk_event_box_new();
      GtkWidget *img = gtk_image_new_from_file(filename.c_str());
      gtk_container_add(GTK_CONTAINER(event), img);
      g_signal_connect(G_OBJECT(event),
                       "button_press_event",
                       G_CALLBACK(cbShowBigScreenshot),
                       (void *)si->pkg);
      gtk_text_view_add_child_at_anchor(
         GTK_TEXT_VIEW(si->textview), GTK_WIDGET(event), si->anchor);
      gtk_widget_show_all(event);
   }
#endif
}



gboolean RGPkgDetailsWindow::cbOpenLink(GtkWidget *label,
                                        gchar *uri,
                                        void *data)
{
   // std::cerr << "cbOpenLink: " << uri << std::endl;
   std::vector<const gchar *> cmd;
   cmd.push_back("xdg-open");
   cmd.push_back(uri);
   RunAsSudoUserCommand(cmd);

   return TRUE;
}

gboolean RGPkgDetailsWindow::cbOpenHomepage(GtkWidget *button, void *data)
{
   RPackage *pkg = (RPackage *)data;
   if (!pkg || !pkg->homepage() || !*pkg->homepage())
      return TRUE;

   const char *url = pkg->homepage();

   /* The program now runs as the regular user (not root), so we
    * don't need runuser/pkexec to open the browser. Just call
    * xdg-open directly — it will use the user's D-Bus session and
    * open the URL in a new tab of the running browser (or launch
    * the browser if not running). */
   static const char *openers[] = {
      "xdg-open",
      "gio",
      "x-www-browser",
      "sensible-browser",
      NULL,
   };

   pid_t pid = fork();
   if (pid < 0)
      return TRUE;
   if (pid == 0) {
      /* Child. Try each opener. */
      for (size_t i = 0; openers[i]; i++) {
         char binpath[256];
         snprintf(binpath, sizeof binpath, "/usr/bin/%s", openers[i]);
         if (access(binpath, X_OK) != 0 &&
             access(openers[i], X_OK) != 0)
            continue;
         if (strcmp(openers[i], "gio") == 0) {
            const char *argv[] = {"gio", "open", url, NULL};
            execvp(openers[i], (char *const *)argv);
         } else {
            const char *argv[] = {openers[i], url, NULL};
            execvp(openers[i], (char *const *)argv);
         }
      }
      _exit(127);
   }
   /* Parent — don't wait, the child is fire-and-forget. */
   return TRUE;
}

void RGPkgDetailsWindow::fillInValues(RGGtkBuilderWindow *me,
                                      RPackage *pkg,
                                      bool setTitle)
{
   assert(me != NULL);

   // TRANSLATORS: Title of the package properties dialog
   //              %s is the name of the package
   if (setTitle) {
      gchar *str = g_strdup_printf(_("%s Properties"), pkg->name());
      me->setTitle(str);
      g_free(str);
   }

   char *pkg_summary = g_strdup_printf("%s", pkg->name());
   me->setTextView("textview_pkgcommon", pkg_summary, true);
   g_free(pkg_summary);

   string maintainer = pkg->maintainer();
   int start_index = maintainer.find("<");
   int end_index = maintainer.rfind(">");
   if (start_index != -1 && end_index != -1) {
      gchar *maintainer_label = g_markup_printf_escaped(
         "<a href=\"mailto:%s\">%s</a>",
         maintainer.substr(start_index + 1, end_index - start_index - 1)
            .c_str(),
         maintainer.c_str());
      me->setMarkup("label_maintainer", maintainer_label);
      g_free(maintainer_label);
   } else {
      me->setLabel("label_maintainer", pkg->maintainer());
   }

   me->setPixmap("image_state", RGPackageStatus::pkgStatus.getPixbuf(pkg));
   me->setLabel("label_state",
                RGPackageStatus::pkgStatus.getLongStatusString(pkg));
   me->setLabel("label_priority", pkg->priority());
   me->setLabel("label_section", trans_section(pkg->section()).c_str());
   /* Hide the "Installed Version" and "Installed Size" rows entirely
    * if the package is not installed — same behaviour as the original
    * Synaptic frontend. Showing an empty value next to the label looks
    * confusing for not-installed packages. We hide both the value
    * label and its caption label (label106 / label105) so the row
    * disappears from the grid. */
   {
      bool installed = (pkg->getFlags() & RPackage::FInstalled) != 0;
      GtkWidget *lv = GTK_WIDGET(gtk_builder_get_object(me->getGtkBuilder(), "label_installed_version"));
      GtkWidget *ls = GTK_WIDGET(gtk_builder_get_object(me->getGtkBuilder(), "label_installed_size"));
      GtkWidget *cv = GTK_WIDGET(gtk_builder_get_object(me->getGtkBuilder(), "label106"));
      GtkWidget *cs = GTK_WIDGET(gtk_builder_get_object(me->getGtkBuilder(), "label105"));
      if (lv) gtk_widget_set_visible(lv, installed);
      if (ls) gtk_widget_set_visible(ls, installed);
      if (cv) gtk_widget_set_visible(cv, installed);
      if (cs) gtk_widget_set_visible(cs, installed);
      if (installed) {
         me->setLabel("label_installed_version", pkg->installedVersion());
         me->setLabel("label_installed_size", pkg->installedSize());
      }
   }

   me->setLabel("label_latest_version", pkg->availableVersion());
   /* Show available installed size in human-readable format */
   {
      long asz = pkg->availableInstalledSize();
      if (asz > 0) {
         char szbuf[64];
         double sz = (double)asz;
         if (sz >= 1073741824.0) snprintf(szbuf, sizeof szbuf, "%.2f GiB", sz/1073741824.0);
         else if (sz >= 1048576.0) snprintf(szbuf, sizeof szbuf, "%.2f MiB", sz/1048576.0);
         else if (sz >= 1024.0) snprintf(szbuf, sizeof szbuf, "%.2f KiB", sz/1024.0);
         else snprintf(szbuf, sizeof szbuf, "%ld B", asz);
         me->setLabel("label_latest_size", szbuf);
      } else {
         me->setLabel("label_latest_size", _("N/D"));
      }
   }
   /* Rename "Descargar" label to "Instalado" since XBPS doesn't have
    * a separate download size — the available size IS the installed size
    * of the package from the repo. */
   me->setLabel("label_latest_download_size_label", _("Instalado:"));
   me->setLabel("label_latest_download_size", pkg->availableInstalledSize());
   me->setLabel("label_source", pkg->srcPackage());

   // format description nicely and use emblems
   GtkWidget *textview;
   GtkTextBuffer *buf;
   GtkTextIter it, start, end;
   GtkWidget *emblem;
   const gchar *s;

   textview =
      GTK_WIDGET(gtk_builder_get_object(me->getGtkBuilder(), "text_descr"));
   if (textview == NULL) {
      g_warning("RGPkgDetailsWindow::fillInValues: text_descr widget not found");
      return;
   }
   gtk_text_view_set_editable(GTK_TEXT_VIEW(textview), FALSE);
   gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(textview), FALSE);
   gtk_widget_set_can_focus(textview, FALSE);
   /* Make the description text view non-editable and non-focusable.
    * The user complained that the text was selectable/copyable like a
    * text entry. GtkTextView defaults to editable=False but is still
    * focusable and selectable. We disable cursor and focus so it
    * behaves like a static label. */
   gtk_text_view_set_editable(GTK_TEXT_VIEW(textview), FALSE);
   gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(textview), FALSE);
   gtk_widget_set_can_focus(textview, FALSE);
   buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
   if (buf == NULL) {
      g_warning("RGPkgDetailsWindow::fillInValues: text_descr has no GtkTextBuffer");
      return;
   }
   // clear old buffer
   gtk_text_buffer_get_start_iter(buf, &start);
   gtk_text_buffer_get_end_iter(buf, &end);
   gtk_text_buffer_delete(buf, &start, &end);
   // create bold tag
   GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buf);
   if (gtk_text_tag_table_lookup(tag_table, "bold") == NULL) {
      gtk_text_buffer_create_tag(
         buf, "bold", "weight", PANGO_WEIGHT_BOLD, "scale", 1.1, NULL);
   }
   // set name (title) bold
   gtk_text_buffer_get_start_iter(buf, &it);
   s = utf8(pkg->name());
   gtk_text_buffer_insert(buf, &it, s, -1);
   gtk_text_buffer_get_start_iter(buf, &start);
   gtk_text_buffer_get_end_iter(buf, &end);
   gtk_text_buffer_apply_tag_by_name(buf, "bold", &start, &end);

   gtk_text_buffer_get_end_iter(buf, &it);
   gtk_text_buffer_insert(buf, &it, "\n", 1);

   // set emblems
   GdkPixbuf *supported = RGPackageStatus::pkgStatus.getSupportedPix(pkg);
   if (supported != NULL) {
      gtk_text_buffer_insert(buf, &it, " ", 1);
      emblem = gtk_image_new_from_pixbuf(supported);
      GtkWidget *event = gtk_event_box_new();
      gtk_container_add(GTK_CONTAINER(event), emblem);
      gtk_widget_set_tooltip_text(event, _("Supported by the distribution"));
      GtkTextChildAnchor *anchor = gtk_text_buffer_create_child_anchor(buf, &it);
      gtk_text_view_add_child_at_anchor(GTK_TEXT_VIEW(textview), event, anchor);
      gtk_widget_show_all(event);
   }


   // add homepage button with border (same style as the old changelog button)
   if (strlen(pkg->homepage())) {
      gtk_text_buffer_insert(buf, &it, "    ", 1);
      GtkTextChildAnchor *anchor = gtk_text_buffer_create_child_anchor(buf, &it);
      GtkWidget *button = gtk_button_new_with_label(_("Visitar sitio web"));
      gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NORMAL);
      g_object_set_data(G_OBJECT(button), "me", me);
      g_object_set_data(G_OBJECT(button), "pkg", pkg);
      g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(cbOpenHomepage), pkg);
      gtk_text_view_add_child_at_anchor(GTK_TEXT_VIEW(textview), button, anchor);
      gtk_widget_show(button);
   }

#ifdef HAVE_XBPS
   {
      /* Display package information in the iruka-xbps style:
       * a clean key:value list of all the fields the user actually
       * cares about, instead of a raw xbps-src template file. The
       * raw template was hard to read and most fields were empty.
       *
       * Fields are only printed when they have a non-empty value so
       * the panel stays compact. */
      std::ostringstream ss;

      ss << "\n" << _("--- Package Details ---") << "\n\n";
      ss << _("Name:")         << "   " << (pkg->name() ? pkg->name() : "")              << "\n";
      /* Only show "Installed Version:" if the package is actually
       * installed. For not-installed packages this field is empty
       * and showing "Installed Version: (empty)" looks weird. */
      if (pkg->installedVersion() && *pkg->installedVersion() &&
          (pkg->getFlags() & RPackage::FInstalled))
         ss << _("Installed Version:") << "   " << pkg->installedVersion()               << "\n";
      if (pkg->availableVersion() && *pkg->availableVersion())
         ss << _("Available Version:") << "   " << pkg->availableVersion()               << "\n";
/* Summary removed — redundant with description */
      if (pkg->description() && *pkg->description())
         ss << _("Description:")  << "   " << pkg->description()                          << "\n";
      if (pkg->origin() != "")
         ss << _("Repository:")   << "   " << pkg->origin()                               << "\n";
      if (pkg->homepage() && *pkg->homepage())
         ss << _("Homepage:")     << "   " << pkg->homepage()                             << "\n";
      std::string license = pkg->findTagFromPkgRecord("license");
      if (!license.empty())
         ss << _("License:")      << "   " << license                                     << "\n";
      if (pkg->maintainer() && *pkg->maintainer())
         ss << _("Maintainer:")   << "   " << pkg->maintainer()                           << "\n";
      std::string arch_s = pkg->arch();
      if (!arch_s.empty())
         ss << _("Architecture:") << "   " << arch_s                                       << "\n";
      if (pkg->installedSize() > 0) {
         char buf2[64];
         double sz = (double)pkg->installedSize();
         if (sz >= 1073741824.0)
            snprintf(buf2, sizeof buf2, "%.2f GiB", sz / 1073741824.0);
         else if (sz >= 1048576.0)
            snprintf(buf2, sizeof buf2, "%.2f MiB", sz / 1048576.0);
         else if (sz >= 1024.0)
            snprintf(buf2, sizeof buf2, "%.2f KiB", sz / 1024.0);
         else
            snprintf(buf2, sizeof buf2, "%.0f B", sz);
         ss << _("Installed Size:") << "   " << buf2                                      << "\n";
      }
      std::string build_date = pkg->findTagFromPkgRecord("build-date");
      if (!build_date.empty())
         ss << _("Build Date:")   << "   " << build_date                                  << "\n";
      std::string install_date = pkg->findTagFromPkgRecord("install-date");
      if (!install_date.empty())
         ss << _("Install Date:") << "   " << install_date                                << "\n";
      if (pkg->srcPackage() && *pkg->srcPackage())
         ss << _("Source Package:") << "   " << pkg->srcPackage()                        << "\n";

      /* Dependencies, conflicts, provides — each on its own line. */
      std::vector<DepInformation> deps_all = pkg->enumDeps();
      if (!deps_all.empty()) {
         std::string dep_str;
         for (size_t i = 0; i < deps_all.size(); i++) {
            if (i) dep_str += ", ";
            dep_str += deps_all[i].name ? deps_all[i].name : "";
            if (deps_all[i].versionComp && *deps_all[i].versionComp) {
               dep_str += deps_all[i].versionComp;
               dep_str += deps_all[i].version ? deps_all[i].version : "";
            }
         }
         ss << "\n" << _("Depends On:") << "   " << dep_str                               << "\n";
      }
      std::vector<std::string> prov = pkg->provides();
      if (!prov.empty()) {
         std::string prov_str;
         for (size_t i = 0; i < prov.size(); i++) {
            if (i) prov_str += ", ";
            prov_str += prov[i];
         }
         ss << _("Provides:")     << "   " << prov_str                                    << "\n";
      }
      std::string conflicts = pkg->findTagFromPkgRecord("conflicts");
      if (!conflicts.empty())
         ss << _("Conflicts:")    << "   " << conflicts                                   << "\n";
      std::string replaces = pkg->findTagFromPkgRecord("replaces");
      if (!replaces.empty())
         ss << _("Replaces:")     << "   " << replaces                                    << "\n";
/* Changelog field removed */

      gtk_text_buffer_get_end_iter(buf, &it);
      gtk_text_buffer_insert(buf, &it, ss.str().c_str(), -1);
   }
#endif


   // build dependency lists
   vector<DepInformation> deps;
   deps = pkg->enumDeps();
   me->setTreeList("treeview_deplist", formatDepInformation(deps), true);

   // canidateVersion
   deps = pkg->enumDeps(true);
   me->setTreeList("treeview_availdep_list", formatDepInformation(deps), true);

   // rdepends Version
   deps = pkg->enumRDeps();
   me->setTreeList("treeview_rdeps", formatDepInformation(deps), true);

   // provides
   me->setTreeList("treeview_provides", pkg->provides());

#ifndef HAVE_RPM
   // file list
   gtk_widget_show(GTK_WIDGET(
      gtk_builder_get_object(me->getGtkBuilder(), "scrolledwindow_filelist")));
   me->setTextView("textview_files", pkg->installedFiles().c_str());
#endif

   // versions
   gchar *str;
   vector<string> list;
   vector<pair<string, string>> versions = pkg->getAvailableVersions();
   for (int i = 0; i < versions.size(); i++) {
      // TRANSLATORS: this the format of the available versions in
      // the "Properties/Available versions" window
      // e.g. "0.56 (unstable)"
      //      "0.53.4 (testing)"
      str = g_strdup_printf(
         _("%s (%s)"), versions[i].first.c_str(), versions[i].second.c_str());
      list.push_back(str);
      g_free(str);
   }
   me->setTreeList("treeview_versions", list);

   /*
   g_signal_connect(gtk_builder_get_object
                    (me->getGtkBuilder(), "combobox_depends"),
                    "changed",
                    G_CALLBACK(cbDependsMenuChanged), me);
   */
}

void RGPkgDetailsWindow::cbDependsMenuChanged(GtkWidget *self, void *data)
{
   RGPkgDetailsWindow *me = (RGPkgDetailsWindow *)data;

   int nr = gtk_combo_box_get_active(GTK_COMBO_BOX(self));
   GtkWidget *notebook =
      GTK_WIDGET(gtk_builder_get_object(me->_builder, "notebook_dep_tab"));
   assert(notebook);
   gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), nr);
}

RGPkgDetailsWindow::~RGPkgDetailsWindow()
{}
