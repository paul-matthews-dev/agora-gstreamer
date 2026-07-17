#include "syncbuffer.h"
#include "helpers/context.h"

#include "helpers/agoralog.h"

const size_t MAX_BUFFER_SIZE=200;

static int g_id=0;

SyncBuffer::SyncBuffer(uint16_t videoDelayOffset,
                       uint16_t audioDelayOffset):
_videoBuffer(std::make_shared<WorkQueue <Work_ptr> >()),
_audioBuffer(std::make_shared<WorkQueue <Work_ptr> >()),
_videoOutFn(nullptr),
_audioOutFn(nullptr),
_videoDelayOffset(videoDelayOffset),
_audioDelayOffset(audioDelayOffset)
{
  _objId=g_id++;

  logMessage("SyncBuffer#"+std::to_string(_objId)
             +": videoDelayOffset="+std::to_string(_videoDelayOffset)
             +", audioDelayOffset="+std::to_string(_audioDelayOffset));
}

void SyncBuffer::addVideo(const uint8_t* buffer,
                          size_t length,
                          int isKeyFrame,
                          uint64_t ts){

    //no delay requested: hand the frame through on the caller's thread
    if(_videoDelayOffset==0){
       if(_videoOutFn!=nullptr){
            _videoOutFn(buffer, length, isKeyFrame!=0);
       }
       return;
    }

    if(_videoBuffer->size()>MAX_BUFFER_SIZE){
        logMessage("JB#"+std::to_string(_objId)+": warning: sync buffer (video) exceeded max buffer: "+std::to_string(_videoBuffer->size()));
    }

    auto frame=std::make_shared<Work>(buffer, length, isKeyFrame!=0);
    frame->timestamp=ts;
    _videoBuffer->add(frame);

    //dispatch once roughly delay-offset ms worth of ~30ms frames are queued
    if(_videoBuffer->size()*30>=_videoDelayOffset){

        Work_ptr work=_videoBuffer->get();
        if(work!=nullptr && _videoOutFn!=nullptr){
            _videoOutFn(work->buffer.data(), work->buffer.size(), work->is_key_frame);
        }
    }
}

void SyncBuffer::addAudio(const uint8_t* buffer,
                          size_t length,
                          uint64_t ts){

    //no delay requested: hand the packet through on the caller's thread
    if(_audioDelayOffset==0){
       if(_audioOutFn!=nullptr){
             _audioOutFn(buffer, length);
        }
       return;
    }

    if(_audioBuffer->size()>MAX_BUFFER_SIZE){
        logMessage("JB#"+std::to_string(_objId)+": warning: sync buffer (audio) exceeded max buffer: "+std::to_string(_audioBuffer->size()));
    }

    auto frame=std::make_shared<Work>(buffer, length, false);
    frame->timestamp=ts;
    _audioBuffer->add(frame);

    //dispatch once roughly delay-offset ms worth of ~10ms packets are queued
    if(_audioBuffer->size()*10>=_audioDelayOffset){

        Work_ptr work=_audioBuffer->get();
        if(work!=nullptr && _audioOutFn!=nullptr){
            _audioOutFn(work->buffer.data(), work->buffer.size());
        }
    }
}

void SyncBuffer::stop(){
    clear();
}

void SyncBuffer::setVideoOutFn(const videoOutFn_t& fn){
    _videoOutFn=fn;
}
void SyncBuffer::setAudioOutFn(const audioOutFn_t& fn){
    _audioOutFn=fn;
}

void SyncBuffer::clear(){

    _videoBuffer->clear();
    _audioBuffer->clear();
}
