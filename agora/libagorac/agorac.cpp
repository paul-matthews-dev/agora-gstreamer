#include "agorac.h"

#include <string>

#include "helpers/agoralog.h"
#include "helpers/context.h"
#include "agoraio.h"

AgoraIoContext_t*  agoraio_init(agora_config_t* config){

    setLogEnabled(config->verbose);

    AgoraIoContext_t* ctx=new AgoraIoContext_t;

    ctx->agoraIo=std::make_shared<AgoraIo>(config->verbose,
                                           config->fn,
                                           config->userData,
                                           config->in_audio_delay,
                                           config->in_video_delay,
                                           config->out_audio_delay,
                                           config->out_video_delay,
                                           config->sendOnly,
                                           config->enableProxy,
                                           config->proxy_timeout,
                                           config->proxy_ips,
                                           config->receive_video);

    if(!ctx->agoraIo->init(config->app_id, config->ch_id, config->user_id)){
       //release whatever the partial init created, so no SDK thread outlives us
       ctx->agoraIo->disconnect();
       delete ctx;
       return nullptr;
    }

    return ctx;
}

int  agoraio_send_video(AgoraIoContext_t* ctx,
                        const unsigned char* buffer,
                        unsigned long len,
                        int is_key_frame,
                        long timestamp){

    if(ctx==nullptr || ctx->agoraIo==nullptr)  return -1;

    return ctx->agoraIo->sendVideo(buffer, len, is_key_frame, timestamp);
}

int agoraio_send_audio(AgoraIoContext_t* ctx,
                       const unsigned char * buffer,
                       unsigned long len,
                       long timestamp){

    if(ctx==nullptr || ctx->agoraIo==nullptr)  return -1;

    ctx->agoraIo->sendAudio(buffer, len, timestamp);
    return 0;
}

void agoraio_set_video_dimensions(AgoraIoContext_t* ctx, int width, int height, int fps){

   if(ctx==nullptr)  return;

   ctx->agoraIo->setVideoDimensions(width, height, fps);
}

void agoraio_disconnect(AgoraIoContext_t** ctx){

   if(ctx==nullptr || *ctx==nullptr){
      logMessage("agoraio_disconnect called with no context");
      return;
   }

   (*ctx)->agoraIo->disconnect();

   delete (*ctx);
   *ctx=nullptr;
}

void  agoraio_set_paused(AgoraIoContext_t* ctx, int flag){

    if(ctx==nullptr)  return;

    ctx->agoraIo->setPaused(flag);
}

void agoraio_set_event_handler(AgoraIoContext_t* ctx, event_fn fn, void* userData){

    if(ctx==nullptr)  return;

    ctx->agoraIo->setEventFunction(fn, userData);
}

void agoraio_set_video_out_handler(AgoraIoContext_t* ctx, agora_media_out_fn fn, void* userData){

   if(ctx==nullptr)  return;

   ctx->agoraIo->setVideoOutFn(fn, userData);
}

void agoraio_set_audio_out_handler(AgoraIoContext_t* ctx, agora_media_out_fn fn, void* userData){

   if(ctx==nullptr)  return;

   ctx->agoraIo->setAudioOutFn(fn, userData);
}
