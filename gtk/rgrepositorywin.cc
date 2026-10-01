/* rgrepositorywin.cc - gtk editor for the xbps.d repository configuration
 *
 * Copyright (c) (c) 1999 Patrick Cole <z@amused.net>
 *               (c) 2002 Synaptic development team
 *
 * Author: Patrick Cole <z@amused.net>
 *         Michael Vogt <mvo@debian.org>
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

#include "rgrepositorywin.h"

#include "i18n.h"
#include "rggtkbuilderwindow.h"
#include "rguserdialog.h"
#include "rgutils.h"
#include "ruserdialog.h"

#include <apt-pkg/error.h>
#include <apt-pkg/sourcelist.h>
#include <cassert>
#include <cstddef>
#include <gdk/gdk.h>
#include <glib-object.h>
#include <glib.h>
#include <glib/gtypes.h>
#include <gobject/gclosure.h>
#include <gtk/gtk.h>
#include <string>

class RGWindow;

using namespace std;

#if HAVE_RPM
enum {
   ITEM_TYPE_RPM,
   ITEM_TYPE_RPMSRC,
   ITEM_TYPE_RPMDIR,
   ITEM_TYPE_RPMSRCDIR,
   ITEM_TYPE_REPOMD,
   ITEM_TYPE_REPOMDSRC,
   ITEM_TYPE_DEB,
   ITEM_TYPE_DEBSRC
};
#elif defined(HAVE_XBPS)
// Under HAVE_XBPS the repository editor only deals with XBPS binary
// repositories. There is no source / debug / cdrom / repomd distinction
// at the UI level (xbps.d files are a flat list of repository URLs).
enum {
   ITEM_TYPE_XBPS = 1
};
#else
enum {
   ITEM_TYPE_DEB,
   ITEM_TYPE_DEBSRC,
   ITEM_TYPE_RPM,
   ITEM_TYPE_RPMSRC,
   ITEM_TYPE_RPMDIR,
   ITEM_TYPE_RPMSRCDIR,
   ITEM_TYPE_REPOMD,
   ITEM_TYPE_REPOMDSRC
};
#endif

enum {
   STATUS_COLUMN,
   TYPE_COLUMN,
   VENDOR_COLUMN,
   URI_COLUMN,
   DISTRIBUTION_COLUMN,
   SECTIONS_COLUMN,
   RECORD_COLUMN,
   DISABLED_COLOR_COLUMN,
   N_SOURCES_COLUMNS
};

enum {
   COL_NAME,
   COL_TYPE,
};

// Shown under the edit fields for the selected source
static string SourceHint(const SourcesList::SourceRecord *rec)
{
#ifdef HAVE_XBPS
   // Under HAVE_XBPS the APT SourcesList::SourceRecord is not used;
   // SelectionChanged builds the hint inline from the URL stored in
   // the tree model. SourceHint is kept for the APT path only.
   (void)rec;
   return "";
#else
   if (rec == NULL)
      return "";
   gchar *text = g_strdup_printf(_("Defined in %s."), rec->SourceFile.c_str());
   string hint = text;
   g_free(text);
   // deb822 is an APT-only sources.list format; XBPS uses xbps.d
   // which is always the simple one-URL-per-line format.
   if (rec->Format == SourcesList::Deb822) {
      hint += " ";
      hint += _("This source uses the deb822 format and can not be edited "
                "here yet.");
   }
   return hint;
#endif
}

void RGRepositoryEditor::item_toggled(GtkCellRendererToggle *cell,
                                      gchar *path_str,
                                      gpointer data)
{
   RGRepositoryEditor *me =
      (RGRepositoryEditor *)g_object_get_data(G_OBJECT(cell), "me");
   GtkTreeModel *model = (GtkTreeModel *)data;
   GtkTreePath *path = gtk_tree_path_new_from_string(path_str);
   GtkTreeIter iter;
   gboolean toggle_item;

   /* get toggled iter */
   gtk_tree_model_get_iter(model, &iter, path);

#ifdef HAVE_XBPS
   gtk_tree_model_get(model, &iter, STATUS_COLUMN, &toggle_item, -1);

   /* The URL is the only thing that identifies a repository line
    * under the XBPS backend; flip its enabled state via the
    * SourcesListXbps adapter and update the row in place. */
   gchar *url = NULL;
   gtk_tree_model_get(model, &iter, URI_COLUMN, &url, -1);
   if (url != NULL && url[0] != '\0') {
      toggle_item ^= 1;
      me->_lstXbps.ToggleSource(url, toggle_item == TRUE);
      gtk_list_store_set(GTK_LIST_STORE(model),
                         &iter,
                         STATUS_COLUMN,
                         toggle_item,
                         DISABLED_COLOR_COLUMN,
                         toggle_item ? nullptr : &me->_gray,
                         -1);
      me->_dirty = true;
   }
   g_free(url);
   gtk_tree_path_free(path);
#else
   gchar *section = NULL;
   gtk_tree_model_get(
      model, &iter, STATUS_COLUMN, &toggle_item, SECTIONS_COLUMN, &section, -1);

   SourcesList::SourceRecord *rec;
   gtk_tree_model_get(model, &iter, RECORD_COLUMN, &rec, -1);
   // deb822 is APT-only; under HAVE_XBPS xbps.d stanzas are always the
   // simple one-URL-per-line format and are always editable.
   if (rec->Format == SourcesList::Deb822) {
      // read-only for now, see SourcesList::FileFormat
      g_free(section);
      gtk_tree_path_free(path);
      return;
   }

   /* do something with the value */
   toggle_item ^= 1;

   // special warty check
   if (toggle_item && section && g_strrstr(section, "universe")) {
      gchar *msg = _("You are adding the \"universe\" component.\n\n "
                     "Packages in this component are not supported. "
                     "Are you sure?");
      if (!me->_userDialog->message(
             msg, RUserDialog::DialogWarning, RUserDialog::ButtonsOkCancel)) {
         gtk_tree_path_free(path);
         g_free(section);
         return;
      }
   }

   /* set new value */
   gtk_list_store_set(
      GTK_LIST_STORE(model), &iter, STATUS_COLUMN, toggle_item, -1);

   me->_dirty = true;

   /* clean up */
   g_free(section);
   gtk_tree_path_free(path);
