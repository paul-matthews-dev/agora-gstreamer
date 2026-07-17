/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2021 Ubuntu <<user@hostname.org>>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
* Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-agoraioudp
 *
 * Bidirectional bridge between a GStreamer pipeline and an Agora RTC channel.
 * The sink pad publishes encoded H.264 video to the channel; audio is bridged
 * over local UDP (inport = Opus in -> Agora, outport = PCM out from Agora).
 * Remote video is only delivered on the src pad when receive-video=true.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 videotestsrc ! x264enc tune=zerolatency !
 *   video/x-h264,stream-format=byte-stream,alignment=au !
 *   agoraioudp appid=XXX channel=test userid=101
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <glib/gstdio.h>

#include "gstagoraioudp.h"

GST_DEBUG_CATEGORY_STATIC (gst_agoraioudp_debug);
#define GST_CAT_DEFAULT gst_agoraioudp_debug

/* Filter signals and args */
enum
{
  ON_IFRAME_SIGNAL=1,
  ON_CONNECTING_SIGNAL,
  ON_CONNECTED_SIGNAL,
  ON_DISCONNECTED_SIGNAL,
  ON_USER_STATE_CHANGED_SIGNAL,
  ON_UPLINK_NETWORK_INFO_UPDATED_SIGNAL,

  ON_CONNECTION_LOST_SIGNAL,
  ON_CONNECTION_FAILURE_SIGNAL,

  ON_RECONNECTING_SIGNAL,
  ON_RECONNECTED_SIGNAL,

  ON_VIDEO_SUBSCRIBED_SIGNAL,

  ON_REMOTE_TRACK_STATS_CHANGED,
  ON_LOCAL_TRACK_STATS_CHANGED,

  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_VERBOSE,
  APP_ID,
  CHANNEL_ID,
  USER_ID,
  IN_PORT,
  OUT_PORT,
  HOST,
  AUDIO,
  IN_AUDIO_DELAY,
  IN_VIDEO_DELAY,
  OUT_AUDIO_DELAY,
  OUT_VIDEO_DELAY,
  PROXY,
  OPERATIONAL_MODE,
  PROXY_CONNECT_TIMEOUT,
  PROXY_IPS,
  RECEIVE_VIDEO,
  AUDIO_PCM,
  AGORA_PARAMS
};

/* Operational modes:
 *   1 = local loopback test mode: no SDK connection at all; sink video is
 *       looped back to the src pad and inport audio to outport.
 *   2 = video only: publish video via the SDK, no audio UDP bridge.
 *   3 = full mode (default): publish video + bidirectional audio bridge.
 */
enum {

    OPERATION_MODE_1=1,
    OPERATION_MODE_2=2,
    OPERATION_MODE_3=3
};


static guint agoraio_signals[LAST_SIGNAL] = { 0 };


/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */

/* The element carries encoded H.264 only. Pinning byte-stream here matters:
 * with ANY caps an upstream tee/rtph264pay could flip x264enc to avc, which
 * the Agora receivers cannot render (black screen). */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, stream-format=(string)byte-stream, alignment=(string)au")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, stream-format=(string)byte-stream")
    );


#define gst_agoraioudp_parent_class parent_class
G_DEFINE_TYPE (Gstagoraioudp, gst_agoraioudp, GST_TYPE_ELEMENT);

static void gst_agoraioudp_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_agoraioudp_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_agoraioudp_finalize (GObject * object);

static void handle_video_out_fn(const u_int8_t* buffer, u_int64_t len, void* user_data );

static void handle_audio_out_fn(const u_int8_t* buffer, u_int64_t len, void* user_data );

static void gst_agoraioudp_teardown (Gstagoraioudp *agoraIO);


/* The appsink has received a buffer: hand the audio to the SDK in place */
static GstFlowReturn new_sample (GstElement *sink, gpointer *user_data) {

   GstSample *sample;
   GstMapInfo map;

   Gstagoraioudp *agoraIO=(Gstagoraioudp*)user_data;

  g_signal_emit_by_name (sink, "pull-sample", &sample);
  if (!sample) {
    return GST_FLOW_ERROR;
  }

  GstBuffer * in_buffer=gst_sample_get_buffer (sample);
  if (in_buffer && gst_buffer_map (in_buffer, &map, GST_MAP_READ)) {

    GstClockTime in_buffer_pts=GST_BUFFER_PTS (in_buffer);

    if(agoraIO->mode==OPERATION_MODE_3){
        g_mutex_lock (&agoraIO->ctx_lock);
        if (agoraIO->agora_ctx!=NULL) {
            agoraio_send_audio(agoraIO->agora_ctx, map.data, map.size,
                               in_buffer_pts/1000000);
        }
        g_mutex_unlock (&agoraIO->ctx_lock);
    }
    else if(agoraIO->mode==OPERATION_MODE_1){
        handle_audio_out_fn(map.data, map.size, agoraIO);
    }

    gst_buffer_unmap (in_buffer, &map);
  }

  gst_sample_unref (sample);
  return GST_FLOW_OK;
}

