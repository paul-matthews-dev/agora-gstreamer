#ifndef _AGORA_C_H_
#define _AGORA_C_H_
#include <stdbool.h>
#include <sys/types.h>
#ifdef __cplusplus
 #define EXTERNC extern "C"
 #else
 #define EXTERNC
 #endif

 #include "agoraconfig.h"

 typedef  void (*agora_media_out_fn)(const u_int8_t* buffer,
                                     u_int64_t len,
                                     void* user_data);

 typedef struct AgoraIoContext_t  AgoraIoContext_t;


 EXTERNC AgoraIoContext_t*  agoraio_init(agora_config_t* config);

 EXTERNC void agoraio_disconnect(AgoraIoContext_t** ctx);

 EXTERNC int  agoraio_send_video(AgoraIoContext_t* ctx,
                                const unsigned char* buffer,
                                unsigned long len,
                                int is_key_frame,
                                long timestamp);

 EXTERNC void  agoraio_set_paused(AgoraIoContext_t* ctx, int flag);

 // Set the encoded video dimensions/framerate (from the GStreamer caps). These
 // populate EncodedVideoFrameInfo so remote SDK decoders render correctly; call
 // whenever caps change (resolution/fps can differ per call type).
 EXTERNC void  agoraio_set_video_dimensions(AgoraIoContext_t* ctx, int width, int height, int fps);

 EXTERNC int  agoraio_send_audio(AgoraIoContext_t* ctx,
                               const unsigned char* buffer,
                               unsigned long len,
                               long timestamp);

 EXTERNC void agoraio_set_event_handler(AgoraIoContext_t* ctx, event_fn fn, void* userData);

 EXTERNC void agoraio_set_video_out_handler(AgoraIoContext_t* ctx, agora_media_out_fn fn, void* userData);

 EXTERNC void agoraio_set_audio_out_handler(AgoraIoContext_t* ctx, agora_media_out_fn fn, void* userData);

#undef EXTERNC


#endif