#endif
}


RGRepositoryEditor::RGRepositoryEditor(RGWindow *parent)
   : RGGtkBuilderWindow(parent, "repositories"), _dirty(false)
{
   // cout << "RGRepositoryEditor::RGRepositoryEditor(RGWindow *parent)"<<endl;
   assert(_win);
   _userDialog = new RGUserDialog(_win);
   _applied = false;
   _lastIter = NULL;

   setTitle(_("Repositories"));
   gtk_window_set_modal(GTK_WINDOW(_win), TRUE);
   // gtk_window_set_policy(GTK_WINDOW(_win), TRUE, TRUE, FALSE);

   // build gtktreeview
   _sourcesListStore = gtk_list_store_new(N_SOURCES_COLUMNS,
                                          G_TYPE_BOOLEAN,
                                          G_TYPE_STRING,
                                          G_TYPE_STRING,
                                          G_TYPE_STRING,
                                          G_TYPE_STRING,
                                          G_TYPE_STRING,
                                          G_TYPE_POINTER,
                                          GDK_TYPE_RGBA);

   _sourcesListView =
      GTK_WIDGET(gtk_builder_get_object(_builder, "treeview_repositories"));
   gtk_tree_view_set_model(GTK_TREE_VIEW(_sourcesListView),
                           GTK_TREE_MODEL(_sourcesListStore));

   GtkCellRenderer *renderer;
   GtkTreeViewColumn *column;

   // status
   renderer = gtk_cell_renderer_toggle_new();
   g_object_set_data(G_OBJECT(renderer), "me", this);
   column = gtk_tree_view_column_new_with_attributes(
      _("Enabled"), renderer, "active", STATUS_COLUMN, NULL);
   g_signal_connect(renderer,
                    "toggled",
                    G_CALLBACK(item_toggled),
                    GTK_TREE_MODEL(_sourcesListStore));
   gtk_tree_view_append_column(GTK_TREE_VIEW(_sourcesListView), column);

   // type
   renderer = gtk_cell_renderer_text_new();
   column = gtk_tree_view_column_new_with_attributes(_("Type"),
                                                     renderer,
                                                     "text",
                                                     TYPE_COLUMN,
                                                     "foreground-rgba",
                                                     DISABLED_COLOR_COLUMN,
                                                     NULL);
   gtk_tree_view_append_column(GTK_TREE_VIEW(_sourcesListView), column);
#ifdef HAVE_XBPS
   // Under HAVE_XBPS xbps.d only knows about `repository=` lines; the
   // Type column ("deb" / "deb-src" / "rpm" / ...) doesn't apply.
   gtk_tree_view_column_set_visible(column, FALSE);
#endif

   // vendor
   renderer = gtk_cell_renderer_text_new();
   column = gtk_tree_view_column_new_with_attributes(_("Vendor"),
                                                     renderer,
                                                     "text",
                                                     VENDOR_COLUMN,
                                                     "foreground-rgba",
                                                     DISABLED_COLOR_COLUMN,
                                                     NULL);
   gtk_tree_view_append_column(GTK_TREE_VIEW(_sourcesListView), column);
#if !defined(HAVE_RPM) || defined(HAVE_XBPS)
   // The Vendor column is APT/RPM-only. Under HAVE_XBPS the xbps.d
   // repository= lines have no vendor concept.
   gtk_tree_view_column_set_visible(column, FALSE);
#endif

   // uri
   renderer = gtk_cell_renderer_text_new();
   column = gtk_tree_view_column_new_with_attributes(
#ifdef HAVE_XBPS
      _("Repository URL"),
#else
      _("URI"),
#endif
      renderer,
      "text",
      URI_COLUMN,
      "foreground-rgba",
      DISABLED_COLOR_COLUMN,
      NULL);
   gtk_tree_view_append_column(GTK_TREE_VIEW(_sourcesListView), column);

   // distribution
   renderer = gtk_cell_renderer_text_new();
   column = gtk_tree_view_column_new_with_attributes(_("Distribution"),
                                                     renderer,
                                                     "text",
                                                     DISTRIBUTION_COLUMN,
                                                     "foreground-rgba",
                                                     DISABLED_COLOR_COLUMN,
                                                     NULL);
   gtk_tree_view_append_column(GTK_TREE_VIEW(_sourcesListView), column);
#ifdef HAVE_XBPS
   // xbps.d repository= lines have no Distribution concept.
   gtk_tree_view_column_set_visible(column, FALSE);
#endif

   // section(s)
   renderer = gtk_cell_renderer_text_new();
   column = gtk_tree_view_column_new_with_attributes(_("Section(s)"),
                                                     renderer,
                                                     "text",
                                                     SECTIONS_COLUMN,
                                                     "foreground-rgba",
                                                     DISABLED_COLOR_COLUMN,
                                                     NULL);
   gtk_tree_view_append_column(GTK_TREE_VIEW(_sourcesListView), column);
#ifdef HAVE_XBPS
   // xbps.d repository= lines have no Sections concept.
   gtk_tree_view_column_set_visible(column, FALSE);
#endif

   GtkTreeSelection *select;
   select = gtk_tree_view_get_selection(GTK_TREE_VIEW(_sourcesListView));
   gtk_tree_selection_set_mode(select, GTK_SELECTION_SINGLE);
   g_signal_connect(
      G_OBJECT(select), "changed", G_CALLBACK(SelectionChanged), this);

   //_cbEnabled = GTK_WIDGET(gtk_builder_get_object(_builder,
   //"checkbutton_enabled"));

   _optType = GTK_WIDGET(gtk_builder_get_object(_builder, "combo_type"));

   renderer = gtk_cell_renderer_text_new();
   gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(_optType), renderer, TRUE);
   gtk_cell_layout_set_attributes(
      GTK_CELL_LAYOUT(_optType), renderer, "text", COL_NAME, NULL);
   _optTypeMenu = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);

   GtkTreeIter item;
