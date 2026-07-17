//  Agora RTC/MEDIA SDK
//
//  Created by Pengfei Han in 2020-03.
//  Copyright (c) 2020 Agora.io. All rights reserved.
//

#include "userobserver.h"
#include "../helpers/agoralog.h"

#include <cstring>

UserObserver::UserObserver(agora::rtc::ILocalUser* local_user, const bool& verbose)
    : local_user_(local_user),
    _onUserInfoChanged(nullptr),
    _onUserVolumeChanged(nullptr),
    _onIframeRequest(nullptr),
    _verbose(verbose),
    _onRemoteTrackStats(nullptr),
    _onLocalTrackStats(nullptr){
  local_user_->registerLocalUserObserver(this);
}

UserObserver::~UserObserver() {
  local_user_->unregisterLocalUserObserver(this);
}

agora::rtc::ILocalUser* UserObserver::GetLocalUser() { return local_user_; }

void UserObserver::PublishAudioTrack(
    agora::agora_refptr<agora::rtc::ILocalAudioTrack> audioTrack) {
  local_user_->publishAudio(audioTrack);
}

void UserObserver::PublishVideoTrack(
    agora::agora_refptr<agora::rtc::ILocalVideoTrack> videoTrack) {
  local_user_->publishVideo(videoTrack);
}

void UserObserver::UnpublishAudioTrack(
    agora::agora_refptr<agora::rtc::ILocalAudioTrack> audioTrack) {
  local_user_->unpublishAudio(audioTrack);
}

void UserObserver::UnpublishVideoTrack(
    agora::agora_refptr<agora::rtc::ILocalVideoTrack> videoTrack) {
  local_user_->unpublishVideo(videoTrack);
}

void UserObserver::onUserAudioTrackSubscribed(
    agora::user_id_t userId, agora::agora_refptr<agora::rtc::IRemoteAudioTrack> audioTrack) {
  std::lock_guard<std::mutex> _(observer_lock_);
  remote_audio_track_ = audioTrack;
  if (remote_audio_track_ && media_packet_receiver_) {
    remote_audio_track_->registerMediaPacketReceiver(media_packet_receiver_);
  }
  if (remote_audio_track_ && audio_frame_observer_) {
    local_user_->registerAudioFrameObserver(audio_frame_observer_);
  }
}