void handle_agora_pending_events(Gstagoraioudp *agoraIO, 
                                 int eventType,
                                 const char* userName,
                                 long param1, 
                                 long param2,
                                 long* states);

static void handle_event_Signal(void* userData, 
                         int type, 
                         const char* userName,
                         long param1,
                         long param2,
                         long* states){

  
    Gstagoraioudp* agoraIO=(Gstagoraioudp*)(userData);

    handle_agora_pending_events(agoraIO, type, userName,param1, param2, states);

}

int setup_audio_udp(Gstagoraioudp *agoraIO){

   agoraIO->appAudioSrc= gst_element_factory_make ("appsrc", "source");
   agoraIO->udpsink = gst_element_factory_make("udpsink", "udpsink");
   agoraIO->udpsrc = gst_element_factory_make("udpsrc", "udpsrc");
   agoraIO->appAudioSink = gst_element_factory_make("appsink", "appsink");
   agoraIO->out_pipeline = gst_pipeline_new ("pipeline");
   agoraIO->in_pipeline = gst_pipeline_new ("in-pipeline");

   if(!agoraIO->appAudioSrc || !agoraIO->udpsink || !agoraIO->udpsrc ||
      !agoraIO->appAudioSink || !agoraIO->out_pipeline || !agoraIO->in_pipeline){
       GST_ELEMENT_ERROR (agoraIO, CORE, MISSING_PLUGIN, (NULL),
           ("failed to create the internal audio UDP bridge elements"));
       return FALSE;
   }

   //out plugin
   gst_bin_add_many (GST_BIN (agoraIO->out_pipeline), agoraIO->appAudioSrc, agoraIO->udpsink, NULL);
   gst_element_link_many (agoraIO->appAudioSrc, agoraIO->udpsink, NULL);

   //in plugin
   gst_bin_add_many (GST_BIN (agoraIO->in_pipeline),agoraIO->udpsrc, agoraIO->appAudioSink, NULL);
   gst_element_link_many (agoraIO->udpsrc, agoraIO->appAudioSink, NULL);

    //setup appsrc 
    g_object_set (G_OBJECT (agoraIO->appAudioSrc),
            "stream-type", 0,
            "is-live", TRUE,
            "format", GST_FORMAT_TIME, NULL);

     g_object_set (G_OBJECT (agoraIO->udpsink),
            "host", agoraIO->host,
            "port", agoraIO->out_port,
              NULL);

    g_object_set (G_OBJECT (agoraIO->udpsrc),
             "port", agoraIO->in_port,
              NULL);

    //set the pipeline in playing mode
    gst_element_set_state (agoraIO->out_pipeline, GST_STATE_PLAYING);
    gst_element_set_state (agoraIO->in_pipeline, GST_STATE_PLAYING);

    //Configure appsink 
    g_object_set (agoraIO->appAudioSink, "emit-signals", TRUE, NULL);
    g_signal_connect (agoraIO->appAudioSink, "new-sample",
                    G_CALLBACK (new_sample), agoraIO);

    return TRUE;
}

int init_agora(Gstagoraioudp *agoraIO){

   if (strlen(agoraIO->app_id)==0){
       GST_ELEMENT_ERROR (agoraIO, RESOURCE, SETTINGS, (NULL),
           ("the appid property cannot be empty"));
       return FALSE;
   }

   if (strlen(agoraIO->channel_id)==0){
       GST_ELEMENT_ERROR (agoraIO, RESOURCE, SETTINGS, (NULL),
           ("the channel property cannot be empty"));
       return FALSE;
   }

   if(agoraIO->mode<2){
       GST_INFO_OBJECT (agoraIO, "running in basic mode: no video or audio via the sdk");
       return setup_audio_udp(agoraIO);
   }

   agora_config_t config;

   config.app_id=agoraIO->app_id;               /*appid*/
   config.ch_id=agoraIO->channel_id;            /*channel*/
   config.user_id=agoraIO->user_id;             /*user id*/
   config.verbose=agoraIO->verbose;             /*log level*/
   config.fn=handle_event_Signal;               /*signal function to call*/
   config.userData=(void*)(agoraIO);            /*additional params to the signal function*/ ;
   config.in_audio_delay=agoraIO->in_audio_delay;
   config.in_video_delay=agoraIO->in_video_delay;
   config.out_audio_delay=agoraIO->out_audio_delay;
   config.out_video_delay=agoraIO->out_video_delay;
   config.sendOnly= 0;                          /*send only flag*/
   config.enableProxy=agoraIO->proxy;           /*enable proxy*/
   config.proxy_timeout= agoraIO->reconnect_timeout;   /*proxy timeout*/
   config.proxy_ips= agoraIO->proxy_ips;               /*proxy ips*/
   config.receive_video=agoraIO->receive_video; /*subscribe to remote video*/
   config.audio_pcm=agoraIO->audio_pcm;         /*raw PCM uplink -> SDK 3A/AEC*/
   config.agora_params=agoraIO->agora_params;   /*setParameters JSON*/

    /*initialize agora*/
   agoraIO->agora_ctx=agoraio_init(&config);

   if(agoraIO->agora_ctx==NULL){
      GST_ELEMENT_ERROR (agoraIO, RESOURCE, OPEN_READ_WRITE, (NULL),
          ("could not initialize the Agora SDK connection"));
      return FALSE;
   }

   //apply the video dimensions/fps cached from the caps event (which fired before
   //agora_ctx existed) so encoded frames carry a valid size for remote decoders
   agoraio_set_video_dimensions(agoraIO->agora_ctx,
       agoraIO->vid_width, agoraIO->vid_height, agoraIO->vid_fps);

   //this function will be called whenever there is a video frame ready
   //(remote video is only subscribed when receive-video=true)
   if(agoraIO->receive_video){
       agoraio_set_video_out_handler(agoraIO->agora_ctx, handle_video_out_fn, (void*)(agoraIO));
   }
   agoraio_set_audio_out_handler(agoraIO->agora_ctx, handle_audio_out_fn, (void*)(agoraIO));

   //initialize timestamps to zero
   agoraIO->video_ts=0;
   agoraIO->audio_ts=0;

   GST_INFO_OBJECT (agoraIO, "agora has been successfully initialized");
   if(agoraIO->mode<3){
       GST_INFO_OBJECT (agoraIO, "running in mode 2: video via the sdk, no audio bridge");
       return TRUE;
   }

   GST_INFO_OBJECT (agoraIO, "running in full mode: video via the sdk + audio bridge");
   return setup_audio_udp(agoraIO);
}

