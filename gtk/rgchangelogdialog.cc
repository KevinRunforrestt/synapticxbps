/* rgchangelogdialog.cc - changelog dialog for the XBPS backend
 *
 * This replaces the APT version which used pkgAcquire. Our version
 * fetches the changelog URL via xbps-query -p changelog, then
 * downloads it with curl, strips HTML/markdown, and shows it in a
 * scrollable, non-selectable text view.
 */

#include "config.h"

#include "i18n.h"
#include "rgchangelogdialog.h"
#include "rpackage.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <glib.h>
#include <gtk/gtk.h>
#include <string>

class RGWindow;

using namespace std;

/* Strip HTML tags and return readable text */
static string
strip_html_simple(const string &html)
{
   string s = html;
   const char *blocks[] = {"script", "style", "head", "nav", "footer", "noscript", NULL};
   for (int b = 0; blocks[b]; b++) {
      string open = "<", close = "</"; open += blocks[b]; close += blocks[b]; close += ">";
      size_t pos = 0;
      while ((pos = s.find(open, pos)) != string::npos) {
         size_t end = s.find(close, pos);
         if (end == string::npos) break;
         s.erase(pos, end + close.size() - pos);
      }
   }
   /* Comments */
   { size_t pos = 0;
     while ((pos = s.find("<!--", pos)) != string::npos) {
        size_t end = s.find("-->", pos);
        if (end == string::npos) break;
        s.erase(pos, end + 3 - pos);
     }
   }
   /* Strip tags */
   string out;
   bool in_tag = false;
   for (char c : s) {
      if (c == '<') { in_tag = true; continue; }
      if (c == '>') { in_tag = false; continue; }
      if (!in_tag) out += c;
   }
   /* Decode entities */
   string dec;
   for (size_t i = 0; i < out.size(); i++) {
      if (out[i] == '&') {
         if (out.compare(i, 6, "&amp;") == 0) { dec += '&'; i += 4; continue; }
         if (out.compare(i, 6, "&nbsp;") == 0) { dec += ' '; i += 5; continue; }
         if (out.compare(i, 4, "&lt;") == 0) { dec += '<'; i += 3; continue; }
         if (out.compare(i, 4, "&gt;") == 0) { dec += '>'; i += 3; continue; }
         if (out.compare(i, 6, "&quot;") == 0) { dec += '"'; i += 5; continue; }
      }
      dec += out[i];
   }
   /* Collapse blank lines */
   string col;
   bool last_nl = false;
   for (char c : dec) {
      if (c == '\n') { if (last_nl) continue; last_nl = true; col += '\n'; }
      else { last_nl = false; col += c; }
   }
   return col;
}

/* Simple markdown to text */
static string
render_md_simple(const string &md)
{
   string out;
   string cur;
   vector<string> lines;
   for (char c : md) { if (c == '\n') { lines.push_back(cur); cur.clear(); } else cur += c; }
   if (!cur.empty()) lines.push_back(cur);
   for (auto &line : lines) {
      if (line.find("```") == 0) continue;
      if (!line.empty() && line[0] == '#') {
         size_t l = 0; while (l < line.size() && line[l] == '#') l++;
         string t = line.substr(l);
         while (!t.empty() && t[0] == ' ') t = t.substr(1);
        while (!t.empty() && t.back() == '#') t.pop_back();
         out += t + "\n";
         continue;
      }
      if (!line.empty() && line[0] == '>') { out += "  " + line.substr(1) + "\n"; continue; }
      /* Strip **bold**, *italic*, `code` */
      string stripped;
      for (size_t i = 0; i < line.size(); i++) {
         if (i+1 < line.size() && line[i] == '*' && line[i+1] == '*') { i++; continue; }
         if (line[i] == '*' || line[i] == '`' || line[i] == '_') continue;
         if (line[i] == '[') {
            size_t cb = line.find(']', i);
            if (cb != string::npos && cb+1 < line.size() && line[cb+1] == '(') {
               size_t cp = line.find(')', cb+2);
               if (cp != string::npos) {
                  stripped += line.substr(i+1, cb-i-1);
                  if (cp - cb - 2 > 0) stripped += " (" + line.substr(cb+2, cp-cb-2) + ")";
                  i = cp; continue;
               }
            }
         }
         stripped += line[i];
      }
      out += stripped + "\n";
   }
   return out;
}

