//
//  Agora SDK
//
//  Copyright (c) 2020 Agora.io. All rights reserved.
//
#pragma once  // NOLINT(build/header_guard)

#include "AgoraBase.h"


namespace agora {
namespace rtc {
  struct RemoteVideoStatistics {
    int delay;
    int width;
    int height;
    int receivedBitrate;
    int decoderOutputFrameRate;
    VIDEO_STREAM_TYPE rxStreamType;
  };

  struct RemoteAudioStatistics {
    int quality;
    int networkTransportDelay;
    int jitterBufferDelay;
    int audioLossRate;
  };

  struct SpeakVolumeInfo{
    user_id_t userId;
    unsigned int volume;
  };
/**
 * The IAgoraMediaRtcRecorderEventHandler class, which observes the state and stats of the recorder.
 */
  class IAgoraMediaRtcRecorderEventHandler{
    public:
      virtual ~IAgoraMediaRtcRecorderEventHandler() {}

      /**
       * Occurs when the connection state between the SDK and the Agora channel changes to `CONNECTION_STATE_CONNECTED(3)`.
       *
       * @param channelId The channel ID.
       * @param uid The user ID.
       */
      virtual void onConnected(const char *channelId, user_id_t uid) = 0;

      /**
       * Occurs when the connection state between the SDK and the Agora channel changes to `CONNECTION_STATE_DISCONNECTED(1)`.
       * 
       * @param channelId The channel ID.
       * @param uid The user ID.
       * @param reason The reason of the connection state change. See #CONNECTION_CHANGED_REASON_TYPE.
       */
      virtual void onDisconnected(const char *channelId, user_id_t uid, CONNECTION_CHANGED_REASON_TYPE reason) = 0;

      /**
       * Occurs when the connection state between the SDK and the Agora channel changes to `CONNECTION_STATE_CONNECTED(3) ` again.
       * 
       * @param channelId The channel ID.
       * @param uid The user ID.
       * @param reason The reason of the connection state change. See #CONNECTION_CHANGED_REASON_TYPE.
       */
      virtual void onReconnected(const char *channelId, user_id_t uid, CONNECTION_CHANGED_REASON_TYPE reason) = 0;

      /**
       * Occurs when the SDK loses connection with the Agora channel.
       *
       * @param channelId The channel ID.
       * @param uid The user ID.
       */
      virtual void onConnectionLost(const char *channelId, user_id_t uid) = 0;

       /**
       * Occurs when a remote user joins the channel.
       *
       * @param channelId The channel ID.
       * @param uid The user ID.
       */
      virtual void onUserJoined(const char *channelId, user_id_t uid) = 0;

      /**
       * Occurs when a remote user leaves the channel.
       *
       * @param channelId The channel ID.
       * @param uid The user ID.
       * @param reason The reason why the remote user leaves the channel: #USER_OFFLINE_REASON_TYPE.
       */
      virtual void onUserLeft(const char *channelId, user_id_t uid, USER_OFFLINE_REASON_TYPE reason) = 0;

      /**
       * Occurs when the SDK decodes the first remote video frame for recorder.
       *
       * @param channelId The channel ID.
       * @param userId ID of the remote user.
       * @param width Width (px) of the video stream.
       * @param height Height (px) of the video stream.
       * @param rotation rotation of the video stream.
       * @param elapsed The time (ms) since the user connects to an Agora channel.
       */
      virtual void onFirstRemoteVideoDecoded(const char *channelId, user_id_t userId, int width, int height, int elapsed) = 0;

      /**
       * Occurs when the SDK decodes the first remote audio frame for playback.
       * 
       * @param channelId The channel ID.
       * @param uid User ID of the remote user sending the audio stream.
       * @param elapsed The time (ms) since the user connects to an Agora channel.
       */
      virtual void onFirstRemoteAudioDecoded(const char *channelId, user_id_t userId, int elapsed) = 0;
      // virtual void onUserInfoUpdated(agora::user_id_t userId, USER_MEDIA_INFO msg, bool val);