/* the src pad delivers live frames straight from the SDK's jitter buffer */
static gboolean
gst_agoraio_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
      gst_query_set_latency (query, TRUE, 0, GST_CLOCK_TIME_NONE);
      return TRUE;
    default:
      return gst_pad_query_default (pad, parent, query);
  }
}

void handle_agora_pending_events(Gstagoraioudp *agoraIO, 
                                 int eventType,
                                 const char* userName,
                                 long param1, 
                                 long param2,
                                 long* states){

       switch (eventType){
             case ON_IFRAME_SIGNAL: 
                  g_signal_emit (G_OBJECT (agoraIO),agoraio_signals[ON_IFRAME_SIGNAL], 0);
                  break;
             case ON_CONNECTING_SIGNAL: 
                  g_signal_emit (G_OBJECT (agoraIO),agoraio_signals[ON_CONNECTING_SIGNAL], 0);
                  break;
             case ON_CONNECTED_SIGNAL: 
                  g_signal_emit (G_OBJECT (agoraIO),agoraio_signals[ON_CONNECTED_SIGNAL], 0, userName,param1);
                  break;
              case ON_DISCONNECTED_SIGNAL: 
                  g_signal_emit (G_OBJECT (agoraIO),agoraio_signals[ON_DISCONNECTED_SIGNAL], 0, userName,param1);
                  break;
             case ON_UPLINK_NETWORK_INFO_UPDATED_SIGNAL: 
                  g_signal_emit (G_OBJECT (agoraIO),agoraio_signals[ON_UPLINK_NETWORK_INFO_UPDATED_SIGNAL], 0, param1);
                  break;
             case ON_CONNECTION_LOST_SIGNAL: 
                  g_signal_emit (G_OBJECT (agoraIO),agoraio_signals[ON_CONNECTION_LOST_SIGNAL], 0);
                  break;
             case ON_CONNECTION_FAILURE_SIGNAL: 
                  g_signal_emit (G_OBJECT (agoraIO),agoraio_signals[ON_CONNECTION_FAILURE_SIGNAL], 0);
                  break;
            case ON_RECONNECTING_SIGNAL: 
                  g_signal_emit (G_OBJECT (agoraIO),agoraio_signals[ON_RECONNECTING_SIGNAL], 0,userName,param1);
                  break;
            case ON_RECONNECTED_SIGNAL: 
                  g_signal_emit (G_OBJECT (agoraIO),agoraio_signals[ON_RECONNECTED_SIGNAL], 0,userName,param1);
                  break;
            case ON_USER_STATE_CHANGED_SIGNAL: 
                  g_signal_emit (G_OBJECT (agoraIO),agoraio_signals[ON_USER_STATE_CHANGED_SIGNAL], 0,userName,param1);
                  break;
            case ON_VIDEO_SUBSCRIBED_SIGNAL: 
                  g_signal_emit (G_OBJECT (agoraIO),agoraio_signals[ON_VIDEO_SUBSCRIBED_SIGNAL], 0,userName);
                  break;
            case ON_REMOTE_TRACK_STATS_CHANGED: 
                  g_signal_emit (G_OBJECT (agoraIO),agoraio_signals[ON_REMOTE_TRACK_STATS_CHANGED], 0, userName,
                                 states[0], states[1], states[2],
                                 states[3], states[4], states[5],
                                 states[6], states[7], states[8],
                                 states[9], states[10], states[11]);

                  break;
             case ON_LOCAL_TRACK_STATS_CHANGED: 
                  g_signal_emit (G_OBJECT (agoraIO),agoraio_signals[ON_LOCAL_TRACK_STATS_CHANGED], 0, userName,
                                 states[0], states[1], states[2],
                                 states[3], states[4], states[5],
                                 states[6], states[7], states[8],
                                 states[9], states[10], states[11],
                                 states[12], states[13], states[14]);

                  break;
            default:
                 return; //may be there is no more signals 
       }
}