#if HAVE_RPM
   gtk_list_store_append(_optTypeMenu, &item);
   gtk_list_store_set(
      _optTypeMenu, &item, COL_NAME, "rpm", COL_TYPE, ITEM_TYPE_RPM, -1);

   gtk_list_store_append(_optTypeMenu, &item);
   gtk_list_store_set(
      _optTypeMenu, &item, COL_NAME, "rpm-src", COL_TYPE, ITEM_TYPE_RPMSRC, -1);

   gtk_list_store_append(_optTypeMenu, &item);
   gtk_list_store_set(
      _optTypeMenu, &item, COL_NAME, "rpm-dir", COL_TYPE, ITEM_TYPE_RPMDIR, -1);

   gtk_list_store_append(_optTypeMenu, &item);
   gtk_list_store_set(_optTypeMenu,
                      &item,
                      COL_NAME,
                      "rpm-src-dir",
                      COL_TYPE,
                      ITEM_TYPE_RPMSRCDIR,
                      -1);

   gtk_list_store_append(_optTypeMenu, &item);
   gtk_list_store_set(
      _optTypeMenu, &item, COL_NAME, "repomd", COL_TYPE, ITEM_TYPE_REPOMD, -1);

   gtk_list_store_append(_optTypeMenu, &item);
   gtk_list_store_set(_optTypeMenu,
                      &item,
                      COL_NAME,
                      "repomd-src",
                      COL_TYPE,
                      ITEM_TYPE_REPOMDSRC,
                      -1);
#elif defined(HAVE_XBPS)
   // Under HAVE_XBPS xbps.d files contain a flat list of repository URLs
   // (one per line, optionally commented with '#'). There is no source /
   // binary distinction at the UI level so we only expose a single entry.
   gtk_list_store_append(_optTypeMenu, &item);
   gtk_list_store_set(_optTypeMenu,
                      &item,
                      COL_NAME,
                      _("Binary (xbps)"),
                      COL_TYPE,
                      ITEM_TYPE_XBPS,
                      -1);
#else
   gtk_list_store_append(_optTypeMenu, &item);
   gtk_list_store_set(_optTypeMenu,
                      &item,
                      COL_NAME,
                      _("Binary (deb)"),
                      COL_TYPE,
                      ITEM_TYPE_DEB,
                      -1);

   gtk_list_store_append(_optTypeMenu, &item);
   gtk_list_store_set(_optTypeMenu,
                      &item,
                      COL_NAME,
                      _("Source (deb-src)"),
                      COL_TYPE,
                      ITEM_TYPE_DEBSRC,
                      -1);
#endif
   gtk_combo_box_set_model(GTK_COMBO_BOX(_optType),
                           GTK_TREE_MODEL(_optTypeMenu));


   // the (rpm) vendor menu
   _optVendor = GTK_WIDGET(gtk_builder_get_object(_builder, "combo_vendor"));
   renderer = gtk_cell_renderer_text_new();
   gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(_optVendor), renderer, TRUE);
   gtk_cell_layout_set_attributes(
      GTK_CELL_LAYOUT(_optVendor), renderer, "text", 0, NULL);
   _optVendorMenu = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);

   gtk_list_store_append(_optVendorMenu, &item);
   gtk_list_store_set(_optVendorMenu, &item, 0, _("(no vendor)"), 1, "", -1);

   gtk_combo_box_set_model(GTK_COMBO_BOX(_optVendor),
                           GTK_TREE_MODEL(_optVendorMenu));

#if !defined(HAVE_RPM) || defined(HAVE_XBPS)
   // debian can't use the vendors menu, so we hide it; under HAVE_XBPS
   // there is no vendor concept either.
   gtk_widget_hide(GTK_WIDGET(_optVendor));
   GtkWidget *vendors =
      GTK_WIDGET(gtk_builder_get_object(_builder, "button_edit_vendors"));
   assert(vendors);
   gtk_widget_hide(GTK_WIDGET(vendors));
#endif

   _entryURI = GTK_WIDGET(gtk_builder_get_object(_builder, "entry_uri"));
   assert(_entryURI);
#ifdef HAVE_XBPS
   /* Under HAVE_XBPS the URI entry must always be editable so the
    * user can type a new repository URL without first selecting an
    * existing row. The entry_distribution and entry_sections are
    * APT-only and hidden. */
   gtk_editable_set_editable(GTK_EDITABLE(_entryURI), TRUE);
   gtk_widget_set_sensitive(_entryURI, TRUE);