      /**
       * Reports which users are speaking, the speakers' volumes, and whether the local user is speaking.
       * 
       * @param channelId The channel ID.
       * @param speakers An array of each speaker’s UID and volume information..
       * @param speakerNumber The total number of the speakers.
       */
      virtual void onAudioVolumeIndication(const char *channelId, const SpeakVolumeInfo* speakers,  unsigned int speakerNumber) = 0;

      /**
       * Occurs when an active speaker is detected.
       *
       * You can add relative functions on your app, for example, the active speaker, once detected,
       * will have the portrait zoomed in.
       *
       * @note
       * - The active speaker means the user ID of the speaker who speaks at the highest volume during a
       * certain period of time.
       *
       * @param channelId The channel ID.
       * @param userId The ID of the active speaker. A `userId` of `0` means the local user.
       */
      virtual void onActiveSpeaker(const char *channelId, user_id_t userId) = 0;

      /**
       * Occurs when the state of video changes.
       *
       * @param channelId The channel ID.
       * @param userId the ID of the remote user whose video state has changed
       * @param state The current state of the video.
       * @param reason The reason for the state change.
       * @param elapsed The time (ms) since the user connects to an Agora channel.
       */
      virtual void onUserVideoStateChanged(const char *channelId, user_id_t userId, REMOTE_VIDEO_STATE state, REMOTE_VIDEO_STATE_REASON reason,int elapsed) = 0;

       /**
       * Occurs when the state of audio changes.
       *
       * @param channelId The channel ID.
       * @param userId the ID of the remote user whose audio state has changed
       * @param state The current state of the audio.
       * @param reason The reason for the state change.
       * @param elapsed The time (ms) since the user connects to an Agora channel.
       */
      virtual void onUserAudioStateChanged(const char *channelId, user_id_t userId, REMOTE_AUDIO_STATE state, REMOTE_AUDIO_STATE_REASON reason,int elapsed) = 0;

      /**
       * Reports the statistics of a remote video.
       *
       * @param channelId The channel ID.
       * @param userId the ID of the remote user.
       * @param stats The current stats of the video.
       */
      virtual void onRemoteVideoStats(const char *channelId, user_id_t userId, const RemoteVideoStatistics& stats) = 0;

      /**
       * Reports the statistics of a remote audio.
       *
       * @param channelId The channel ID.
       * @param userId the ID of the remote user.
       * @param stats The current state of the audio.
       */
      virtual void onRemoteAudioStats(const char *channelId, user_id_t userId, const RemoteAudioStatistics& stats) = 0;

       /**
       * Occurs when the recording state changes.
       *
       * When the local audio and video recording state changes, the SDK triggers this callback to
       * report the current recording state and the reason for the change.
       *
       * @param channelId The channel name.
       * @param userId ID of the user.
       * @param state The current recording state. See \ref agora::media::RecorderState "RecorderState".
       * @param reason The reason for the state change. See \ref agora::media::RecorderReasonCode
       * @param filename The filename for recorde stream
       * "RecorderReasonCode".
       */
      virtual void onRecorderStateChanged(const char* channelId, user_id_t userId, media::RecorderState state,media::RecorderReasonCode reason, const char* filename) = 0;

       /**
       * Occurs when the recording information is updated.
       *
       * After you successfully register this callback and enable the local audio and video recording,
       * the SDK periodically triggers the `onRecorderInfoUpdated` callback based on the set value of
       * `recorderInfoUpdateInterval`. This callback reports the filename, duration, and size of the
       * current recording file.
       *
       * @param channelId The channel name.
       * @param uid ID of the user.
       * @param info Information about the recording file. See \ref agora::media::RecorderInfo
       * "RecorderInfo".
       */
      virtual void onRecorderInfoUpdated(const char* channelId, user_id_t userId,const media::RecorderInfo& info) = 0;

      /**
        * Reports the error type of encryption.
        * @param channelId The channel name.
        * @param type See #ENCRYPTION_ERROR_TYPE.
        */
      virtual void onEncryptionError(const char* channelId, ENCRYPTION_ERROR_TYPE errorType) = 0;
  };

}
}