//handle video out from agora to the plugin (only wired when receive-video=true)
static void handle_video_out_fn(const u_int8_t* buffer, u_int64_t len, void* user_data ){

    Gstagoraioudp* agoraIO=(Gstagoraioudp*)(user_data);

     GstBuffer * out_buffer=gst_buffer_new_allocate (NULL, len, NULL);

     gst_buffer_fill(out_buffer, 0, buffer, len);

     GST_BUFFER_DURATION (out_buffer)=30*GST_MSECOND;

     GstFlowReturn retCode=gst_pad_push (agoraIO->srcpad, out_buffer);
     if(retCode!=GST_FLOW_OK && retCode!=GST_FLOW_NOT_LINKED){
         GST_WARNING_OBJECT (agoraIO, "cannot push remote video downstream: %s",
             gst_flow_get_name (retCode));
     }
}

static void handle_audio_out_fn(const u_int8_t* data_buffer, u_int64_t len, void* user_data ){

    GstBuffer *buffer;
    GstFlowReturn ret;

    Gstagoraioudp* agoraIO=(Gstagoraioudp*)(user_data);
    if(agoraIO->mode==OPERATION_MODE_2){
        return;
    }

    buffer = gst_buffer_new_allocate (NULL, len, NULL);
    gst_buffer_fill(buffer, 0, data_buffer, len);

    /* PCM out of the SDK is S16LE mono 48 kHz: len/2 samples */
    GstClockTime duration=gst_util_uint64_scale (len/2, GST_SECOND, 48000);

    GST_BUFFER_PTS (buffer)=agoraIO->audio_ts;
    GST_BUFFER_DTS (buffer)=agoraIO->audio_ts;
    GST_BUFFER_DURATION (buffer)=duration;

    agoraIO->audio_ts += duration;

    ret = gst_app_src_push_buffer(GST_APP_SRC_CAST(agoraIO->appAudioSrc), buffer);
    if (ret != GST_FLOW_OK) {
        GST_WARNING_OBJECT (agoraIO, "not able to push audio data: %s",
            gst_flow_get_name (ret));
    }
}

static GstFlowReturn gst_agoraio_chain (GstPad * pad, GstObject * parent, GstBuffer * in_buffer){

    GstMapInfo map;
    int    is_key_frame=0;

    Gstagoraioudp *agoraIO=GST_AGORAIOUDP (parent);

    if(agoraIO->agora_ctx==NULL && agoraIO->mode>OPERATION_MODE_1){
        gst_buffer_unref (in_buffer);
        return GST_FLOW_ERROR;
    }

    //do nothing in case of pause
    if(agoraIO->state==PAUSED){
        gst_buffer_unref (in_buffer);
        return GST_FLOW_OK;
    }

    if(!gst_buffer_map (in_buffer, &map, GST_MAP_READ)){
        gst_buffer_unref (in_buffer);
        GST_ERROR_OBJECT (agoraIO, "cannot map input buffer");
        return GST_FLOW_ERROR;
    }

    GstClockTime in_buffer_pts=GST_BUFFER_PTS (in_buffer);

    if(GST_BUFFER_FLAG_IS_SET(in_buffer, GST_BUFFER_FLAG_DELTA_UNIT) == FALSE){
        is_key_frame=1;
    }

    //mode 1: used for testing and debugging
    if(agoraIO->mode==OPERATION_MODE_1){
        handle_video_out_fn(map.data, map.size, (void*)(agoraIO));
    }
    else if(agoraIO->audio==FALSE){
        unsigned long ts=(unsigned long)(in_buffer_pts/1000000); //in ms
        g_mutex_lock (&agoraIO->ctx_lock);
        if(agoraIO->agora_ctx!=NULL){
            agoraio_send_video(agoraIO->agora_ctx, map.data, map.size, is_key_frame, ts);
        }
        g_mutex_unlock (&agoraIO->ctx_lock);
    }

    gst_buffer_unmap (in_buffer, &map);
    gst_buffer_unref (in_buffer);

    return GST_FLOW_OK;
}