#endif
   /* Distribution and Sections entries removed from the UI.
    * Set to NULL so any code that references them is a no-op. */
   _entryDist = NULL;
   _entrySect = NULL;

   g_signal_connect(GTK_WIDGET(gtk_builder_get_object(_builder, "button_ok")),
                    "clicked",
                    G_CALLBACK(DoOK),
                    this);

   g_signal_connect(
      GTK_WIDGET(gtk_builder_get_object(_builder, "button_cancel")),
      "clicked",
      G_CALLBACK(DoCancel),
      this);

   g_signal_connect(
      GTK_WIDGET(gtk_builder_get_object(_builder, "button_edit_vendors")),
      "clicked",
      G_CALLBACK(VendorsWindow),
      this);

   /*
      g_signal_connect(GTK_WIDGET(gtk_builder_get_object
      (_builder, "button_clear")),
      "clicked",
      G_CALLBACK(DoClear),
      this);
    */

   g_signal_connect(GTK_WIDGET(gtk_builder_get_object(_builder, "button_add")),
                    "clicked",
                    G_CALLBACK(DoAdd),
                    this);

   _upBut = GTK_WIDGET(gtk_builder_get_object(_builder, "button_up"));
   assert(_upBut);
   g_object_set_data(G_OBJECT(_upBut), "up", GINT_TO_POINTER(1));
   g_signal_connect(_upBut, "clicked", G_CALLBACK(DoUpDown), this);
   gtk_widget_set_sensitive(_upBut, FALSE);

   _downBut = GTK_WIDGET(gtk_builder_get_object(_builder, "button_down"));
   assert(_downBut);
   g_object_set_data(G_OBJECT(_downBut), "down", GINT_TO_POINTER(0));
   g_signal_connect(_downBut, "clicked", G_CALLBACK(DoUpDown), this);
   gtk_widget_set_sensitive(_downBut, FALSE);

   _deleteBut = GTK_WIDGET(gtk_builder_get_object(_builder, "button_remove"));
   assert(_deleteBut);
   g_signal_connect(_deleteBut, "clicked", G_CALLBACK(DoRemove), this);
   gtk_widget_set_sensitive(_deleteBut, FALSE);

   _editTable = GTK_WIDGET(gtk_builder_get_object(_builder, "table_edit"));
   assert(_editTable);
#ifdef HAVE_XBPS
   /* Under HAVE_XBPS the edit table (which contains the URI entry)
    * must stay enabled at all times so the user can type a new
    * repository URL without selecting an existing row first. The
    * Remove/Up/Down buttons still need a selection. */
   gtk_widget_set_sensitive(_editTable, TRUE);
#else
   gtk_widget_set_sensitive(_editTable, FALSE);
#endif
   _hintLabel =
      GTK_WIDGET(gtk_builder_get_object(_builder, "label_source_hint"));
   assert(_hintLabel);

#ifdef HAVE_XBPS
   // Under HAVE_XBPS the repository editor only manages a flat list
   // of `repository=` URLs. Hide the widgets that only make sense for
   // the APT source-record model (Type combo, Vendor combo, Distribution
   // entry, Sections entry, and the up/down ordering buttons — the
   // order of `repository=` lines in /etc/xbps.d/*.conf doesn't matter
   // per xbps.d(5)). The URI entry stays visible because the user
   // types a new URL there before clicking "Add".
   gtk_widget_hide(GTK_WIDGET(_optType));
   gtk_widget_hide(GTK_WIDGET(_entryDist));
   gtk_widget_hide(GTK_WIDGET(_entrySect));
   gtk_widget_hide(_upBut);
   gtk_widget_hide(_downBut);
#endif

   gtk_window_resize(GTK_WINDOW(_win), 620, 400);
   skipTaskbar(true);
   show();
}


RGRepositoryEditor::~RGRepositoryEditor()
{
   // gtk_widget_destroy(_win);
   delete _userDialog;
}


bool RGRepositoryEditor::Run()
{
#ifdef HAVE_XBPS
   if (_lstXbps.ReadSources() == false) {
      _userDialog->warning(
         _("Ignoring invalid record(s) in xbps.d!"));
   }
   // keep a backup of the original list so DoOK can roll back if needed
   _savedListXbps.ReadSources();

   GtkTreeIter iter;
   for (const SourcesListXbps::SourceRecord &rec : _lstXbps.getRecords()) {
      // Skip blank lines and unknown key=value lines (e.g.
      // `cachedir=...`, `architecture=...`) — they're preserved on
      // disk by UpdateSources() but don't show up as rows.
      if (rec.type == SourcesListXbps::TYPE_EMPTY)
         continue;
      if (rec.type == SourcesListXbps::TYPE_UNKNOWN)
         continue;
      // Plain comments (no embedded `repository=`) are skipped.
      if (rec.type == SourcesListXbps::TYPE_COMMENT && rec.url.empty())
         continue;
      // TYPE_XBPS (enabled) and commented-out repositories (disabled)
      // both appear as rows; the toggle column lets the user flip
      // between them.

      gtk_list_store_append(_sourcesListStore, &iter);
      gtk_list_store_set(_sourcesListStore,
                         &iter,
                         STATUS_COLUMN,
                         rec.enabled ? TRUE : FALSE,
                         TYPE_COLUMN,
                         "xbps",
                         VENDOR_COLUMN,
                         "",
                         URI_COLUMN,
                         utf8(rec.url.c_str()),
                         DISTRIBUTION_COLUMN,
                         "",
                         SECTIONS_COLUMN,
                         "",
                         RECORD_COLUMN,
                         (gpointer) nullptr,
                         DISABLED_COLOR_COLUMN,
                         rec.enabled ? nullptr : &_gray,
                         -1);
   }

   gtk_main();
   return _applied;
#else
   if (_lst.ReadSources() == false) {
      _userDialog->warning(
         _("Ignoring invalid record(s) in xbps.d!"));
      // return false;
   }
   // keep a backup of the orginal list
   _savedList.ReadSources();

   if (_lst.ReadVendors() == false) {
      _error->Error(_("Cannot read vendors.list file"));
      _userDialog->showErrors();
      return false;
   }

   GtkTreeIter iter;
   for (SourcesListIter it = _lst.SourceRecords.begin();
        it != _lst.SourceRecords.end();
        it++) {
      if ((*it)->Type & SourcesList::Comment)
         continue;
      string Sections;
      for (unsigned int J = 0; J < (*it)->NumSections; J++) {
         Sections += (*it)->Sections[J];
         Sections += " ";
      }

      gtk_list_store_append(_sourcesListStore, &iter);
      gtk_list_store_set(_sourcesListStore,
                         &iter,
                         STATUS_COLUMN,
                         !((*it)->Type & SourcesList::Disabled),
                         TYPE_COLUMN,
                         utf8((*it)->TypeLabel().c_str()),
                         VENDOR_COLUMN,
                         utf8((*it)->VendorID.c_str()),
                         URI_COLUMN,
                         utf8((*it)->URI.c_str()),
                         DISTRIBUTION_COLUMN,
                         utf8((*it)->Dist.c_str()),
                         SECTIONS_COLUMN,
                         utf8(Sections.c_str()),
                         RECORD_COLUMN,
                         (gpointer)(*it),
                         DISABLED_COLOR_COLUMN,
                         (*it)->Type & SourcesList::Disabled ? &_gray : NULL,
                         -1);
   }


   UpdateVendorMenu();

   gtk_main();
   return _applied;
#endif
}

