plugin_c_args = ['-DHAVE_CONFIG_H -DBOOB']

cdata = configuration_data()
cdata.set_quoted('PACKAGE_VERSION', gst_version)
cdata.set_quoted('PACKAGE', 'gst-agora')
cdata.set_quoted('GST_LICENSE', 'LGPL')
cdata.set_quoted('GST_API_VERSION', api_version)
cdata.set_quoted('GST_PACKAGE_NAME', 'agora source and sink pluging')
cdata.set_quoted('GST_PACKAGE_ORIGIN', 'https://www.agora.io')
configure_file(output : 'config.h', configuration : cdata)


# This fork builds only the agoraioudp element. The agorasink / agorasrc /
# agoraio targets were removed; restore them from git history if needed.

x264 = dependency('x264')
swscale = dependency('libswscale')
opus = dependency('opus')
av= dependency('libavcodec')
gst_base_dep = dependency('gstreamer-base-1.0')
gst_app_dep = dependency('gstreamer-app-1.0')

# Libraries the plugin links against. Default to the system-installed copies;
# a local build passes -Dagorac_lib=... -Dagora_sdk_lib=... to link the
# freshly-built backend + vendored SDK instead (see build/build_local_*.sh).
agorac_lib = get_option('agorac_lib')
agora_sdk_lib = get_option('agora_sdk_lib')

# agoraioudp
agoraioudp_sources = [
  'agoraioudp/gstagoraioudp.c',
  ]

gstagorioudp = library('gstagoraioudp',
  agoraioudp_sources,
  c_args: plugin_c_args,
  dependencies : [gst_dep,x264, swscale, opus,av, gst_base_dep, gst_app_dep],
  install : true,
  install_dir : plugins_install_dir,
  # Link the prebuilt backend + SDK after the plugin objects and force-keep them:
  # the plugin references agoraio_* from libgstagorac.so, so --as-needed (default)
  # would otherwise drop it and leave those symbols undefined.
  link_args: [
        '-Wl,--no-as-needed',
        agorac_lib,
        agora_sdk_lib,
    ]
)