#if SDK_BUILD_NUM>=675674
void UserObserver::onUserVideoTrackSubscribed(
    agora::user_id_t userId, const agora::rtc::VideoTrackInfo& trackInfo,
    agora::agora_refptr<agora::rtc::IRemoteVideoTrack> videoTrack) {
#else
void UserObserver::onUserVideoTrackSubscribed(
    agora::user_id_t userId, agora::rtc::VideoTrackInfo trackInfo,
    agora::agora_refptr<agora::rtc::IRemoteVideoTrack> videoTrack) {
#endif
 /* AG_LOG(INFO, "onUserVideoTrackSubscribed: userId %s, codecType %d, encodedFrameOnly %d", userId,
         trackInfo.codecType, trackInfo.encodedFrameOnly);*/
  std::lock_guard<std::mutex> _(observer_lock_);
  remote_video_track_ = videoTrack;
  if (remote_video_track_ && video_encoded_receiver_) {
    remote_video_track_->registerVideoEncodedFrameObserver(video_encoded_receiver_);
  }
  if (remote_video_track_ && media_packet_receiver_) {
    remote_video_track_->registerMediaPacketReceiver(media_packet_receiver_);
  }
  if (remote_video_track_ && video_frame_observer_) {
    remote_video_track_->addRenderer(video_frame_observer_,
                                     agora::media::base::POSITION_PRE_RENDERER);
  }
}

void UserObserver::onUserInfoUpdated(agora::user_id_t userId,
                                                ILocalUserObserver::USER_MEDIA_INFO msg, bool val) {
 // AG_LOG(INFO, "onUserInfoUpdated: userId %s, msg %d, val %d", userId, msg, val);

   if(_onUserInfoChanged!=nullptr){
       _onUserInfoChanged(userId, msg, val);
   }

   if(_verbose){
       logMessage(std::string("UserObserver::onUserInfoUpdated: userId ")+userId
                  +", msg "+std::to_string(msg)+", val "+std::to_string(val));
   }
 
}

void UserObserver::onUserAudioTrackStateChanged(
    agora::user_id_t userId, agora::agora_refptr<agora::rtc::IRemoteAudioTrack> audioTrack,
    agora::rtc::REMOTE_AUDIO_STATE state, agora::rtc::REMOTE_AUDIO_STATE_REASON reason,
    int elapsed) {
  

}

void UserObserver::onIntraRequestReceived() {

  if(_onIframeRequest){
      _onIframeRequest();
  }

  if(_verbose==false)  return;
   logMessage("Agora sdk requested an iframe");
}

/* The SDK fires this twice per interval: once for the LOCAL user (userId "0",
   vad meaningful) and once for remote users (vad always 0). Only the local
   entry may drive the vad state, otherwise the two reports flap it. */
#if SDK_BUILD_NUM>=190534
void UserObserver::onAudioVolumeIndication(const agora::rtc::AudioVolumeInformation* speakers,
                                       unsigned int speakerNumber, int totalVolume) {

    if(speakers==nullptr || _onLocalVad==nullptr){
        return;
    }

    for(unsigned int i=0;i<speakerNumber;i++){
        if(speakers[i].userId!=nullptr &&
           (strcmp(speakers[i].userId,"0")==0 ||
            (!_localUserId.empty() && _localUserId==speakers[i].userId))){
            _onLocalVad((int)speakers[i].vad, (int)speakers[i].volume);
            return;
        }
    }
}

#else

void UserObserver::onAudioVolumeIndication(const agora::rtc::AudioVolumeInfo* speakers,
                                       unsigned int speakerNumber, int totalVolume) {

    if(speakers==nullptr || _onLocalVad==nullptr){
        return;
    }

    for(unsigned int i=0;i<speakerNumber;i++){
        if(speakers[i].uid==0){
            _onLocalVad((int)speakers[i].vad, (int)speakers[i].volume);
            return;
        }
    }
}

#endif

void UserObserver::onRemoteVideoTrackStatistics(agora::agora_refptr<agora::rtc::IRemoteVideoTrack> videoTrack,
                                    const agora::rtc::RemoteVideoTrackStats& stats)
{

  if(_onRemoteTrackStats!=nullptr){

      const int MAX_STATES=15;
      long remoteStats[MAX_STATES];

      remoteStats[0]=stats.receivedBitrate;
      remoteStats[1]=stats.decoderOutputFrameRate;
      remoteStats[2]=stats.rendererOutputFrameRate;

      remoteStats[3]=stats.frameLossRate;
      remoteStats[4]=stats.packetLossRate;
      remoteStats[5]=stats.rxStreamType;

      remoteStats[6]=stats.totalFrozenTime;
      remoteStats[7]=stats.frozenRate;
      remoteStats[8]=stats.totalDecodedFrames;

      #if SDK_BUILD_NUM !=110077
     	remoteStats[9]=stats.avSyncTimeMs;
      	remoteStats[10]=stats.downlink_process_time_ms;
      	remoteStats[11]=stats.frame_render_delay_ms;
      #endif



      std::string userId= std::to_string(stats.uid);
      _onRemoteTrackStats(userId, remoteStats);
  }

}

void UserObserver::onLocalVideoTrackStatistics(agora::agora_refptr<agora::rtc::ILocalVideoTrack> videoTrack,
                                   const agora::rtc::LocalVideoTrackStats& stats) 
{
     if(_onRemoteTrackStats!=nullptr){

      const int MAX_STATES=15;
      long localStats[MAX_STATES];

      localStats[0]=stats.number_of_streams;
      localStats[1]=stats.bytes_major_stream;
      localStats[2]=stats.bytes_minor_stream;

      localStats[3]=stats.frames_encoded;
      localStats[4]=stats.ssrc_major_stream;
      localStats[5]=stats.ssrc_minor_stream;

      localStats[6]=stats.input_frame_rate;
      localStats[7]=stats.encode_frame_rate;
      localStats[8]=stats.render_frame_rate;

      localStats[9]=stats.target_media_bitrate_bps;
      localStats[10]=stats.media_bitrate_bps;
      localStats[11]=stats.total_bitrate_bps;

      localStats[12]=stats.width;
      localStats[13]=stats.height;
      localStats[14]=stats.encoder_type;

      _onLocalTrackStats("Local", localStats);
     }

}

void UserObserver::setOnUserRemoteTrackStatsFn(const OnUserRemoteTrackStateFn& fn){

    _onRemoteTrackStats=fn;
}
void UserObserver::setOnUserLocalTrackStatsFn(const OnUserRemoteTrackStateFn& fn){

    _onLocalTrackStats=fn;
}
