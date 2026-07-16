#! /bin/sh
#
# Local / in-place build. Compiles the agoraioudp plugin and its libgstagorac
# backend against the vendored 4.4.32 SDK WITHOUT installing anything.
# Nothing is written to /usr/local, so your currently-installed plugins and
# SDK libs are left untouched. Run the normal build_all_aarch64_4.4.32.sh only
# once you've confirmed this build works and you're ready to install.

BUILD_DIR=$PWD
REPO=$BUILD_DIR/..
SDK=$REPO/agora/sdk/agora_sdk_aarch64_4.4.32

# 1) Build the C++ backend libgstagorac.so in place (make, no `make install`)
echo "building libgstagorac.so (local, not installed) ..."
cd $REPO/agora/libagorac || exit 1
AGORA_SDK_DIR=$SDK make clean 2>/dev/null
AGORA_SDK_DIR=$SDK make || exit 1

# 2) Build the plugin into gst-agora/build_local, linking against the local
#    backend + vendored SDK. No `ninja install`.
echo "building libgstagoraioudp.so (local, not installed) ..."
cd $REPO/gst-agora || exit 1
rm -rf build_local
meson build_local \
  -Dagorac_lib="$REPO/agora/libagorac/libgstagorac.so" \
  -Dagora_sdk_lib="$SDK/agora_sdk/libagora_rtc_sdk.so" || exit 1
ninja -C build_local || exit 1

echo ""
echo "Done - nothing installed. Built artifacts:"
echo "  backend: $REPO/agora/libagorac/libgstagorac.so"
echo "  plugin : $REPO/gst-agora/build_local/plugin-src/libgstagoraioudp.so"
echo ""
echo "Test it without installing:"
echo "  export LD_LIBRARY_PATH=$SDK/agora_sdk:$REPO/agora/libagorac"
echo "  export GST_PLUGIN_PATH=$REPO/gst-agora/build_local/plugin-src"
echo "  gst-inspect-1.0 agoraioudp"