void RGRepositoryEditor::UpdateVendorMenu()
{
#ifndef HAVE_XBPS
   GtkTreeIter item;
   gtk_list_store_clear(_optVendorMenu);

   gtk_list_store_append(_optVendorMenu, &item);
   gtk_list_store_set(_optVendorMenu, &item, 0, _("(no vendor)"), 1, "", -1);

   for (VendorsListIter it = _lst.VendorRecords.begin();
        it != _lst.VendorRecords.end();
        it++) {
      gtk_list_store_append(_optVendorMenu, &item);
      gtk_list_store_set(_optVendorMenu,
                         &item,
                         0,
                         (*it)->VendorID.c_str(),
                         1,
                         (*it)->VendorID.c_str(),
                         -1);
   }
#endif
}

int RGRepositoryEditor::VendorMenuIndex(string VendorID)
{
#ifndef HAVE_XBPS
   int index = 0;
   for (VendorsListIter it = _lst.VendorRecords.begin();
        it != _lst.VendorRecords.end();
        it++) {
      index += 1;
      if ((*it)->VendorID == VendorID)
         return index;
   }
   return 0;
#else
   // Under HAVE_XBPS there is no vendor concept; the combo is hidden
   // and this helper is never called from a visible code path. We
   // still provide the stub so the symbol exists for the linker in
   // case any APT-only string-table lookup falls through.
   (void)VendorID;
   return 0;
#endif
}

void RGRepositoryEditor::DoClear(GtkWidget *, gpointer data)
{
   RGRepositoryEditor *me = (RGRepositoryEditor *)data;
   // cout << "RGRepositoryEditor::DoClear(GtkWidget *, gpointer data)"<<endl;

   gtk_combo_box_set_active(GTK_COMBO_BOX(me->_optType), 0);
   gtk_combo_box_set_active(GTK_COMBO_BOX(me->_optVendor), 0);
   gtk_entry_set_text(GTK_ENTRY(me->_entryURI), "");
   gtk_entry_set_text(GTK_ENTRY(me->_entryDist), "");
   gtk_entry_set_text(GTK_ENTRY(me->_entrySect), "");
}

void RGRepositoryEditor::DoAdd(GtkWidget *, gpointer data)
{
   RGRepositoryEditor *me = (RGRepositoryEditor *)data;
#ifdef HAVE_XBPS
   // Read the URL the user typed into the URI entry and append it as
   // a new `repository=<url>` line in /etc/xbps.d/99-synaptic.conf via
   // SourcesListXbps::AddSource. AddSource re-reads the on-disk files
   // so we rebuild the list store from getRecords() afterwards.
   const gchar *url = gtk_entry_get_text(GTK_ENTRY(me->_entryURI));
   if (url == NULL || url[0] == '\0') {
      me->_userDialog->warning(_("The repository URL cannot be empty."));
      return;
   }

   /* Reject duplicate URLs so the user doesn't accidentally add the
    * same repo twice. */
   for (const SourcesListXbps::SourceRecord &rec :
        me->_lstXbps.getRecords()) {
      if (rec.url == url) {
         me->_userDialog->warning(
            _("This repository is already in the list."));
         return;
      }
   }

   if (!me->_lstXbps.AddSource(url)) {
      me->_userDialog->error(
         _("Failed to add repository. Check that /etc/xbps.d exists "
           "and is writable."));
      return;
   }

   /* Verify the repo actually landed on disk by re-reading the
    * records and checking the URL is present. If AddSource said OK
    * but the URL is not in getRecords(), something is wrong. */
   bool verified = false;
   for (const SourcesListXbps::SourceRecord &rec :
        me->_lstXbps.getRecords()) {
      if (rec.url == url) {
         verified = true;
         break;
      }
   }
   if (!verified) {
      me->_userDialog->error(
         _("Repository was not saved. Check that /etc/xbps.d is "
           "writable and the URL is valid."));
      return;
   }

   gtk_list_store_clear(me->_sourcesListStore);
   GtkTreeIter iter;
   for (const SourcesListXbps::SourceRecord &rec :
        me->_lstXbps.getRecords()) {
      if (rec.type == SourcesListXbps::TYPE_EMPTY)
         continue;
      if (rec.type == SourcesListXbps::TYPE_UNKNOWN)
         continue;
      if (rec.type == SourcesListXbps::TYPE_COMMENT && rec.url.empty())
         continue;
      gtk_list_store_append(me->_sourcesListStore, &iter);
      gtk_list_store_set(me->_sourcesListStore,
                         &iter,
                         STATUS_COLUMN,
                         rec.enabled ? TRUE : FALSE,
                         TYPE_COLUMN,
                         "xbps",
                         VENDOR_COLUMN,
                         "",
                         URI_COLUMN,
                         utf8(rec.url.c_str()),
                         DISTRIBUTION_COLUMN,
                         "",
                         SECTIONS_COLUMN,
                         "",
                         RECORD_COLUMN,
                         (gpointer) nullptr,
                         DISABLED_COLOR_COLUMN,
                         rec.enabled ? nullptr : &me->_gray,
                         -1);
   }

   gtk_entry_set_text(GTK_ENTRY(me->_entryURI), "");
   me->_dirty = true;
#else
   // cout << "RGRepositoryEditor::DoAdd(GtkWidget *, gpointer data)"<<endl;

   SourcesList::SourceRecord *rec = me->_lst.AddEmptySource();

   GtkTreeIter iter;
   gtk_list_store_append(me->_sourcesListStore, &iter);
   gtk_list_store_set(me->_sourcesListStore,
                      &iter,
                      STATUS_COLUMN,
                      1,
                      TYPE_COLUMN,
                      "",
                      VENDOR_COLUMN,
                      "",
                      URI_COLUMN,
                      "",
                      DISTRIBUTION_COLUMN,
                      "",
                      SECTIONS_COLUMN,
                      "",
                      RECORD_COLUMN,
                      (gpointer)(rec),
                      DISABLED_COLOR_COLUMN,
                      NULL,
                      -1);

   GtkTreeSelection *selection;
   selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(me->_sourcesListView));
   gtk_tree_selection_unselect_all(selection);
   gtk_tree_selection_select_iter(selection, &iter);

   GtkTreePath *path;
   path = gtk_tree_model_get_path(GTK_TREE_MODEL(me->_sourcesListStore), &iter);
   gtk_tree_view_set_cursor(
      GTK_TREE_VIEW(me->_sourcesListView), path, NULL, false);
   gtk_tree_view_scroll_to_cell(
      GTK_TREE_VIEW(me->_sourcesListView), path, NULL, TRUE, 0.0, 0.0);
   gtk_tree_path_free(path);

   me->_dirty = true;
