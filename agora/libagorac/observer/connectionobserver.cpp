
#include "connectionobserver.h"
#include "AgoraBase.h"

#include "../agoraio.h"

#include "../helpers/agoralog.h"

static std::string connInfoString(const char* event,
                                  const agora::rtc::TConnectionInfo &connectionInfo){
    return std::string(event)+": id "+std::to_string(connectionInfo.id)
           +", channelId "+connectionInfo.channelId.get()->c_str()
           +", localUserId "+connectionInfo.localUserId.get()->c_str();
}

void ConnectionObserver::onConnected(const agora::rtc::TConnectionInfo &connectionInfo,
										   agora::rtc::CONNECTION_CHANGED_REASON_TYPE reason)
{
	/*AG_LOG(INFO, "onConnected: id %u, channelId %s, localUserId %s, reason %d\n", connectionInfo.id,
		   connectionInfo.channelId.get()->c_str(), connectionInfo.localUserId.get()->c_str(),
		   reason);*/

    logMessage(connInfoString("onConnected", connectionInfo)+", reason "+std::to_string(reason));

    if(_onUserConnected!=nullptr){
        _onUserConnected(connectionInfo.localUserId.get()->c_str(), USER_CONNECTED);
    }

	// notify the thread which is waiting for the SDK to be connected
	connect_ready_.Set();

    std::string userId=connectionInfo.localUserId.get()->c_str();
    if(_parent!=nullptr){
        _parent->addEvent(AGORA_EVENT_ON_CONNECTED, userId,reason,0);
    }
}

void ConnectionObserver::onDisconnected(const agora::rtc::TConnectionInfo &connectionInfo,
											  agora::rtc::CONNECTION_CHANGED_REASON_TYPE reason)
{
	logMessage(connInfoString("onDisconnected", connectionInfo)+", reason "+std::to_string(reason));

	// notify the thread which is waiting for the SDK to be disconnected
	disconnect_ready_.Set();

    std::string userId=connectionInfo.localUserId.get()->c_str();
    if(_parent!=nullptr){
         _parent->addEvent(AGORA_EVENT_ON_DISCONNECTED,userId,reason,0);
    }
}

void ConnectionObserver::onConnecting(const agora::rtc::TConnectionInfo &connectionInfo,
											agora::rtc::CONNECTION_CHANGED_REASON_TYPE reason)
{
	logMessage(connInfoString("onConnecting", connectionInfo)+", reason "+std::to_string(reason));

    std::string userId=connectionInfo.localUserId.get()->c_str();
    if(_parent!=nullptr){
         _parent->addEvent(AGORA_EVENT_ON_CONNECTING,userId,0,0);
    }
}

void ConnectionObserver::onReconnecting(const agora::rtc::TConnectionInfo &connectionInfo,
											  agora::rtc::CONNECTION_CHANGED_REASON_TYPE reason)
{
	logMessage(connInfoString("onReconnecting", connectionInfo)+", reason "+std::to_string(reason));

    std::string userId=connectionInfo.localUserId.get()->c_str();
    if(_parent!=nullptr){
         _parent->addEvent(AGORA_EVENT_ON_RECONNECTING,userId,reason,0);
    }

}

void ConnectionObserver::onReconnected(const agora::rtc::TConnectionInfo &connectionInfo,
											 agora::rtc::CONNECTION_CHANGED_REASON_TYPE reason)
{
	logMessage(connInfoString("onReconnected", connectionInfo)+", reason "+std::to_string(reason));

    std::string userId=connectionInfo.localUserId.get()->c_str();
    if(_parent!=nullptr){
         _parent->addEvent(AGORA_EVENT_ON_RECONNECTED,userId,reason,0);
    }
}

void ConnectionObserver::onConnectionLost(const agora::rtc::TConnectionInfo &connectionInfo)
{
	logMessage(connInfoString("onConnectionLost", connectionInfo));

    std::string userId=connectionInfo.localUserId.get()->c_str();
    if(_parent!=nullptr){
         _parent->addEvent(AGORA_EVENT_ON_CONNECTION_LOST,userId,0,0);
    }
}

void ConnectionObserver::onUplinkNetworkInfoUpdated(const agora::rtc::UplinkNetworkInfo &info)
{

    if(_parent!=nullptr){
         _parent->addEvent(AGORA_EVENT_ON_UPLINK_NETWORK_INFO_UPDATED,"",info.video_encoder_target_bitrate_bps,0);
    }
}


void ConnectionObserver::onUserJoined(agora::user_id_t userId)
{

    logMessage(std::string("onUserJoined: userId ")+userId);

    if(_onUserStateChanged!=nullptr){
        _onUserStateChanged(userId, USER_JOIN);
    }

    if(_parent!=nullptr){
         _parent->addEvent(AGORA_EVENT_ON_USER_STATE_CHANED,userId,USER_STATE_JOIN,0);
    }
}

void ConnectionObserver::onUserLeft(agora::user_id_t userId,
										  agora::rtc::USER_OFFLINE_REASON_TYPE reason)
{

   logMessage(std::string("onUserLeft: userId ")+userId);

   if(_onUserStateChanged!=nullptr){
        _onUserStateChanged(userId, USER_LEAVE);
   }

   if(_parent!=nullptr){
         _parent->addEvent(AGORA_EVENT_ON_USER_STATE_CHANED,userId,USER_STATE_LEAVE,0);
    }
}