static GstStateChangeReturn
gst_on_change_state (GstElement *element, GstStateChange transition)
{

  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  Gstagoraioudp *agoraIO=GST_AGORAIOUDP (element);

  GST_DEBUG_OBJECT (agoraIO, "state change: %s -> %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
       case GST_STATE_CHANGE_NULL_TO_READY:
            if(agoraIO->agora_ctx==NULL && init_agora(agoraIO)!=TRUE){
                GST_ERROR_OBJECT (agoraIO, "cannot initialize agora");
                return GST_STATE_CHANGE_FAILURE;
             }
            break;
       case GST_STATE_CHANGE_READY_TO_NULL:
            gst_agoraioudp_teardown (agoraIO);
            break;
       case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
            agoraIO->state=PAUSED;
            if(agoraIO->agora_ctx!=NULL)
                agoraio_set_paused(agoraIO->agora_ctx, TRUE);
            break;
        case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
            agoraIO->state=RUNNING;
            if(agoraIO->agora_ctx!=NULL)
                agoraio_set_paused(agoraIO->agora_ctx, FALSE);
            break;
	   default:
	        break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  
  return ret;
}

static void release_audio_pipelines(Gstagoraioudp *agoraIO){

    if(agoraIO->in_pipeline==NULL ||
       agoraIO->out_pipeline==NULL){
          GST_DEBUG_OBJECT (agoraIO, "audio pipelines were never allocated, nothing to release");
          return;
    }
    if(agoraIO->mode==3 || agoraIO->mode==1){

        gst_element_send_event(agoraIO->in_pipeline, gst_event_new_eos());
        gst_element_send_event(agoraIO->out_pipeline, gst_event_new_eos());

        if(!gst_element_set_state (agoraIO->in_pipeline, GST_STATE_NULL) ||
           !gst_element_set_state (agoraIO->out_pipeline, GST_STATE_NULL)){
               GST_WARNING_OBJECT (agoraIO, "not able to stop the audio bridge pipelines");
        }

        //release internal pipelines
        gst_object_unref (agoraIO->in_pipeline);
        gst_object_unref (agoraIO->out_pipeline);
    }

    agoraIO->in_pipeline=NULL;
    agoraIO->out_pipeline=NULL;
}

/* Disconnect from agora and drop the audio bridge. Idempotent; runs on EOS and
   again on READY->NULL so teardown happens even when EOS never arrives (e.g. a
   failed preroll). The SDK is disconnected FIRST so its callback threads stop
   before the appsrc they push into is unreffed. */
static void gst_agoraioudp_teardown (Gstagoraioudp *agoraIO){

    g_mutex_lock (&agoraIO->ctx_lock);
    AgoraIoContext_t* ctx=agoraIO->agora_ctx;
    agoraIO->agora_ctx=NULL;
    g_mutex_unlock (&agoraIO->ctx_lock);

    if(ctx!=NULL){
        agoraio_disconnect(&ctx);
    }

    if(agoraIO->mode!=OPERATION_MODE_2){
        release_audio_pipelines(agoraIO);
    }
}

/* this function handles sink events */
static gboolean
gst_agoraio_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  Gstagoraioudp *agoraIO;

  agoraIO = GST_AGORAIOUDP (parent);

  GST_LOG_OBJECT (agoraIO, "received %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
       {
        GstCaps * caps;
        gst_event_parse_caps (event, &caps);
        /* Cache the encoded video dimensions/fps from the caps and forward them to
           the SDK so remote decoders render correctly (SDK 4.4.x needs a non-zero
           width/height per frame). Caps arrive before agora_ctx exists (it is created
           lazily on the first buffer), so we also apply the cached values in
           init_agora(). This runs again on any mid-stream caps change (eth/4g). */
        if(caps!=NULL){
           GstStructure *st = gst_caps_get_structure(caps, 0);
           gint fps_n=0, fps_d=1;
           gst_structure_get_int(st, "width", &agoraIO->vid_width);
           gst_structure_get_int(st, "height", &agoraIO->vid_height);
           gst_structure_get_fraction(st, "framerate", &fps_n, &fps_d);
           agoraIO->vid_fps = (fps_d>0) ? (fps_n/fps_d) : 0;
           if(agoraIO->agora_ctx!=NULL){
              agoraio_set_video_dimensions(agoraIO->agora_ctx,
                 agoraIO->vid_width, agoraIO->vid_height, agoraIO->vid_fps);
           }
        }
       }
       break;

    case GST_EVENT_EOS:
        GST_INFO_OBJECT (agoraIO, "received EOS, disconnecting from agora");
        agoraIO->state=ENDED;
        gst_agoraioudp_teardown (agoraIO);
        break;
    default:
      break;
  }

  return  gst_pad_event_default (pad, parent, event);
}

/* GObject vmethod implementations */