#endif
}

void RGRepositoryEditor::doEdit()
{
   // cout << "RGRepositoryEditor::doEdit()"<<endl;
#ifdef HAVE_XBPS
   // Under HAVE_XBPS the only editable field is the URL of the
   // selected repository. The UI flow is: user types a new URL in
   // the URI entry and clicks "Add" (which calls AddSource); the
   // "Remove" button calls RemoveSource on the currently-selected
   // row; the checkbox toggles enabled/disabled in place via
   // ToggleSource. In-place URL editing (typing a different URL for
   // an existing row) is intentionally not implemented here — it
   // would require a SourcesListXbps::setUrl() helper that the spec
   // doesn't list, and the user can polish it later. This stub
   // keeps SelectionChanged's doEdit() call safe (no-op).
   (void)0;
#else
   // GtkTreeSelection *selection;
   // selection = gtk_tree_view_get_selection(GTK_TREE_VIEW (_sourcesListView));
   if (_lastIter == NULL) {
      // cout << "deadbeef"<<endl;
      return;
   }

   GtkTreeModel *model = GTK_TREE_MODEL(_sourcesListStore);
   // if (!gtk_tree_selection_get_selected (selection, &model, &iter))
   // return;

   SourcesList::SourceRecord *rec;
   gtk_tree_model_get(model, _lastIter, RECORD_COLUMN, &rec, -1);
   assert(rec);
   // deb822 is APT-only; under HAVE_XBPS xbps.d stanzas are always the
   // simple one-URL-per-line format and are always editable.
   if (rec->Format == SourcesList::Deb822)
      return;

   rec->Type = 0;
   gboolean status;
   gtk_tree_model_get(
      GTK_TREE_MODEL(_sourcesListStore), _lastIter, STATUS_COLUMN, &status, -1);
   if (!status)
      rec->Type |= SourcesList::Disabled;

   GtkTreeIter item;
   int type;
   gtk_combo_box_get_active_iter(GTK_COMBO_BOX(_optType), &item);
   gtk_tree_model_get(GTK_TREE_MODEL(_optTypeMenu), &item, 1, &type, -1);

   switch (type) {
      case ITEM_TYPE_DEB:
         rec->Type |= SourcesList::Deb;
         break;
      case ITEM_TYPE_DEBSRC:
         rec->Type |= SourcesList::DebSrc;
         break;
      case ITEM_TYPE_RPM:
         rec->Type |= SourcesList::Rpm;
         break;
      case ITEM_TYPE_RPMSRC:
         rec->Type |= SourcesList::RpmSrc;
         break;
      case ITEM_TYPE_RPMDIR:
         rec->Type |= SourcesList::RpmDir;
         break;
      case ITEM_TYPE_RPMSRCDIR:
         rec->Type |= SourcesList::RpmSrcDir;
         break;
      case ITEM_TYPE_REPOMD:
         rec->Type |= SourcesList::Repomd;
         break;
      case ITEM_TYPE_REPOMDSRC:
         rec->Type |= SourcesList::RepomdSrc;
         break;
      default:
         _userDialog->error(_("Unknown source type"));
         return;
   }

#if 0 // PORTME, no vendor id support right now
   gtk_combo_box_get_active_iter(GTK_COMBO_BOX(_optVendor), &item);
   gtk_tree_model_get(GTK_TREE_MODEL(_optVendorMenu), &item,
                      1, &type,
                      -1);
   rec->VendorID = type;
#endif

   rec->URI = gtk_entry_get_text(GTK_ENTRY(_entryURI));
   rec->Dist = gtk_entry_get_text(GTK_ENTRY(_entryDist));

   delete[] rec->Sections;
   rec->NumSections = 0;

   const char *Section = gtk_entry_get_text(GTK_ENTRY(_entrySect));
   if (Section != 0 && Section[0] != 0)
      rec->NumSections++;

   rec->Sections = new string[rec->NumSections];
   rec->NumSections = 0;
   Section = gtk_entry_get_text(GTK_ENTRY(_entrySect));

   if (Section != 0 && Section[0] != 0)
      rec->Sections[rec->NumSections++] = Section;

   string Sect;
   for (unsigned int I = 0; I < rec->NumSections; I++) {
      Sect += rec->Sections[I];
      Sect += " ";
   }

   /* repaint screen */
   gtk_list_store_set(_sourcesListStore,
                      _lastIter,
                      STATUS_COLUMN,
                      !(rec->Type & SourcesList::Disabled),
                      TYPE_COLUMN,
                      utf8(rec->TypeLabel().c_str()),
                      VENDOR_COLUMN,
                      utf8(rec->VendorID.c_str()),
                      URI_COLUMN,
                      utf8(rec->URI.c_str()),
                      DISTRIBUTION_COLUMN,
                      utf8(rec->Dist.c_str()),
                      SECTIONS_COLUMN,
                      utf8(Sect.c_str()),
                      RECORD_COLUMN,
                      (gpointer)(rec),
                      DISABLED_COLOR_COLUMN,
                      (rec->Type & SourcesList::Disabled ? &_gray : NULL),
                      -1);
#endif
}

