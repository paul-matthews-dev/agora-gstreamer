//
//  Agora SDK
//
//  Copyright (c) 2020 Agora.io. All rights reserved.
//
#pragma once  // NOLINT(build/header_guard)

#include "AgoraBase.h"
#include "AgoraRefPtr.h"
#include "IAgoraMediaRtcRecorderEventHandler.h"


namespace agora {
namespace base {
class IAgoraService;
}
namespace rtc {

class IAgoraMediaRtcRecorderEventHandler;

struct UserMixerLayout
{
  user_id_t userId;
  MixerLayoutConfig config;
};

struct VideoMixingLayout
{
  int canvasWidth;
  int canvasHeight;
  int canvasFps;
  uint32_t backgroundColor;
  const char*  backgroundImage;
  uint32_t userLayoutConfigNum;
  const UserMixerLayout* userLayoutConfigs; 
};

/**
 * The IAgoraMediaRtcRecorder provides functions to record media stream from rtc channel.
 * You can create it by IMediaComponentFactory::createMediaRtcRecorder().
 * If you want to record multiple media stream,create multiple  IAgoraMediaRtcRecorder objects.
 * Enjoy your recording experience。
 */
class IAgoraMediaRtcRecorder : public RefCountInterface{

protected:
  virtual ~IAgoraMediaRtcRecorder() {}

public:
  /** 
   *  Initialize the recorder
   *  @param agora_service The agora service,please make sure it has initialized.
   *  @param enable_mix  Enable mix or not.
   *  @return
   *   - = 0: init success.
   *   - < 0: Failure.
   */
  virtual int initialize(base::IAgoraService* agora_service,bool enable_mix = true) = 0;

  /** 
   *  Join the Agora rtc channel.
   *  @param token The app ID or token.
   *  @param channelId The channel name. It must be in the string format and not exceed 64 bytes in length. Supported character scopes are:
   *  - All lowercase English letters: a to z.
   *  - All uppercase English letters: A to Z.
   *  - All numeric characters: 0 to 9.
   *  - The space character.
   *  - Punctuation characters and other symbols, including: "!", "#", "$", "%", "&", "(", ")", "+",
   *  "-", ":", ";", "<", "=",
   *  ".", ">", "?", "@", "[", "]", "^", "_", " {", "}", "|", "~", ","
   *  @param userId The ID of the local user. If you do not specify a user ID or set `userId` as `null`,
   *  @return
   *  - = 0: init success.
   *  - < 0: Failure.
   */
  virtual int joinChannel(const char* token,const char* channel_name,const char* userId) = 0;

  /**
   * leave from the Agora rtc channel.
   * @return
   * - 0: Success.
   * - < 0: Failure.
   */
  virtual int leavelChannel() = 0;

  /** Enables/Disables the built-in encryption.
   *
   * In scenarios requiring high security, Agora recommends calling this method to enable the built-in encryption before joining a channel.
   *
   * All users in the same channel must use the same encryption mode and encryption key. Once all users leave the channel, the encryption key of this channel is automatically cleared.
   *
   * @note
   * - If you enable the built-in encryption, you cannot use the RTMP streaming function.
   *
   * @param enabled Whether to enable the built-in encryption:
   * - true: Enable the built-in encryption.
   * - false: Disable the built-in encryption.
   * @param config Configurations of built-in encryption schemas. See \ref agora::rtc::EncryptionConfig "EncryptionConfig".
   *
   * @return
   * - 0: Success.
   * - < 0: Failure.
   */
  virtual int enableEncryption(bool enabled, const EncryptionConfig& config) = 0;

  /**
   * Subscribes and record to the audio of all remote users in the channel.
   *
   * This method also automatically subscribes to the audio of any subsequent user.
   *
   * @return
   * - 0: Success.
   * - < 0: Failure.
   */
  virtual int subscribeAllAudio() = 0;

  /**
   * Subscribes to the video of all remote users in the channel.
   *
   * This method also automatically subscribes to the video of any subsequent remote user.
   *
   * @param subscriptionOptions The reference to the video subscription options: \ref agora::rtc::VideoSubscriptionOptions "VideoSubscriptionOptions".
   * @return
   * - 0: Success.
   * - < 0: Failure.
   */
  virtual int subscribeAllVideo(const agora::rtc::VideoSubscriptionOptions& subscriptionOptions) = 0;

  /**
   * Stops subscribing to the audio of all remote users in the channel.
   *
   * This method automatically stops subscribing to the audio of any subsequent user, unless you explicitly
   * call \ref subscribeAudio "subscribeAudio" or \ref subscribeAllAudio "subscribeAllAudio".
   *
   * @return
   * - 0: Success.
   * - < 0: Failure.
   */
  virtual int unsubscribeAllAudio() = 0;

  /**
   * Stops subscribing to the video of all remote users in the channel.
   *
   * This method automatically stops subscribing to the video of any subsequent user, unless you explicitly
   * call \ref subscribeVideo "subscribeVideo" or \ref subscribeAllVideo "subscribeAllVideo".
   *
   * @return
   * - 0: Success.
   * - < 0: Failure.
   */
  virtual int unsubscribeAllVideo() = 0;

  /**
   * Subscribes and record to the audio of a specified remote user in channel.
   *
   * @param userId The ID of the remote user whose audio you want to subscribe to.
   * @return
   * - 0: Success.
   * - < 0: Failure.
   *   - -2(ERR_INVALID_ARGUMENT), if no such user exists or `userId` is invalid.
   */
  virtual int subscribeAudio(user_id_t userId) = 0;
  
