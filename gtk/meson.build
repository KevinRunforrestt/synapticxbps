# Under HAVE_XBPS we drop APT/dpkg-only source files from the gtk list:
#   - rgdebinstallprogress.cc  (gated on WITH_DPKG_STATUSFD, dpkg-only)
#   - rgterminstallprogress.cc (gated on HAVE_TERMINAL, dpkg-only)
#   - rgpkgcdrom.cc             (gated on HAVE_APTPKG_CDROM, APT-only)
#   - rgcdscanner.cc            (gated on !HAVE_APTPKG_CDROM, APT-only;
#                                 pulls in <apt-pkg/configuration.h>)
#   - rgfetchprogress.cc        (subclasses pkgAcquireStatus; APT-only)
#   - rgchangelogdialog.cc      (uses pkgAcqChangelog; APT-only)
# and add:
#   - rgxbpsinstallprogress.cc  (no-op install progress stub)
#   - rgfetchprogress_xbps.cc   (no-op fetch progress stub using the
#                                inert RFetchStatus base under XBPS)
#   - xbps_stubs.cc             (vtable stubs for the APT-only classes
#                                the GUI still references: RPackageStatus,
#                                RPackageView* subclasses, RCacheActor,
#                                RAPTOptions, RInstallProgress, etc.)
# The actual XBPS install-progress logic lives in
# common/rpackagelister_xbps.cc::commitChanges, which drives the libxbps
# state_cb and fetch_cb directly.
if have_xbps
  gtk_sources = files(
    'gsynaptic.cc',
    'gtkpkglist.cc',
    'rgcacheprogress.cc',
    'rgpreferenceswindow.cc',
    'rgfetchprogress_xbps.cc',
    'rgfiltermanager.cc',
    'rgfindwindow.cc',
    'rgiconlegend.cc',
    'rglogview.cc',
    'rgutils.cc',
    'rgpackagestatus.cc',
    'rgsetoptwindow.cc',
    'rgdummyinstallprogress.cc',
    'rginstallprogress.cc',
    'rgxbpsinstallprogress.cc',
    'rgslideshow.cc',
    'rgtaskswin.cc',
    'rgmainwindow.cc',
    'rgpkgdetails.cc',
    'rgrepositorywin.cc',
    'rgsummarywindow.cc',
    'rgchangeswindow.cc',
    'rguserdialog.cc',
    'rgwindow.cc',
    'rggtkbuilderwindow.cc',
    'rgpkgtreeview.cc',
    'xbps_stubs.cc',
    'rgchangelogdialog.cc',
  )
else
  gtk_sources = files(
    'gsynaptic.cc',
    'gtkpkglist.cc',
    'rgcacheprogress.cc',
    'rgpkgcdrom.cc',
    'rgpreferenceswindow.cc',
    'rgfetchprogress.cc',
    'rgfiltermanager.cc',
    'rgfindwindow.cc',
    'rgiconlegend.cc',
    'rglogview.cc',
    'rgutils.cc',
    'rgpackagestatus.cc',
    'rgsetoptwindow.cc',
    'rgdummyinstallprogress.cc',
    'rgdebinstallprogress.cc',
    'rgterminstallprogress.cc',
    'rginstallprogress.cc',
    'rgslideshow.cc',
    'rgtaskswin.cc',
    'rgcdscanner.cc',
    'rgmainwindow.cc',
    'rgpkgdetails.cc',
    'rgrepositorywin.cc',
    'rgsummarywindow.cc',
    'rgchangelogdialog.cc',
    'rgchangeswindow.cc',
    'rguserdialog.cc',
    'rgwindow.cc',
    'rggtkbuilderwindow.cc',
    'rgpkgtreeview.cc',
  )
endif

resources = gnome.compile_resources(
  'resources',
  'gtkbuilder/synaptic.gresource.xml',
  source_dir: '.'
)

# Compile the embedded Spanish .mo as a GResource so translations
# are baked into the binary (no external file needed).
locale_resources = gnome.compile_resources(
  'locale_resources',
  '../po/synaptic-es.gresource.xml',
  source_dir: '../po'
)

executable(
  'synaptic',
  gtk_sources + resources + locale_resources,
  install: true,
  install_dir: bindir,
  cpp_args: gtk_cpp_args + rpm_compile_args,
  dependencies: gui_deps,
  include_directories: [root_inc, common_inc, gtk_inc] +
                       (have_xbps ? [apt_stub_inc] : []),
  link_with: libsynaptic,
)