void RGRepositoryEditor::DoRemove(GtkWidget *, gpointer data)
{
   RGRepositoryEditor *me = (RGRepositoryEditor *)data;
#ifdef HAVE_XBPS
   // Find the URL of the selected row and tell SourcesListXbps to
   // remove every record that matches it (this also clears any
   // matching `#repository=...` commented-out line so a future
   // AddSource(url) doesn't resurrect a stale disabled entry).
   GtkTreeSelection *selection;
   GtkTreeIter iter;
   selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(me->_sourcesListView));

   if (gtk_tree_selection_get_selected(
          selection, (GtkTreeModel **)(&me->_sourcesListStore), &iter)) {
      gchar *url = NULL;
      gtk_tree_model_get(
         GTK_TREE_MODEL(me->_sourcesListStore), &iter, URI_COLUMN, &url, -1);
      if (url != NULL && url[0] != '\0')
         me->_lstXbps.RemoveSource(url);
      g_free(url);

      if (me->_lastIter != NULL)
         gtk_tree_iter_free(me->_lastIter);
      me->_lastIter = NULL;
      gtk_list_store_remove(me->_sourcesListStore, &iter);
   }
   me->_dirty = true;
#else
   // cout << "RGRepositoryEditor::DoRemove(GtkWidget *, gpointer data)"<<endl;

   GtkTreeSelection *selection;
   GtkTreeIter iter;
   selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(me->_sourcesListView));

   if (gtk_tree_selection_get_selected(
          selection, (GtkTreeModel **)(&me->_sourcesListStore), &iter)) {
      SourcesList::SourceRecord *rec;
      gtk_tree_model_get(
         GTK_TREE_MODEL(me->_sourcesListStore), &iter, RECORD_COLUMN, &rec, -1);
      assert(rec);

      me->_lst.RemoveSource(rec);
      if (me->_lastIter != NULL)
         gtk_tree_iter_free(me->_lastIter);
      me->_lastIter = NULL;
      gtk_list_store_remove(me->_sourcesListStore, &iter);
   }
   me->_dirty = true;
#endif
}

void RGRepositoryEditor::DoOK(GtkWidget *, gpointer data)
{
   RGRepositoryEditor *me = (RGRepositoryEditor *)data;

   me->doEdit();
#ifdef HAVE_XBPS
   me->_lstXbps.UpdateSources();
#else
   me->_lst.UpdateSources();

   // check if we actually can parse the xbps.d configuration
   // Under HAVE_XBPS we don't have the APT pkgSourceList machinery; the
   // SourcesList class above already validated the records, so we just
   // accept the update here.
   pkgSourceList List;
   if (!List.ReadMainList()) {
      me->_userDialog->showErrors();
      me->_savedList.UpdateSources();
      return;
   }
#endif

   gtk_main_quit();
   me->_applied = me->_dirty;
}

void RGRepositoryEditor::DoCancel(GtkWidget *, gpointer data)
{
   // RGRepositoryEditor *me = (RGRepositoryEditor *)data;
   gtk_main_quit();
}