  /**
   * Stops subscribing to the audio of a specified remote user in the channel.
   *
   * @param userId The ID of the user whose audio you want to stop subscribing to.
   * @return
   * - 0: Success.
   * - < 0: Failure.
   *   - -2(ERR_INVALID_ARGUMENT), if no such user exists or `userId` is invalid.
   */
  virtual int unsubscribeAudio(user_id_t userId) = 0;

  /**
   * Subscribes to the video of a specified remote user in the channel.
   *
   * @param userId The ID of the user whose video you want to subscribe to.
   * @param subscriptionOptions The reference to the video subscription options: \ref agora::rtc::VideoSubscriptionOptions "VideoSubscriptionOptions".
   * For example, subscribing to encoded video data only or subscribing to low-stream video.
   *
   * @return
   * - 0: Success.
   * - < 0: Failure.
   *   - -2(ERR_INVALID_ARGUMENT), if `userId` is invalid.
   */
  virtual int subscribeVideo(user_id_t userId, const VideoSubscriptionOptions& subscriptionOptions) = 0;
    
  /**
   * Stops subscribing to the video of a specified remote user in the channel.
   *
   * @param userId The ID of the user whose video you want to stop subscribing to.
   * @return
   * - 0: Success.
   * - < 0: Failure.
   *   - -2(ERR_INVALID_ARGUMENT), if `userId` is invalid.
   */
  virtual int unsubscribeVideo(user_id_t userId) = 0;
  
  /**
   * Sets the time interval of the onAudioVolumeIndication callback.
   * @param intervalInMS Sets the time interval(ms) between two consecutive volume indications. The default
   * value is 500.
   * - &le; 10: Disables the volume indication.
   * - > 10: The time interval (ms) between two consecutive callbacks.
   *
   * @return
   * - 0: Success.
   * - < 0: Failure.
   */
  virtual int setAudioVolumeIndicationParameters(int intervalInMS) = 0;
  
  /**
   *  Set The mix video stream Layout
   *  @param layout The layout of the mixed video stream you want \ref agora::rtc::VideoMixingLayout
   *  @return
   *  - 0: Success.
   *  - < 0: Failure.
   */
  virtual int setVideoMixingLayout(const VideoMixingLayout& layout) = 0;

  /**
   * Set the recorder config
   * @param config The config of the recorder \ref agora::media::MediaRecorderConfiguration
   * @return
   *  - 0: Success.
   *  - < 0: Failure.
  */
  virtual int setRecorderConfig(const media::MediaRecorderConfiguration& config) = 0;

  /**
   * Add the watermark for the stream
   * @param watermark_configs The  watermark config
   * @param num The  watermark config nums;
   * @return
   *  - 0: Success.
   *  - < 0: Failure.
  */
  virtual int enableAndUpdateVideoWatermarks(WatermarkConfig* watermark_configs, int num) = 0;

  /**
   * disable the watermark for the stream
   *  - 0: Success.
   *  - < 0: Failure.
   */
  virtual int disableVideoWatermarks() = 0;
  /**
   * Start the recording 
   * @return
   *  - 0: Success.
   *  - < 0: Failure.
  */
  virtual int startRecording() = 0;

  /**
   * Stop the recording 
   * @return
   *  - 0: Success.
   *  - < 0: Failure.
  */
  virtual int stopRecording() = 0;

  /**
   * Start the recording by userid
   * @param userId The ID of the user whose stream you want to start recorde to.
   * @return
   *  - 0: Success.
   *  - < 0: Failure.
  */
  virtual int startSingleRecordingByUid(user_id_t userId) = 0;

  /**
   * Stop the recording by userid
   * @param userId The ID of the user whose stream you want to stop recorde to.
   * @return
   *  - 0: Success.
   *  - < 0: Failure.
  */
  virtual int stopSingleRecordingByUid(user_id_t userId) = 0;

  /**
   * Stop the recording by userid
   * @param config The config of the recorder \ref agora::media::MediaRecorderConfiguration
   * @param userId The ID of the user whose stream you want to stop recorde to.
   * @return
   *  - 0: Success.
   *  - < 0: Failure.
  */
  virtual int setRecorderConfigByUid(const media::MediaRecorderConfiguration& config, user_id_t userId) = 0;

   /**
   * Add the watermark for the stream by uid
   * @param watermark_configs The  watermark config
   * @param num The  watermark config nums;
   * @param userId The ID of the user whose stream you want to add watermark.
   * @return
   *  - 0: Success.
   *  - < 0: Failure.
  */
  virtual int enableAndUpdateVideoWatermarksByUid(WatermarkConfig* watermark_configs, int num, user_id_t userId) = 0;

  /**
   * disable the watermark for the stream by uid
   * @param userId The ID of the user whose stream you want to disable watermark.
   *  - 0: Success.
   *  - < 0: Failure.
   */
  virtual int disableVideoWatermarksByUid(user_id_t userId) = 0;

  /**
   * Register the recording event handle
   * @param handler The handler of the recorder \ref agora::rtc::IAgoraMediaRtcRecorderEventHandler
   * @return
   *  - 0: Success.
   *  - < 0: Failure.
  */
  virtual int registerRecorderEventHandle(IAgoraMediaRtcRecorderEventHandler* handler) = 0;

  /**
   * Unregister the recording event handle
   * @param handler The handler of the recorder \ref agora::rtc::IAgoraMediaRtcRecorderEventHandler
   * @return
   *  - 0: Success.
   *  - < 0: Failure.
  */
  virtual int unregisterRecorderEventHandle(IAgoraMediaRtcRecorderEventHandler* handler) = 0;
};

}//namespace rtc
}// namespace agora