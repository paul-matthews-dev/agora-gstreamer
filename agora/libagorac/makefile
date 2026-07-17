CXX      = g++
CXXFLAGS = -std=c++17 -O2 -g -Wall -Wextra -Wno-unused-parameter -fPIC \
           -I${AGORA_SDK_DIR}/agora_sdk/include
LDFLAGS  = -shared
LDLIBS   = -ldl

TARGET_LIB = libgstagorac.so
DESTDIR ?= /usr/local/lib

SRCS = agorac.cpp \
       agoraio.cpp \
       agoraevent.cpp \
       syncbuffer.cpp \
       helpers/agoralog.cpp \
       helpers/utilities.cpp \
       observer/pcmframeobserver.cpp \
       observer/h264frameobserver.cpp \
       observer/connectionobserver.cpp \
       observer/userobserver.cpp

OBJS = $(SRCS:.cpp=.o)

all: $(TARGET_LIB)

$(TARGET_LIB): $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET_LIB) $(OBJS)

install:
	install -m 644 $(TARGET_LIB) $(DESTDIR)

.PHONY: all clean install
