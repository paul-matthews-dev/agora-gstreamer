#ifndef _JITTER_BUFFER_H_
#define _JITTER_BUFFER_H_

#include "agoratype.h"
#include <functional>

using videoOutFn_t=std::function<void (const uint8_t* buffer,
                                       size_t bufferLength,
                                       bool isKeyFrame)>;

using audioOutFn_t=std::function<void (const uint8_t* buffer,
                                       size_t bufferLength)>;

/* A small delay buffer between the plugin and the SDK. With zero delay
   offsets (the default) frames pass straight through to the out functions on
   the caller's thread — no queue, no copy, no threads. A non-zero offset
   queues frames and starts dispatching once roughly offset-ms worth of frames
   are buffered (assumes ~30 ms/video frame and ~10 ms/audio packet). */
class SyncBuffer{
public:
  SyncBuffer(uint16_t videoDelayOffset=0,
             uint16_t audioDelayOffset=0);

  //use this function to add a new video frame to JB
  void addVideo(const uint8_t* buffer,
                size_t length,
                int isKeyFrame,
                uint64_t ts);

  //use this function to add a new audio packet to JB
  void addAudio(const uint8_t* buffer,
                size_t length,
                uint64_t ts);

  //set a video function that will be called by JB when
  //there is a video frame available
  void setVideoOutFn(const videoOutFn_t& fn);

  //set an audio function that will be called by JB when
  //there is an audio packet available
  void setAudioOutFn(const audioOutFn_t& fn);

  void clear();

  //stop dispatching and drop anything still buffered
  void stop();

private:

  WorkQueue_ptr                   _videoBuffer;
  WorkQueue_ptr                   _audioBuffer;

  videoOutFn_t                    _videoOutFn;
  audioOutFn_t                    _audioOutFn;

  uint16_t                        _videoDelayOffset; //in ms
  uint16_t                        _audioDelayOffset; //in ms

  int                             _objId;
};

#endif