void ShowChangelogDialog(RGWindow *me, RPackage *pkg)
{
   (void)me;
   if (!pkg) return;

   string pkgname = pkg->name();
   string cmd = "xbps-query ";
   if (!(pkg->getFlags() & RPackage::FInstalled)) cmd += "-R ";
   cmd += "-p changelog '" + pkgname + "' 2>/dev/null";
   FILE *fp = popen(cmd.c_str(), "r");
   string changelog_url;
   if (fp) {
      char buf[2048];
      if (fgets(buf, sizeof buf, fp)) {
         char *nl = strchr(buf, '\n'); if (nl) *nl = 0;
         changelog_url = buf;
      }
      pclose(fp);
   }

   string content;
   if (changelog_url.empty()) {
      content = _("No hay changelog disponible para este paquete.");
   } else {
      string fetch_cmd = "curl -sL --max-time 15 --max-filesize 524288 '";
      fetch_cmd += changelog_url + "' 2>/dev/null";
      FILE *cfp = popen(fetch_cmd.c_str(), "r");
      if (cfp) {
         char buf[8192];
         while (fgets(buf, sizeof buf, cfp)) content += buf;
         pclose(cfp);
      }
      if (content.empty()) {
         content = _("No se pudo descargar el changelog.\nURL: ");
         content += changelog_url;
      } else {
         bool is_html = (content.find("<html") != string::npos ||
                         content.find("<!DOCTYPE") != string::npos ||
                         content.find("<head") != string::npos);
         bool is_md = false;
         if (!is_html) {
            int m = 0;
            if (content.find("\n# ") != string::npos) m++;
            if (content.find("\n- ") != string::npos) m++;
            if (content.find("```") != string::npos) m++;
            if (content.find("**") != string::npos) m++;
            if (m >= 2) is_md = true;
         }
         if (is_html) {
            content = strip_html_simple(content);
            if (content.size() < 50) {
               content = _("El changelog requiere JavaScript.\nURL: ");
               content += changelog_url;
            }
         } else if (is_md) {
            content = render_md_simple(content);
         }
         if (content.size() > 65536) {
            content.resize(65536);
            content += "\n[... truncado ...]\n";
         }
         string header = _("Registro de cambios de ");
         header += pkgname + ":\n\n";
         content = header + content;
      }
   }

   /* Show in scrollable, non-selectable dialog */
   GtkWidget *dialog = gtk_dialog_new_with_buttons(
      _("Registro de cambios"), NULL,
      (GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
      _("Cerrar"), GTK_RESPONSE_CLOSE, NULL);
   gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 400);

   GtkWidget *ca = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
      GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

   GtkTextBuffer *buf2 = gtk_text_buffer_new(NULL);
   gtk_text_buffer_set_text(buf2, content.c_str(), -1);
   GtkWidget *tv = gtk_text_view_new_with_buffer(buf2);
   gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
   gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(tv), FALSE);
   gtk_widget_set_can_focus(tv, FALSE);
   g_signal_connect(G_OBJECT(tv), "button-press-event", G_CALLBACK(gtk_true), NULL);
   gtk_container_add(GTK_CONTAINER(scroll), tv);
   gtk_box_pack_start(GTK_BOX(ca), scroll, TRUE, TRUE, 0);
   gtk_widget_show_all(scroll);

   gtk_dialog_run(GTK_DIALOG(dialog));
   gtk_widget_destroy(dialog);
}
