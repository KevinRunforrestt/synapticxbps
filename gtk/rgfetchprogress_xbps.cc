/* rgfetchprogress_xbps.cc - RGFetchProgress for HAVE_XBPS
 *
 * This implements the fetch progress window using the real GTK
 * widgets from window_fetch.ui (progressbar_download, label_description).
 * The window is shown during xbps_rpool_sync (Reload) and during
 * xbps_transaction_commit (Apply).
 */

#include "config.h"

#ifdef HAVE_XBPS

#include "rgfetchprogress.h"
#include "rgutils.h"
#include <gtk/gtk.h>

bool RGFetchProgress::MediaChange(std::string, std::string) { return false; }
void RGFetchProgress::IMSHit(pkgAcquire::ItemDesc &) {}
void RGFetchProgress::Fetch(pkgAcquire::ItemDesc &) {}
void RGFetchProgress::Done(pkgAcquire::ItemDesc &) {}
void RGFetchProgress::Fail(pkgAcquire::ItemDesc &) {}

void RGFetchProgress::Start()
{
   _cancelled = false;
   if (_win)
      gtk_widget_show_all(_win);
}

void RGFetchProgress::Stop()
{
   if (_mainProgressBar)
      gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(_mainProgressBar), 1.0);
   RGFlushInterface();
}

bool RGFetchProgress::Pulse(pkgAcquire *)
{
   if (_cancelled)
      return false;
   if (_mainProgressBar)
      gtk_progress_bar_pulse(GTK_PROGRESS_BAR(_mainProgressBar));
   RGFlushInterface();
   return true;
}

void RGFetchProgress::close()
{
   if (_win)
      gtk_widget_hide(_win);
}

void RGFetchProgress::setDescription(std::string mainText, std::string secondText)
{
   if (_win) {
      GtkWidget *label = GTK_WIDGET(
         gtk_builder_get_object(_builder, "label_description"));
      if (label)
         gtk_label_set_text(GTK_LABEL(label), mainText.c_str());
   }
}

RGFetchProgress::RGFetchProgress(RGWindow *win)
   : RGGtkBuilderWindow(win, "fetch")
   , _table(NULL), _tableListStore(NULL)
   , _mainProgressBar(NULL), _statusColumn(NULL), _statusRenderer(NULL)
   , _cancelled(false), _cursorDirty(false)
{
   _mainProgressBar =
      GTK_WIDGET(gtk_builder_get_object(_builder, "progressbar_download"));
}

void RGFetchProgress::updateStatus(pkgAcquire::ItemDesc &, int) {}
void RGFetchProgress::stopDownload(GtkWidget *, void *) {}
void RGFetchProgress::cursorChanged(GtkTreeView *, gpointer) {}
void RGFetchProgress::expanderActivate(GObject *, GParamSpec *, gpointer) {}
char *RGFetchProgress::getStatusStr(int) { static char b[16]; b[0]=0; return b; }
int RGFetchProgress::getStatusPercent(int) { return 0; }
void RGFetchProgress::refreshTable(int, bool) {}

#endif /* HAVE_XBPS */
