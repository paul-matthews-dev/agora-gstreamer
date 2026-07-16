#! /bin/sh

INPUT_DIR=$1;
export AGORA_SDK_DIR=$INPUT_DIR

echo "MAKE"
make 
echo "MAKE INSTALL"
sudo make install
# copy every shared lib the SDK ships (rtc_sdk NEEDs libagora-fdkaac.so and,
# since 4.4.x, libaosl.so; the exact set varies by SDK version)
sudo cp $INPUT_DIR/agora_sdk/*.so /usr/local/lib
sudo cp agorac.h /usr/local/include
sudo cp agoraconfig.h /usr/local/include
sudo ldconfig