void RGRepositoryEditor::SelectionChanged(GtkTreeSelection *selection,
                                          gpointer data)
{
   RGRepositoryEditor *me = (RGRepositoryEditor *)data;
   // cout << "RGRepositoryEditor::SelectionChanged()"<<endl;

   GtkTreeIter iter;
   GtkTreeModel *model;
   if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
      me->doEdit(); // save the old row
      if (me->_lastIter != NULL)
         gtk_tree_iter_free(me->_lastIter);
      me->_lastIter = gtk_tree_iter_copy(&iter);

#ifdef HAVE_XBPS
      // Under HAVE_XBPS the URI entry is always free for the user to
      // type a NEW repository URL. Selecting an existing row does NOT
      // copy its URL into the entry — that would be confusing because
      // the user might think they're adding a new repo but actually
      // editing the selected one. Instead, the hint label shows the
      // selected repo's URL for reference.
      gchar *url = NULL;
      gtk_tree_model_get(model, &iter, URI_COLUMN, &url, -1);

      gtk_widget_set_sensitive(me->_editTable, TRUE);
      gtk_widget_set_sensitive(me->_upBut, FALSE);
      gtk_widget_set_sensitive(me->_downBut, FALSE);
      gtk_widget_set_sensitive(me->_deleteBut, TRUE);

      /* Show the selected repo's URL in the hint, but leave the entry
       * empty so the user can type a new URL. */
      gchar *hint = g_strdup_printf(
         _("Seleccionado: %s\n"
           "Escriba una nueva URL arriba y haga clic en «Añadir» para añadir un repositorio.\n"
           "Haga clic en «Eliminar» para borrar el repositorio seleccionado."),
         url ? url : _("(desconocido)"));
      gtk_label_set_text(GTK_LABEL(me->_hintLabel), hint);
      g_free(hint);

      /* Don't overwrite the entry — leave it as-is so the user can
       * type a new URL. If they want to edit the selected repo's URL,
       * they can copy it manually. */
      g_free(url);
#else
      const SourcesList::SourceRecord *rec;
      gtk_tree_model_get(model, &iter, RECORD_COLUMN, &rec, -1);

      // deb822 stanzas are shown but not editable yet, see
      // SourcesList::FileFormat
      const bool editable = rec->Format != SourcesList::Deb822;
      gtk_widget_set_sensitive(me->_editTable, editable);
      gtk_widget_set_sensitive(me->_upBut, editable);
      gtk_widget_set_sensitive(me->_downBut, editable);
      gtk_widget_set_sensitive(me->_deleteBut, editable);
      gtk_label_set_text(GTK_LABEL(me->_hintLabel), SourceHint(rec).c_str());

      int id = ITEM_TYPE_DEB;
      if (rec->Type & SourcesList::DebSrc)
         id = ITEM_TYPE_DEBSRC;
      else if (rec->Type & SourcesList::Rpm)
         id = ITEM_TYPE_RPM;
      else if (rec->Type & SourcesList::RpmSrc)
         id = ITEM_TYPE_RPMSRC;
      else if (rec->Type & SourcesList::RpmDir)
         id = ITEM_TYPE_RPMDIR;
      else if (rec->Type & SourcesList::RpmSrcDir)
         id = ITEM_TYPE_RPMSRCDIR;
      else if (rec->Type & SourcesList::Repomd)
         id = ITEM_TYPE_REPOMD;
      else if (rec->Type & SourcesList::RepomdSrc)
         id = ITEM_TYPE_REPOMDSRC;
      gtk_combo_box_set_active(GTK_COMBO_BOX(me->_optType), id);

      gtk_combo_box_set_active(GTK_COMBO_BOX(me->_optVendor),
                               me->VendorMenuIndex(rec->VendorID));

      gtk_entry_set_text(GTK_ENTRY(me->_entryURI), utf8(rec->URI.c_str()));
      gtk_entry_set_text(GTK_ENTRY(me->_entryDist), utf8(rec->Dist.c_str()));
      gtk_entry_set_text(GTK_ENTRY(me->_entrySect), "");

      for (unsigned int I = 0; I < rec->NumSections; I++) {
         int pos = gtk_editable_get_position(GTK_EDITABLE(me->_entrySect));
         gtk_editable_insert_text(GTK_EDITABLE(me->_entrySect),
                                  utf8(rec->Sections[I].c_str()),
                                  -1,
                                  &pos);
         gtk_editable_insert_text(GTK_EDITABLE(me->_entrySect), " ", -1, &pos);
      }
#endif
   } else {
      /* No row selected — keep the URI entry editable so the user
       * can type a new repository URL without first selecting an
       * existing one. The Add button stays enabled; only Remove
       * and the up/down buttons need a selection. */
      gtk_widget_set_sensitive(me->_editTable, TRUE);
      gtk_entry_set_text(GTK_ENTRY(me->_entryURI), "");
      gtk_label_set_text(GTK_LABEL(me->_hintLabel),
                         _("Escriba una URL de repositorio y haga clic en «Añadir».\n"
                           "Seleccione una fila para editarla o eliminarla."));

      gtk_widget_set_sensitive(me->_upBut, FALSE);
      gtk_widget_set_sensitive(me->_downBut, FALSE);
      gtk_widget_set_sensitive(me->_deleteBut, FALSE);
   }
}

void RGRepositoryEditor::VendorsWindow(GtkWidget *, void *data)
{
#if 0 // PORTME
   RGRepositoryEditor *me = (RGRepositoryEditor *) data;
   RGVendorsEditor w(me, me->_lst);
   w.Run();
   GtkWidget *menuitem = gtk_menu_get_active(GTK_MENU(me->_optVendorMenu));
   string VendorID = (char *)g_object_get_data(G_OBJECT(menuitem), "id");
   me->UpdateVendorMenu();
   gtk_option_menu_set_history(GTK_OPTION_MENU(me->_optVendor),
                               me->VendorMenuIndex(VendorID));
#endif
}

void RGRepositoryEditor::DoUpDown(GtkWidget *self, gpointer data)
{
#ifdef HAVE_XBPS
   // Under HAVE_XBPS the order of `repository=` lines in /etc/xbps.d/*.conf
   // doesn't matter (xbps.d(5) treats them as a set, not a list), so the
   // up/down buttons are hidden in the constructor and this handler is
   // never invoked. We keep the no-op so the signal handler registration
   // in the constructor doesn't reference a missing symbol.
   (void)self;
   (void)data;
#else
   RGRepositoryEditor *me = (RGRepositoryEditor *)data;
   // cout << "RGRepositoryEditor::DoUpDown(GtkWidget *, gpointer data)"<<endl;

   GtkTreeIter iter;
   GtkTreeIter iter2;

   GtkTreeSelection *selection;
   selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(me->_sourcesListView));
   gtk_tree_selection_get_selected(
      selection, (GtkTreeModel **)(&me->_sourcesListStore), &iter);

   GtkTreePath *path;
   path = gtk_tree_model_get_path(GTK_TREE_MODEL(me->_sourcesListStore), &iter);

   // check if it does up or down
   gboolean up = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(self), "up"));
   if (up)
      gtk_tree_path_prev(path);
   else
      gtk_tree_path_next(path);

   // check if we can get a iter at the given path
   if (!gtk_tree_model_get_iter(
          GTK_TREE_MODEL(me->_sourcesListStore), &iter2, path))
      return;

   gtk_list_store_swap(me->_sourcesListStore, &iter, &iter2);
   gtk_tree_path_free(path);

   SourcesList::SourceRecord *rec;
   gtk_tree_model_get(
      GTK_TREE_MODEL(me->_sourcesListStore), &iter, RECORD_COLUMN, &rec, -1);
   assert(rec);
   SourcesList::SourceRecord *rec_p;
   gtk_tree_model_get(
      GTK_TREE_MODEL(me->_sourcesListStore), &iter2, RECORD_COLUMN, &rec_p, -1);
   assert(rec_p);

   // Insert rec before rec_p
   if (up)
      me->_lst.SwapSources(rec_p, rec);
   else
      me->_lst.SwapSources(rec, rec_p);
#endif
}
