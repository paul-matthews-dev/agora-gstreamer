#ifndef _AGORA_TYPE_H_
#define _AGORA_TYPE_H_

#include <chrono>
#include <memory>

#include "workqueue.h"
#include "agorac.h"   /* event_fn (via agoraconfig.h) and agora_media_out_fn */

class SyncBuffer;
using SyncBuffer_ptr=std::shared_ptr<SyncBuffer>;

using TimePoint=std::chrono::steady_clock::time_point;

enum AgoraEventType{

   AGORA_EVENT_ON_IFRAME=1,
   AGORA_EVENT_ON_CONNECTING,
   AGORA_EVENT_ON_CONNECTED,
   AGORA_EVENT_ON_DISCONNECTED,

   AGORA_EVENT_ON_USER_STATE_CHANED,

   AGORA_EVENT_ON_UPLINK_NETWORK_INFO_UPDATED,
   AGORA_EVENT_ON_CONNECTION_LOST,
   AGORA_EVENT_ON_CONNECTION_FAILURE,

   AGORA_EVENT_ON_RECONNECTING,
   AGORA_EVENT_ON_RECONNECTED,

   AGORA_EVENT_ON_VIDEO_SUBSCRIBED,

   AGORA_EVENT_ON_REMOTE_TRACK_STATS_CHANGED,
   AGORA_EVENT_ON_LOCAL_TRACK_STATS_CHANGED
};

enum State{

   USER_STATE_JOIN=1,
   USER_STATE_LEAVE,
   USER_STATE_CAM_ON,
   USER_STATE_CAM_OFF,
   USER_STATE_MIC_ON,
   USER_STATE_MIC_OFF
};

#endif
