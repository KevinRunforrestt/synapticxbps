# The XBPS backend adds parallel implementation files
# (rpackage_xbps.cc, rpackagelister_xbps.cc, rpackagecache_xbps.cc,
# rsources_xbps.cc). Each is wrapped in `#ifdef HAVE_XBPS ... #endif`
# so it compiles to an empty translation unit when the APT backend is
# selected (the default).
#
# Under the APT backend, the original .cc files are compiled.
# Under the XBPS backend, the APT-specific .cc files (rpackage.cc,
# rpackagelister.cc, rpackagecache.cc, rcdscanner.cc, rpmindexcopy.cc,
# rinstallprogress.cc, rconfiguration.cc, rsources.cc, raptoptions.cc,
# rcacheactor.cc, rtagcollbuilder.cc, rtagcollfilter.cc) are excluded
# from the build because they `#include <apt-pkg/...>` headers and
# there is no libapt-pkg under the XBPS build. The remaining files
# (rpackagestatus, rpackagefilter, rpackageview, rpackagelistactor,
# ruserdialog, sections_trans) are kept because they have been
# `#ifdef`'d appropriately OR are APT-agnostic in their implementation.

if have_xbps
  glib_dep = dependency('glib-2.0')
  common_sources = files(
    # APT-agnostic utilities only. The filters/views/status classes
    # have header-only implementations for the simple cases; their
    # complex APT-specific .cc bodies are excluded under HAVE_XBPS
    # because they would not compile without libapt-pkg. The UI
    # uses the headers' class declarations which are APT-agnostic
    # in the public API.
    'rpackagelistactor.cc',
    'ruserdialog.cc',
    'sections_trans.cc',
    # --- XBPS backend ---
    'rpackage_xbps.cc',
    'rpackagelister_xbps.cc',
    'rpackagecache_xbps.cc',
    'rsources_xbps.cc',
  )
  # The apt-pkg-stub.cc provides global _config, _error, _system
  # definitions so UI code that references them links cleanly.
  common_sources += files('../apt-pkg-stub/apt-pkg-stub.cc')
else
  common_sources = files(
    'rconfiguration.cc',
    'rinstallprogress.cc',
    'rpackage.cc',
    'rpackagestatus.cc',
    'rpackagecache.cc',
    'rpackagefilter.cc',
    'rpackagelister.cc',
    'rpackageview.cc',
    'rcdscanner.cc',
    'rpmindexcopy.cc',
    'raptoptions.cc',
    'rsources.cc',
    'rcacheactor.cc',
    'rpackagelistactor.cc',
    'rtagcollbuilder.cc',
    'rtagcollfilter.cc',
    'ruserdialog.cc',
    'sections_trans.cc',
    # --- XBPS backend (compiled in only when -Dxbps=enabled) ---
    'rpackage_xbps.cc',
    'rpackagelister_xbps.cc',
    'rpackagecache_xbps.cc',
    'rsources_xbps.cc',
  )
endif

libsynaptic = static_library(
  'synaptic',
  common_sources,
  cpp_args: rpm_compile_args + (have_xbps ? ['-DHAVE_XBPS=1'] : []),
  dependencies: common_deps + (have_xbps ? [glib_dep] : []),
  include_directories: [root_inc, common_inc] + (have_xbps ? [apt_stub_inc] : []),
)

libsynaptic_dep = declare_dependency(
  link_with: libsynaptic,
  include_directories: [root_inc, common_inc] + (have_xbps ? [apt_stub_inc] : []),
)
