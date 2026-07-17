#ifndef _AGORA_IO_H_
#define _AGORA_IO_H_

#include <atomic>
#include <chrono>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "agoratype.h"
#include "helpers/context.h"

#include "IAgoraService.h"
#include "AgoraBase.h"

#include "observer/pcmframeobserver.h"
#include "observer/h264frameobserver.h"
#include "observer/userobserver.h"
#include "observer/connectionobserver.h"

class AgoraIo{

  public:
   AgoraIo(bool verbose,
           event_fn fn,
           void* userData,
           int in_audio_delay,
           int in_video_delay,
           int out_audio_delay,
           int out_video_delay,
           bool sendOnly=false,
           bool enableProxy=false,
           int proxyTimeout=0,
           const std::string& proxyIps="",
           bool receiveVideo=false,
           bool audioPcm=false,
           const std::string& agoraParams="");

   bool  init(char* in_app_id,
              char* in_ch_id,
              char* in_user_id);

   int sendVideo(const uint8_t * buffer,
                 uint64_t len,
                 int is_key_frame,
                 long timestamp);

   int sendAudio(const uint8_t * buffer,
                 uint64_t len,
                 long timestamp);

   void disconnect();

   void setPaused(bool flag);

   //right now we support two params to the event
   void addEvent(const AgoraEventType& eventType,
                  const std::string& userName,
                  long param1=0,
                  long param2=0,
                  long* states=nullptr);

   void setEventFunction(event_fn fn, void* userData);

   void setVideoOutFn(agora_media_out_fn videoOutFn, void* userData);
   void setAudioOutFn(agora_media_out_fn audioOutFn, void* userData);

   void setVideoDimensions(int width, int height, int fps);

protected:

  bool initAgoraService(const std::string& appid);

  bool doConnect(char* in_app_id,
                 char* in_channel_id,
                 char* in_user_id);

  bool checkConnection();

  bool doSendHighVideo(const uint8_t* buffer,
                       uint64_t len,
                       int is_key_frame);

  bool doSendAudio(const uint8_t* buffer,  uint64_t len);

   //receiver events
   void subscribeToVideoUser(const std::string& userId);

   void receiveVideoFrame(uint userId,
                          const uint8_t* buffer,
                          size_t length,
                          int isKeyFrame,
                          uint64_t ts);

   void receiveAudioFrame(uint userId,
                          const uint8_t* buffer,
                          size_t length,
                          uint64_t ts);

   void handleUserStateChange(const std::string& userId,
                              const UserState& newState);

    void subscribeAudioUser(const std::string& userId);
    void unsubscribeAudioUser(const std::string& userId);

    void unsubscribeAllVideo();

    void publishUnpublishThreadFn();

    //PCM mode: drains the ring buffer in exactly-10ms frames (SDK requirement)
    void audioPacerThreadFn();

    void handleLocalVad(int vad, int volume);

    void startPublishAudio();
    void startPublishVideo();

    void stopPublishAudio();
    void stopPublishVideo();

    void showFps();

    std::list<std::string> parseIpList();

    std::string createProxyString(const std::list<std::string>& ipList);

 private:

    bool                                          _verbose;

    std::list<std::string>                         _activeUsers;
    std::string                                    _currentVideoUser;

    agora::base::IAgoraService*                     _service;
    agora::agora_refptr<agora::rtc::IRtcConnection> _connection;
    agora::rtc::RtcConnectionConfiguration          _rtcConfig;

    std::shared_ptr<H264FrameReceiver>   h264FrameReceiver;

    PcmFrameObserver_ptr                 _pcmFrameObserver;
    ConnectionObserver_ptr               _connectionObserver;
    UserObserver_ptr                     _userObserver;

    agora::agora_refptr<agora::rtc::IMediaNodeFactory> _factory;

    agora::agora_refptr<agora::rtc::ILocalAudioTrack> _customAudioTrack;
    agora::agora_refptr<agora::rtc::ILocalVideoTrack> _customVideoTrack;

    agora::agora_refptr<agora::rtc::IVideoEncodedImageSender> _videoFrameSender;
    agora::agora_refptr<agora::rtc::IAudioEncodedFrameSender>  _audioSender;
    agora::agora_refptr<agora::rtc::IAudioPcmDataSender>       _pcmSender;

    TimePoint                                       _lastVideoUserSwitchTime;

    std::atomic<bool>                               _isRunning;
    std::atomic<bool>                               _isPaused;

    event_fn                                         _eventfn;
    void*                                            _userEventData;

    //from the app to agora sdk
    SyncBuffer_ptr                                   _outSyncBuffer;
    SyncBuffer_ptr                                   _inSyncBuffer;

    int                                              _in_audio_delay;
    int                                              _in_video_delay;

    int                                              _out_audio_delay;
    int                                              _out_video_delay;

    agora_media_out_fn                                _videoOutFn;
    void*                                             _videoOutUserData;

    agora_media_out_fn                                _audioOutFn;
    void*                                             _audioOutUserData;

    TimePoint                                         _lastTimeAudioReceived;
    TimePoint                                         _lastTimeVideoReceived;

    std::thread                                       _publishUnpublishCheckThread;

    std::atomic<bool>                                 _isPublishingAudio;
    std::atomic<bool>                                 _isPublishingVideo;

    int                                               _videoOutFps;
    int                                               _videoInFps;
    TimePoint                                         _lastFpsPrintTime;

    bool                                              _sendOnly;

    bool                                              _enableProxy;
    int                                               _proxyConnectionTimeOut;
    std::string                                       _proxyIps;

    //encoded video dimensions/fps from the pipeline caps (for EncodedVideoFrameInfo)
    std::atomic<int>                                  _videoWidth;
    std::atomic<int>                                  _videoHeight;
    std::atomic<int>                                  _videoFps;

    //subscribe to remote video (off for publish-only deployments)
    bool                                              _receiveVideo;

    //raw-PCM uplink with SDK 3A/AEC (vs pre-encoded pass-through)
    bool                                              _audioPcm;
    std::string                                       _agoraParams;

    //PCM ring buffer feeding the 10ms pacer thread
    std::mutex                                        _pcmBufMutex;
    std::vector<uint8_t>                              _pcmBuffer;
    std::thread                                       _audioPacerThread;

    std::atomic<int>                                  _lastVadState;

 };

#endif