/* initialize the agoraioudp's class */
static void
gst_agoraioudp_class_init (GstagoraioudpClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_agoraioudp_set_property;
  gobject_class->get_property = gst_agoraioudp_get_property;
  gobject_class->finalize = gst_agoraioudp_finalize;

  //on pipeline stae change
  gstelement_class->change_state = gst_on_change_state;

 g_object_class_install_property (gobject_class, PROP_VERBOSE,
      g_param_spec_boolean ("verbose", "verbose", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, AUDIO,
      g_param_spec_boolean ("audio", "audio", "when true, it reads audio from agora than video",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /*app id*/
  g_object_class_install_property (gobject_class, APP_ID,
      g_param_spec_string ("appid", "appid", "agora app id",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /*channel_id*/
  g_object_class_install_property (gobject_class, CHANNEL_ID,
      g_param_spec_string ("channel", "channel", "agora channel id",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /*user_id*/
  g_object_class_install_property (gobject_class, USER_ID,
      g_param_spec_string ("userid", "userid", "agora user id (optional)",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


  /*in port*/
  g_object_class_install_property (gobject_class, IN_PORT,
      g_param_spec_int ("inport", "inport", "inport udp port for audio in",0, G_MAXUINT16,
          7373, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    

  /*out port*/
  g_object_class_install_property (gobject_class, OUT_PORT,
      g_param_spec_int ("outport", "outport", "outport udp port for audio out", 0, G_MAXUINT16,
          7374, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /*host*/
  g_object_class_install_property (gobject_class, HOST,
      g_param_spec_string ("host", "host", "udp host that we send audio to it",
          "127.0.0.1", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /*in audio delay*/
  g_object_class_install_property (gobject_class, IN_AUDIO_DELAY,
      g_param_spec_int ("in-audio-delay", "in-audio-delay", "amount of delay (ms) for audio gst -> agora SDK", 0, G_MAXUINT16,
          0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /*in video delay*/
  g_object_class_install_property (gobject_class, IN_VIDEO_DELAY,
      g_param_spec_int ("in-video-delay", "in-video-delay", "amount of delay (ms) for video from gst -> agora SDK ", 0, G_MAXUINT16,
          0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
     
  /*in audio delay*/
  g_object_class_install_property (gobject_class, OUT_AUDIO_DELAY,
      g_param_spec_int ("out-audio-delay", "out-audio-delay", "amount of delay (ms) for audio from agora SDK -> gst ", 0, G_MAXUINT16,
          0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

   /*in video delay*/
  g_object_class_install_property (gobject_class, OUT_VIDEO_DELAY,
      g_param_spec_int ("out-video-delay", "out-video-delay", "amount of delay (ms) for video from agora SDK -> gst ", 0, G_MAXUINT16,
          0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

   g_object_class_install_property (gobject_class, PROXY,
      g_param_spec_boolean ("proxy", "proxy", "place call via proxy  ?",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

   //mode
   g_object_class_install_property (gobject_class, OPERATIONAL_MODE,
      g_param_spec_int ("mode", "mode",
          "operational mode: 1=local loopback test (no SDK), 2=video only (no audio bridge), 3=video and audio (default)",
          1, 3, 3, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    //proxy timeout
    g_object_class_install_property (gobject_class, PROXY_CONNECT_TIMEOUT,
      g_param_spec_int ("proxytimeout", "proxytimeout", "set the value of connection timeout before trying to connect  with a proxy ", 1, G_MAXUINT16,
          10000, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    //proxy ips
    g_object_class_install_property (gobject_class, PROXY_IPS,
      g_param_spec_string ("proxyips", "proxyips", "set proxy ips (comma separated list)",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
        
    // receive-video: subscribe to remote video and push it out of the src pad.
    // Off by default: this element is typically used publish-only for video
    // (e.g. an intercom), and subscribing wastes downlink bandwidth.
    g_object_class_install_property (gobject_class, RECEIVE_VIDEO,
      g_param_spec_boolean ("receive-video", "receive-video",
          "subscribe to remote video and push it out of the src pad",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    // audio-pcm: inport carries raw PCM (S16LE, 48 kHz, mono) instead of Opus.
    // The backend paces it into 10 ms frames and publishes through the SDK's
    // audio processing pipeline with AEC enabled (plus ANS/AGC/VAD). The
    // default (false) keeps the legacy pre-encoded pass-through, which gets
    // no audio processing at all.
    g_object_class_install_property (gobject_class, AUDIO_PCM,
      g_param_spec_boolean ("audio-pcm", "audio-pcm",
          "inport carries raw PCM S16LE 48kHz mono; enables SDK AEC/ANS/AGC/VAD",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    // agora-params: raw setParameters JSON forwarded to the SDK after connect,
    // e.g. '{"che.audio.aec.fixed_delay":80}' for AEC delay tuning.
    g_object_class_install_property (gobject_class, AGORA_PARAMS,
      g_param_spec_string ("agora-params", "agora-params",
          "JSON passed to the SDK's setParameters after connect (tuning knobs)",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class,
    "Agora RTC bridge (agoraioudp)",
    "Source/Sink/Network",
    "Publishes H.264 video to an Agora RTC channel and bridges audio over local UDP; "
    "optionally delivers remote video on the src pad",
    "Ben <benweekes73@gmail.com>");

  gst_element_class_add_static_pad_template (gstelement_class, &src_factory);
  gst_element_class_add_static_pad_template (gstelement_class, &sink_factory);


  /*install agoraio available signals*/
  agoraio_signals[ON_IFRAME_SIGNAL] =
      g_signal_new ("on-iframe", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,G_TYPE_NONE, 0);

  agoraio_signals[ON_CONNECTING_SIGNAL] =
      g_signal_new ("on-connecting", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,G_TYPE_NONE, 0);

    
  agoraio_signals[ON_CONNECTED_SIGNAL] =
      g_signal_new ("on-connected", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,G_TYPE_NONE,2, G_TYPE_STRING, G_TYPE_INT);

  agoraio_signals[ON_DISCONNECTED_SIGNAL] =
      g_signal_new ("on-disconnected", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,G_TYPE_NONE,2, G_TYPE_STRING, G_TYPE_INT);

 agoraio_signals[ON_UPLINK_NETWORK_INFO_UPDATED_SIGNAL] =
      g_signal_new ("on-uplink-network-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,G_TYPE_NONE, 1, G_TYPE_INT);

 agoraio_signals[ON_CONNECTION_LOST_SIGNAL] =
      g_signal_new ("on-connection-lost", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,G_TYPE_NONE, 0);

 agoraio_signals[ON_CONNECTION_FAILURE_SIGNAL] =
      g_signal_new ("on-connection-failure", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,G_TYPE_NONE, 0);

 agoraio_signals[ON_RECONNECTING_SIGNAL] =
      g_signal_new ("on-reconnecting", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,G_TYPE_NONE,2, G_TYPE_STRING, G_TYPE_INT);

 agoraio_signals[ON_RECONNECTED_SIGNAL] =
      g_signal_new ("on-reconnected", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,G_TYPE_NONE,2, G_TYPE_STRING, G_TYPE_INT);

 agoraio_signals[ON_USER_STATE_CHANGED_SIGNAL] =
      g_signal_new ("on-user-state-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_INT);

 agoraio_signals[ON_VIDEO_SUBSCRIBED_SIGNAL] =
      g_signal_new ("on-user-video-subscribed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,G_TYPE_NONE, 1, G_TYPE_STRING);

 agoraio_signals[ON_REMOTE_TRACK_STATS_CHANGED] =
      g_signal_new ("on-remote-track-stats", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,G_TYPE_NONE, 
       13, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT,
                         G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT,
                         G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT,
                         G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);


  agoraio_signals[ON_LOCAL_TRACK_STATS_CHANGED] =
      g_signal_new ("on-local-track-stats", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,G_TYPE_NONE, 
       16, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT,
                         G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT,
                         G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT,
                         G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT,
                          G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);

}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_agoraioudp_init (Gstagoraioudp * agoraIO)
{
  //for src
  agoraIO->srcpad = gst_pad_new_from_static_template (&src_factory, "src");

  GST_PAD_SET_PROXY_CAPS (agoraIO->srcpad);
  gst_pad_set_query_function (agoraIO->srcpad,
                              GST_DEBUG_FUNCPTR(gst_agoraio_src_query));
  gst_element_add_pad (GST_ELEMENT (agoraIO), agoraIO->srcpad);

  //for sink 
  agoraIO->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_chain_function (agoraIO->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_agoraio_chain));

  gst_pad_set_event_function (agoraIO->sinkpad,gst_agoraio_sink_event);

  GST_PAD_SET_PROXY_CAPS (agoraIO->sinkpad);
  gst_element_add_pad (GST_ELEMENT (agoraIO), agoraIO->sinkpad);


  //set it initially to null
  agoraIO->agora_ctx=NULL;
  g_mutex_init (&agoraIO->ctx_lock);

  //set app_id and channel_id to zero
  memset(agoraIO->app_id, 0, MAX_STRING_LEN);
  memset(agoraIO->channel_id, 0, MAX_STRING_LEN);
  memset(agoraIO->user_id, 0, MAX_STRING_LEN);

  memset(agoraIO->host, 0, MAX_STRING_LEN);
  strcpy(agoraIO->host,"127.0.0.1");

  agoraIO->in_port=7373;
  agoraIO->out_port=7374;
  
  agoraIO->verbose = FALSE;
  agoraIO->audio=FALSE;
  agoraIO->receive_video=FALSE;
  agoraIO->audio_pcm=FALSE;
  memset(agoraIO->agora_params, 0, MAX_STRING_LEN);

  agoraIO->mode=3;

  agoraIO->reconnect_timeout=10000;
  memset(agoraIO->proxy_ips, 0, MAX_STRING_LEN);

  agoraIO->in_pipeline=NULL; 
  agoraIO->out_pipeline=NULL;
}

static void
gst_agoraioudp_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{

 Gstagoraioudp *agoraIO = GST_AGORAIOUDP (object);

 const gchar* str;

  switch (prop_id) {
    case PROP_VERBOSE:
         agoraIO->verbose = g_value_get_boolean (value);
         break;
    case APP_ID:
        str=g_value_get_string (value);
        g_strlcpy(agoraIO->app_id, str, MAX_STRING_LEN);
        break;
    case CHANNEL_ID:
        str=g_value_get_string (value);
        g_strlcpy(agoraIO->channel_id, str, MAX_STRING_LEN);
        break; 
    case USER_ID:
        str=g_value_get_string (value);
        g_strlcpy(agoraIO->user_id, str, MAX_STRING_LEN);
        break; 
    case AUDIO: 
        agoraIO->audio = g_value_get_boolean (value);
        break;
    case IN_PORT: 
       agoraIO->in_port=g_value_get_int (value);
       break;
    case OUT_PORT: 
       agoraIO->out_port=g_value_get_int (value);
       break;
    case HOST: 
       str=g_value_get_string (value);
       g_strlcpy(agoraIO->host, str, MAX_STRING_LEN);
       break;
    case IN_AUDIO_DELAY: 
       agoraIO->in_audio_delay=g_value_get_int (value);
       break;
    case IN_VIDEO_DELAY: 
       agoraIO->in_video_delay=g_value_get_int (value);
       break;
    case OUT_AUDIO_DELAY: 
       agoraIO->out_audio_delay=g_value_get_int (value);
       break;
    case OUT_VIDEO_DELAY: 
       agoraIO->out_video_delay=g_value_get_int (value);
       break;
    case PROXY:
       agoraIO->proxy = g_value_get_boolean (value);
       break;
    case OPERATIONAL_MODE: 
       agoraIO->mode=g_value_get_int (value);
       break;
    case PROXY_CONNECT_TIMEOUT: 
       agoraIO->reconnect_timeout=g_value_get_int (value);
       break;
    case PROXY_IPS:
        str=g_value_get_string (value);
        g_strlcpy(agoraIO->proxy_ips, str, MAX_STRING_LEN);
        break; 
    case RECEIVE_VIDEO:
         agoraIO->receive_video = g_value_get_boolean (value);
         break;
    case AUDIO_PCM:
         agoraIO->audio_pcm = g_value_get_boolean (value);
         break;
    case AGORA_PARAMS:
        str=g_value_get_string (value);
        g_strlcpy(agoraIO->agora_params, str ? str : "", MAX_STRING_LEN);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
  }
}

static void
gst_agoraioudp_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Gstagoraioudp *agoraIO = GST_AGORAIOUDP (object);

  switch (prop_id) {
    case PROP_VERBOSE:
       g_value_set_boolean (value, agoraIO->verbose);
       break;
    case APP_ID:
       g_value_set_string (value, agoraIO->app_id);
       break;
    case CHANNEL_ID:
        g_value_set_string (value, agoraIO->channel_id);
       break;
    case USER_ID:
        g_value_set_string (value, agoraIO->user_id);
        break;
    case AUDIO:
        g_value_set_boolean (value, agoraIO->audio);
        break;
    case IN_PORT:
        g_value_set_int (value, agoraIO->in_port);
        break;
    case OUT_PORT:
        g_value_set_int (value, agoraIO->out_port);
        break;
    case HOST:
        g_value_set_string (value, agoraIO->host);
        break;
    case IN_AUDIO_DELAY: 
        g_value_set_int(value, agoraIO->in_audio_delay);
        break;
    case IN_VIDEO_DELAY: 
        g_value_set_int(value, agoraIO->in_video_delay);
        break;
    case OUT_AUDIO_DELAY: 
        g_value_set_int(value, agoraIO->out_audio_delay);
        break;
    case OUT_VIDEO_DELAY: 
        g_value_set_int(value, agoraIO->out_video_delay);
        break;
    case PROXY:
        g_value_set_boolean (value, agoraIO->proxy);
       break;
    case OPERATIONAL_MODE: 
        g_value_set_int(value, agoraIO->mode);
        break;
    case PROXY_CONNECT_TIMEOUT: 
        g_value_set_int(value, agoraIO->reconnect_timeout);
        break;
    case PROXY_IPS:
        g_value_set_string (value, agoraIO->proxy_ips);
        break;
    case RECEIVE_VIDEO:
        g_value_set_boolean (value, agoraIO->receive_video);
        break;
    case AUDIO_PCM:
        g_value_set_boolean (value, agoraIO->audio_pcm);
        break;
    case AGORA_PARAMS:
        g_value_set_string (value, agoraIO->agora_params);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
  }
}

static void
gst_agoraioudp_finalize (GObject * object)
{
  Gstagoraioudp *agoraIO = GST_AGORAIOUDP (object);

  gst_agoraioudp_teardown (agoraIO);
  g_mutex_clear (&agoraIO->ctx_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
agoraioudp_init (GstPlugin * agoraioudp)
{
  /* debug category for fltering log messages
   *
   * exchange the string 'Template agoraioudp' with your description
   */
  GST_DEBUG_CATEGORY_INIT (gst_agoraioudp_debug, "agoraioudp",
      0, "Template agoraioudp");

  return gst_element_register (agoraioudp, "agoraioudp", GST_RANK_NONE,
      GST_TYPE_AGORAIOUDP);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "agoraioudp"
#endif

/* gstreamer looks for this structure to register agoraioudps
 *
 * exchange the string 'Template agoraioudp' with your agoraioudp description
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    agoraioudp,
    "agoraioudp",
    agoraioudp_init,
    PACKAGE_VERSION,
    GST_LICENSE,
    GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN
)
     

