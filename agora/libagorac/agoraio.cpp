#include "agoraio.h"

#include <dlfcn.h>
#include <syslog.h>
#include <cstdarg>
#include <cstring>
#include <sstream>
#include <string>

//agora header files
#include "NGIAgoraRtcConnection.h"

#include "helpers/agoralog.h"
#include "helpers/context.h"
#include "helpers/utilities.h"

#include "syncbuffer.h"


AgoraIo::AgoraIo(bool verbose,
                 event_fn fn,
                 void* userData,
                 int in_audio_delay,
                 int in_video_delay,
                 int out_audio_delay,
                 int out_video_delay,
                 bool sendOnly,
                 bool enableProxy,
                 int proxyTimeout,
                 const std::string& proxyIps,
                 bool receiveVideo):
 _verbose(verbose),
 _currentVideoUser(""),
 _service(nullptr),
 _lastVideoUserSwitchTime(Now()),
 _isRunning(false),
 _isPaused(false),
 _eventfn(fn),
 _userEventData(userData),
 _outSyncBuffer(nullptr),
 _inSyncBuffer(nullptr),
 _in_audio_delay(in_audio_delay),
 _in_video_delay(in_video_delay),
 _out_audio_delay(out_audio_delay),
 _out_video_delay(out_video_delay),
 _videoOutFn(nullptr),
 _videoOutUserData(nullptr),
 _audioOutFn(nullptr),
 _audioOutUserData(nullptr),
 _lastTimeAudioReceived(Now()),
 _lastTimeVideoReceived(Now()),
 _isPublishingAudio(false),
 _isPublishingVideo(false),
 _videoOutFps(0),
 _videoInFps(0),
 _lastFpsPrintTime(Now()),
 _sendOnly(sendOnly),
 _enableProxy(enableProxy),
 _proxyConnectionTimeOut((proxyTimeout < 1 ) ? 10000 : proxyTimeout),
 _proxyIps(proxyIps),
 _videoWidth(0),
 _videoHeight(0),
 _videoFps(0),
 _receiveVideo(receiveVideo)
{
   _activeUsers.clear();
}

// libaosl spams syslog with a harmless "Java VM not set ..." warning on
// non-Android platforms, and it is emitted unconditionally (the log-level knob
// does not gate it). aosl ships no header, so at runtime we replace its logging
// sink (aosl_set_vlog_func) with a no-op, which silences all libaosl syslog
// output. The no-op ignores its arguments, so it is safe regardless of the exact
// callback signature. The Agora RTC SDK's own log (~/.agora/agorasdk.log) is
// separate and unaffected.
static void aoslNoopVlog(int /*level*/, const char* /*fmt*/, va_list /*ap*/) {}

static void quietAoslLogging()
{
    void* h = dlopen("libaosl.so", RTLD_LAZY);
    if(h){
        typedef void (*aosl_vlog_func_t)(int, const char*, va_list);
        typedef void (*aosl_set_vlog_func_fn)(aosl_vlog_func_t);
        auto setVlog = (aosl_set_vlog_func_fn)dlsym(h, "aosl_set_vlog_func");
        if(setVlog) setVlog(&aoslNoopVlog);
    }

    // The vlog hook alone is not enough: the SDK swaps the sink back to the
    // built-in vsyslog default while the service is released, and the SDK
    // threads exiting at that point each log "AOSL: Java VM not set, so do
    // nothing for thread detach" at EMERGENCY priority — which journald
    // broadcasts (wall) to every logged-in terminal. Mask emerg/alert for
    // this process so libc drops those before they reach syslogd; nothing in
    // a media pipeline legitimately logs at these priorities.
    setlogmask(LOG_UPTO(LOG_DEBUG) & ~(LOG_MASK(LOG_EMERG) | LOG_MASK(LOG_ALERT)));
}

