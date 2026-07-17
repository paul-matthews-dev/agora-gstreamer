#ifndef _AGORA_CONFIG_H_
#define _AGORA_CONFIG_H_

typedef void (*event_fn)(void* userData,
                         int type,
                         const char* userName,
                         long param1,
                         long param2,
                         long* states);

typedef struct{
    char*           app_id;
    char*           ch_id;
    char*           user_id;
    bool            verbose;
    event_fn        fn;
    void*           userData;
    int             in_audio_delay;
    int             in_video_delay;
    int             out_audio_delay;
    int             out_video_delay;
    int             sendOnly;
    int             enableProxy;
    int             proxy_timeout;
    char*           proxy_ips;
    bool            receive_video;   /* subscribe to remote video */
}agora_config_t;

#endif