bool AgoraIo::initAgoraService(const std::string& appid)
{
    quietAoslLogging();

    _service = createAgoraService();
    if (!_service)
    {
        logMessage("Error init Agora SDK");
        return false;
    }

    int32_t buildNum = 0;
    getAgoraSdkVersion(&buildNum);
    logInfo("Agora SDK version: "+std::to_string(buildNum));

    agora::base::AgoraServiceConfiguration scfg;
    scfg.appId = appid.c_str();
    scfg.enableAudioProcessor = true;
    scfg.enableAudioDevice = false;
    scfg.enableVideo = true;
    // Restrict Agora's real-time network to "global except mainland China".
    // 0xFFFFFFFE; applied at service init so it governs every connection.
    scfg.areaCode = agora::rtc::AREA_CODE_OVS;


    if (_service->initialize(scfg) != agora::ERR_OK)
    {
        logMessage("Error initialize Agora SDK");
        return false;
    }

    if(verifyLicense() != 0) {
      return false;
    }

    return true;
}

bool AgoraIo::doConnect(char* in_app_id,
                        char* in_channel_id,
                        char* in_user_id){

	 _connection->connect(in_app_id, in_channel_id, in_user_id);
	return true;
}

bool AgoraIo::checkConnection(){

     auto connectionInfo=_connection->getConnectionInfo();
     if(connectionInfo.state==agora::rtc::CONNECTION_STATE_CONNECTED){
         return true;
     }

    if(_connectionObserver!=nullptr){
         _connectionObserver->waitUntilConnected(_proxyConnectionTimeOut);
    }
    else{
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    connectionInfo=_connection->getConnectionInfo();
    if(connectionInfo.state==agora::rtc::CONNECTION_STATE_CONNECTED){
         return true;
     }
     return false;
}

bool  AgoraIo::init(char* in_app_id,
                    char* in_ch_id,
                    char* in_user_id){

    if(!initAgoraService(in_app_id)){
        return false;
    }

    _rtcConfig.clientRoleType = agora::rtc::CLIENT_ROLE_BROADCASTER;
    _rtcConfig.channelProfile = agora::CHANNEL_PROFILE_LIVE_BROADCASTING;
    _rtcConfig.autoSubscribeAudio = false;
    _rtcConfig.autoSubscribeVideo = false;
    _rtcConfig.enableAudioRecordingOrPlayout = false;

    _connection = _service->createRtcConnection(_rtcConfig);
    if (!_connection)
    {
        logMessage("Error creating connection to Agora SDK");
        return false;
    }

    _factory = _service->createMediaNodeFactory();
    if (!_factory)
    {
        logMessage("Error creating factory");
        return false;
    }

   _userObserver=std::make_shared<UserObserver>(_connection->getLocalUser(),_verbose);

    //register audio observer
    _pcmFrameObserver = std::make_shared<PcmFrameObserver>();
    if (_sendOnly==false && _connection->getLocalUser()->setPlaybackAudioFrameParameters(1, 48000) != 0) {
        logMessage("Agora: Failed to set audio frame parameters!");
        return false;
    }

    if (_sendOnly==false && _connection->getLocalUser()->setPlaybackAudioFrameBeforeMixingParameters(1, 48000) != 0) {
        logMessage("Agora: Failed to set audio frame parameters!");
        return false;
    }

   _connectionObserver = std::make_shared<ConnectionObserver>(this);
   _connection->registerObserver(_connectionObserver.get());
   _connection->registerNetworkObserver(_connectionObserver.get());

    if(_sendOnly==false){
        _connection->getLocalUser()->registerAudioFrameObserver(_pcmFrameObserver.get());
    }

    logInfo(std::string("connecting to: ")+in_ch_id+", proxy timeout "+std::to_string(_proxyConnectionTimeOut));
    doConnect(in_app_id, in_ch_id, in_user_id);
    if (!checkConnection() && _enableProxy) {
	_connection->disconnect();
        agora::base::IAgoraParameter* agoraParameter = _connection->getAgoraParameter();
        auto ipList=parseIpList();
        if (ipList.size() > 1) {
            auto ipListString=createProxyString(ipList);
            logMessage("Set proxy IPs "+std::to_string(ipList.size()));
            agoraParameter->setParameters(ipListString.c_str());
        } else {
            logMessage("Enable proxy with default access IPs");
            agoraParameter->setBool("rtc.enable_proxy", true);
        }
       	doConnect(in_app_id, in_ch_id, in_user_id);
    }

    if (checkConnection()==false){

       logInfo("Error connecting to channel");
       return false;
    }

    _videoFrameSender=_factory->createVideoEncodedImageSender();
    if (!_videoFrameSender) {
       logInfo("Failed to create video frame sender!");
       return false;
    }

    //if you want to send_dual_h264,the ccMode must be enabled
#if SDK_BUILD_NUM >=200931 //sdk >=3.8
    agora::rtc::SenderOptions option;
#else
     agora::base::SenderOptions option;
#endif
#if SDK_BUILD_NUM >=190534 //sdk >=3.7
     option.ccMode = agora::rtc::TCcMode::CC_ENABLED;
#else
    option.ccMode = agora::base::CC_ENABLED;
#endif
    // Create video track
    _customVideoTrack=_service->createCustomVideoTrack(_videoFrameSender, option);
    if (!_customVideoTrack) {
         logInfo("Failed to create video track!");
         return false;
     }

     //audio
    // Create audio data sender
     _audioSender = _factory->createAudioEncodedFrameSender();
     if (!_audioSender) {
        return false;
      }

     // Create audio track
     _customAudioTrack =_service->createCustomAudioTrack(_audioSender, agora::base::MIX_DISABLED);
     if (!_customAudioTrack) {
        return false;
     }

    // Publish  video and audio tracks
    startPublishAudio();
    startPublishVideo();

    if(_sendOnly==false){

        //remote video: only subscribed/received when explicitly requested
        if(_receiveVideo){
            h264FrameReceiver = std::make_shared<H264FrameReceiver>();
            _userObserver->setVideoEncodedImageReceiver(h264FrameReceiver.get());

            h264FrameReceiver->setOnVideoFrameReceivedFn([this](const uint userId,
                                                        const uint8_t* buffer,
                                                        const size_t& length,
                                                        const int& isKeyFrame,
                                                        const uint64_t& ts){

                receiveVideoFrame(userId, buffer, length, isKeyFrame, ts);

            });

            //switch the subscribed video to the loudest speaker
            _pcmFrameObserver->setOnUserSpeakingFn([this](const std::string& userId, const int& volume){

                (void)volume;

                //no switching is needed if current user is already shown
                if(userId==_currentVideoUser){
                   return;
                }

                //no switching if last switch time was less than 3 second
                auto diffTime=GetTimeDiff(_lastVideoUserSwitchTime, Now());
                if(diffTime<3000){
                    return;
                }

                if(_currentVideoUser!=""){
                    _connection->getLocalUser()->unsubscribeVideo(_currentVideoUser.c_str());
                }
                subscribeToVideoUser(userId);
                _lastVideoUserSwitchTime=Now();
            });
        }

        //audio
        _pcmFrameObserver->setOnAudioFrameReceivedFn([this](const uint userId,
                                                        const uint8_t* buffer,
                                                        const size_t& length,
                                                        const uint64_t& ts){

             receiveAudioFrame(userId, buffer, length,ts);
        });

        //connection observer: handles user join and leave
        _connectionObserver->setOnUserStateChanged([this](const std::string& userId,
                                                      const UserState& newState){

                handleUserStateChange(userId, newState);

        });
    }

    _userObserver->setOnUserInfofn([this](const std::string& userId, const int& messsage, const int& value){
        if(messsage==1 && value==1){
           handleUserStateChange(userId, USER_CAM_OFF);

           addEvent(AGORA_EVENT_ON_USER_STATE_CHANED,userId,USER_STATE_CAM_OFF,0);
        }
        else if(messsage==1 && value==0){
            handleUserStateChange(userId, USER_CAM_ON);
            addEvent(AGORA_EVENT_ON_USER_STATE_CHANED,userId,USER_STATE_CAM_ON,0);
        }
        else if(messsage==0 && value==1){
            addEvent(AGORA_EVENT_ON_USER_STATE_CHANED,userId,USER_STATE_MIC_OFF,0);
        }
        else if(messsage==0 && value==0){
            addEvent(AGORA_EVENT_ON_USER_STATE_CHANED,userId,USER_STATE_MIC_ON,0);
        }

    });

    _userObserver->setOnIframeRequestFn([this](){
        addEvent(AGORA_EVENT_ON_IFRAME,"",0,0);
    });

    _userObserver->setOnUserRemoteTrackStatsFn([this](const std::string& userId,
                                                      long* stats){

        addEvent(AGORA_EVENT_ON_REMOTE_TRACK_STATS_CHANGED,userId,0,0,stats);
     });

    _userObserver->setOnUserLocalTrackStatsFn([this](const std::string& userId,
                                                      long* stats){

        addEvent(AGORA_EVENT_ON_LOCAL_TRACK_STATS_CHANGED,userId,0,0,stats);
     });

  //setup the out sync buffer (source -> AG sdk)
  _outSyncBuffer=std::make_shared<SyncBuffer>(_in_video_delay, _in_audio_delay);
  _outSyncBuffer->setVideoOutFn([this](const uint8_t* buffer,
                                         size_t bufferLength,
                                         bool isKeyFrame){

        doSendHighVideo(buffer, bufferLength, isKeyFrame);

    });

    _outSyncBuffer->setAudioOutFn([this](const uint8_t* buffer,
                                         size_t bufferLength){
          doSendAudio(buffer, bufferLength);
    });

    //setup the in sync buffer ( AG sdk -> source)
    _inSyncBuffer=std::make_shared<SyncBuffer>(_out_video_delay, _out_audio_delay);
    _inSyncBuffer->setVideoOutFn([this](const uint8_t* buffer,
                                         size_t bufferLength,
                                         bool isKeyFrame){

        (void)isKeyFrame;

        if(_videoOutFn!=nullptr){
            _videoOutFn(buffer, bufferLength, _videoOutUserData);
            _videoInFps++;
        }

    });

    _inSyncBuffer->setAudioOutFn([this](const uint8_t* buffer,
                                         size_t bufferLength){

        if(_audioOutFn!=nullptr){
            _audioOutFn(buffer, bufferLength, _audioOutUserData);
        }

    });

    _isRunning=true;

    _publishUnpublishCheckThread=std::thread(&AgoraIo::publishUnpublishThreadFn,this);

    return true;
}


void AgoraIo::receiveVideoFrame(uint userId,
                                const uint8_t* buffer,
                                size_t length,
                                int isKeyFrame,
                                uint64_t ts){

    (void)userId;

    //do not read video if the pipeline is in pause state
    if(_isPaused) return;

    if(_inSyncBuffer!=nullptr && _isRunning){
        _inSyncBuffer->addVideo(buffer, length, isKeyFrame, ts);
    }
}

void AgoraIo::receiveAudioFrame(uint userId,
                                const uint8_t* buffer,
                                size_t length,
                                uint64_t ts){

    (void)userId;

    //do not read audio if the pipeline is in pause state
    if(_isPaused ) return;

     if(_inSyncBuffer!=nullptr && _isRunning){
        _inSyncBuffer->addAudio(buffer, length, ts);
     }
}

void AgoraIo::handleUserStateChange(const std::string& userId,
                                    const UserState& newState){

    if(newState==USER_JOIN){
        subscribeAudioUser(userId);
        _pcmFrameObserver->setUserJoined(true);
    }

    if(_receiveVideo==false){
        return;
    }

    //we monitor user volumes only for those who have camera events
    if(newState==USER_CAM_ON){
        _pcmFrameObserver->onUserJoined(userId);
    }
    else if(newState==USER_CAM_OFF){
      _pcmFrameObserver->onUserLeft(userId);
    }

    if(newState==USER_JOIN || newState==USER_CAM_ON){

        //if there is not active user we are subscribing to, subscribe to this user
        if(_activeUsers.empty()){
            subscribeToVideoUser(userId);
        }

        _activeUsers.emplace_back(userId);
    }
    else if(newState==USER_LEAVE || newState==USER_CAM_OFF){

         _connection->getLocalUser()->unsubscribeVideo(userId.c_str());
        _activeUsers.remove_if([userId](const std::string& id){ return (userId==id); });
        if(_activeUsers.empty()==false && _currentVideoUser==userId){

            auto newUserId=_activeUsers.front();
            subscribeToVideoUser(newUserId);
        }
    }
}

 void AgoraIo::subscribeToVideoUser(const std::string& userId){

    if(_sendOnly || _receiveVideo==false){
        return;
    }

#if SDK_BUILD_NUM>=190534
    agora::rtc::VideoSubscriptionOptions subscriptionOptions;
#else
    agora::rtc::ILocalUser::VideoSubscriptionOptions subscriptionOptions;
#endif
    subscriptionOptions.encodedFrameOnly = true;
    subscriptionOptions.type = agora::rtc::VIDEO_STREAM_HIGH;
    _connection->getLocalUser()->subscribeVideo(userId.c_str(), subscriptionOptions);

    _currentVideoUser=userId;
    logInfo("subscribed to video user #"+_currentVideoUser);

    addEvent(AGORA_EVENT_ON_VIDEO_SUBSCRIBED,userId,0,0);
 }

void AgoraIo::subscribeAudioUser(const std::string& userId){

    _connection->getLocalUser()->subscribeAudio(userId.c_str());
    logInfo("subscribed to audio user "+userId);
}
void AgoraIo::unsubscribeAudioUser(const std::string& userId){

  _connection->getLocalUser()->unsubscribeAudio(userId.c_str());
}

bool AgoraIo::doSendHighVideo(const uint8_t* buffer,  uint64_t len,int is_key_frame){

  auto frameType=agora::rtc::VIDEO_FRAME_TYPE_DELTA_FRAME;
  if(is_key_frame){
     frameType=agora::rtc::VIDEO_FRAME_TYPE_KEY_FRAME;
  }

  agora::rtc::EncodedVideoFrameInfo videoEncodedFrameInfo;
  videoEncodedFrameInfo.rotation = agora::rtc::VIDEO_ORIENTATION_0;
  videoEncodedFrameInfo.codecType = agora::rtc::VIDEO_CODEC_H264;
  // Dimensions/fps come from the pipeline caps (setVideoDimensions). SDK 4.4.x
  // needs a non-zero width/height here or remote decoders render a black frame.
  videoEncodedFrameInfo.width = _videoWidth;
  videoEncodedFrameInfo.height = _videoHeight;
  videoEncodedFrameInfo.frameType = frameType;
  videoEncodedFrameInfo.streamType = agora::rtc::VIDEO_STREAM_HIGH;

  //for a better a/v sync
  videoEncodedFrameInfo.captureTimeMs = getAgoraCurrentMonotonicTimeInMs();
  videoEncodedFrameInfo.decodeTimeMs = 0;
  videoEncodedFrameInfo.framesPerSecond = (_videoFps>0) ? (int)_videoFps : 30;

  _videoFrameSender->sendEncodedVideoImage(buffer,len,videoEncodedFrameInfo);

  return true;
}

bool AgoraIo::doSendAudio(const uint8_t* buffer,  uint64_t len){

  agora::rtc::EncodedAudioFrameInfo audioFrameInfo;
  audioFrameInfo.numberOfChannels =1; //TODO
  audioFrameInfo.sampleRateHz = 48000; //TODO
  audioFrameInfo.codec = agora::rtc::AUDIO_CODEC_OPUS;

  //for a better a/v sync
  audioFrameInfo.captureTimeMs = getAgoraCurrentMonotonicTimeInMs();

  _audioSender->sendEncodedAudioFrame(buffer,len, audioFrameInfo);

  return true;
}

int AgoraIo::sendVideo(const uint8_t * buffer,
                       uint64_t len,
                       int is_key_frame,
                       long timestamp){

    //do nothing if we are in pause state
    if(_isPaused==true){
        return 0;
    }

    if(_outSyncBuffer!=nullptr && _isRunning){
         startPublishVideo();
        _outSyncBuffer->addVideo(buffer, len, is_key_frame, timestamp);
    }

    _lastTimeVideoReceived=Now();

    showFps();

   return 0; //no errors
}

void AgoraIo::showFps(){

   if(_verbose){
        _videoOutFps++;
        if(_lastFpsPrintTime+std::chrono::milliseconds(1000)<=Now()){

            logMessage("Out video fps: "+std::to_string(_videoOutFps)
                       +", in video fps: "+std::to_string(_videoInFps));

            _videoInFps=0;
            _videoOutFps=0;

            _lastFpsPrintTime=Now();
        }
    }
}

int AgoraIo::sendAudio(const uint8_t * buffer,
                       uint64_t len,
                       long timestamp){

    //do nothing if we are in pause state
    if(_isPaused==true){
        return 0;
    }

    if(_outSyncBuffer!=nullptr && _isRunning){

        startPublishAudio();
        _outSyncBuffer->addAudio(buffer, len, timestamp);
     }

    _lastTimeAudioReceived=Now();

    return 0;
}

void AgoraIo::disconnect(){

    logMessage("start agora disconnect ...");

   //re-assert the aosl silencers: the SDK may have swapped the vlog sink
   //back at any point, and teardown is when the thread-detach spam fires
   quietAoslLogging();

   _isRunning=false;

   //stop the publish/unpublish watchdog before touching the connection
   if(_publishUnpublishCheckThread.joinable()){
       _publishUnpublishCheckThread.join();
   }

   //init may have failed at any point: release whatever exists (an
   //un-released service keeps SDK threads alive and wedges process exit)
   if(_service==nullptr){
       return;
   }
   if(_connection==nullptr){
       _service->release();
       _service=nullptr;
       return;
   }

   if(_outSyncBuffer!=nullptr) _outSyncBuffer->stop();
   if(_inSyncBuffer!=nullptr)  _inSyncBuffer->stop();

   if(_customAudioTrack){
       _connection->getLocalUser()->unpublishAudio(_customAudioTrack);
   }
   if(_customVideoTrack){
       _connection->getLocalUser()->unpublishVideo(_customVideoTrack);
   }

   //wait (bounded) for the SDK to confirm the disconnect instead of sleeping
   if(_connection->disconnect()==0 && _connectionObserver!=nullptr){
       _connectionObserver->waitUntilDisconnected(2000);
   }

   _connectionObserver.reset();
   _userObserver.reset();

   _audioSender = nullptr;
   _videoFrameSender = nullptr;
   _customAudioTrack = nullptr;
   _customVideoTrack = nullptr;

   _outSyncBuffer=nullptr;
   _inSyncBuffer=nullptr;

   _connection=nullptr;

   _service->release();
   _service = nullptr;

   h264FrameReceiver=nullptr;

   logInfo("Agora disconnected");
}

void AgoraIo::unsubscribeAllVideo(){

    _connection->getLocalUser()->unsubscribeAllVideo();
}
void AgoraIo::setPaused(bool flag){

    _isPaused=flag;
    if(_isPaused==true){
        unsubscribeAllVideo();

        stopPublishVideo();
        stopPublishAudio();
    }
    else{

      //clear any buffering
      _inSyncBuffer->clear();
      _outSyncBuffer->clear();

      startPublishVideo();
      startPublishAudio();

       unsubscribeAllVideo();
       if(_currentVideoUser!=""){
           subscribeToVideoUser(_currentVideoUser);
       }
    }
}

void AgoraIo::addEvent(const AgoraEventType& eventType,
                       const std::string& userName,
                       long param1,
                       long param2,
                       long* states){

    if(_eventfn!=nullptr){
        _eventfn(_userEventData, eventType, userName.c_str(), param1, param2, states);
    }
}

 void AgoraIo::setEventFunction(event_fn fn, void* userData){

     _userEventData=userData;
     _eventfn=fn;
 }

 void AgoraIo::setVideoOutFn(agora_media_out_fn videoOutFn, void* userData){
     _videoOutFn=videoOutFn;
     _videoOutUserData=userData;
 }

void AgoraIo::setAudioOutFn(agora_media_out_fn audioOutFn, void* userData){
     _audioOutFn=audioOutFn;
     _audioOutUserData=userData;
 }

 void AgoraIo::publishUnpublishThreadFn(){

     logMessage("Agoraio: publish/unpublish thread started");

     const long checkTimeMs=500;
      while(_isRunning){

         const long allowedUnpublishedTime=1000; //ms
         if((_lastTimeAudioReceived+std::chrono::milliseconds(allowedUnpublishedTime))<Now()){
              stopPublishAudio();
          }

         if((_lastTimeVideoReceived+std::chrono::milliseconds(allowedUnpublishedTime))<Now()){
            stopPublishVideo();
         }

         TimePoint  nextCheckTime = Now()+std::chrono::milliseconds(checkTimeMs);
         std::this_thread::sleep_until(nextCheckTime);

     }
 }

void AgoraIo::startPublishAudio(){

    if(_isPublishingAudio==true){
        return;
    }
    _connection->getLocalUser()->publishAudio(_customAudioTrack);
    _isPublishingAudio=true;

    logInfo("Agoraio: published audio");

 }
void AgoraIo::startPublishVideo(){

    if(_isPublishingVideo==true){
        return;
    }
     _connection->getLocalUser()->publishVideo(_customVideoTrack);
    _isPublishingVideo=true;

    logInfo("Agoraio: published video");
}

void AgoraIo::stopPublishAudio(){

    if(_isPublishingAudio==false){
        return;
    }
    _connection->getLocalUser()->unpublishAudio(_customAudioTrack);
    _isPublishingAudio=false;

    logInfo("Agoraio: unpublished audio");
}
void AgoraIo::stopPublishVideo(){

    if(_isPublishingVideo==false){
        return;
    }

     _connection->getLocalUser()->unpublishVideo(_customVideoTrack);
    _isPublishingVideo=false;

    logInfo("Agoraio: unpublished video");
}

void AgoraIo::setVideoDimensions(int width, int height, int fps){
    if(width>0)  _videoWidth=width;
    if(height>0) _videoHeight=height;
    if(fps>0)    _videoFps=fps;
}

std::list<std::string> AgoraIo::parseIpList(){

    std::stringstream ss(_proxyIps);

   std::list<std::string>  returnList;

    while (ss.good()){
        std::string ip;
        getline(ss, ip, ',');
        returnList.emplace_back(ip);
    }

  return returnList;
}

std::string AgoraIo::createProxyString(const std::list<std::string>& ipList){

    //TODO: this is a reference of how the proxy string looks like
    //agoraParameter->setParameters("{\"rtc.proxy_server\":[2, \"[\\\"128.1.77.34\\\", \\\"128.1.78.146\\\"]\", 0], \"rtc.enable_proxy\":true}");

    std::string ipListStr="\"[";
    bool addComma=false;
    for(const auto& ip: ipList){

        if(addComma){
            ipListStr+=",";
        }
        else{
            addComma=true;
        }

        ipListStr +="\\\""+ip+"\\\" ";
    }

    ipListStr+="]\", ";

    std::string proxyString="{\"rtc.proxy_server\":[2, "+
                             ipListStr +
                             "0], \"rtc.enable_proxy\":true}";

    return proxyString;
}
