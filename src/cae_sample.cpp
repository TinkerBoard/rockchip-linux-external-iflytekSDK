/* =========================================================================

   DESCRIPTION
   cae_demo

   Copyright (c) 2016 by iFLYTEK, Co,LTD.  All Rights Reserved.
   ============================================================================ */

/* =========================================================================

   REVISION

   when            who              why
   --------        ---------        -------------------------------------------
   2016/10/09      cyhu             Created.
   ============================================================================ */

/* ------------------------------------------------------------------------
 ** Includes
 ** ------------------------------------------------------------------------ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <alsa/asoundlib.h>
#include "cae_intf.h"
#include "cae_lib.h"
#include "cae_errors.h"
#include "speech_conversation.h"
#include "InfoLed.h"

//#define DEBUG_WRITE_DATA
#define  INPUT_FILE "test.pcm"
#define  PIECE_LEN  12288 
#define SAVE_CAE_OUT_PCM 1
#define API_WAKEUP_ENABLE 0
#define API_WRITE_AUDIO 1
#define API_SINGLE_MIC 0

void test_led_on(std::shared_ptr<InfoLed> infoled, int color) {
    infoled->leds_multi_all_on(color);
}

void test_led_scroll(std::shared_ptr<InfoLed> infoled, uint64_t bitmap_color, uint32_t color, uint32_t  bg_color, uint32_t shift, uint32_t delay_ms) {
    infoled->leds_multi_set_scroll(bitmap_color, color, bg_color, shift, delay_ms);
}

void test_led_off(std::shared_ptr<InfoLed> infoled) {
    infoled->leds_multi_all_off();
}

void test_led_num(std::shared_ptr<InfoLed> infoled,int vol) {
    infoled->leds_multi_certain_on((1 << vol) -1, LED_MULTI_PURE_COLOR_WHITE); 
}

typedef struct _CAEUserData{
    FILE *fp_out;
    std::shared_ptr<InfoLed> infoled;
}CAEUserData;

static Proc_CAENew api_cae_new;
static Proc_CAEAudioWrite api_cae_audio_write;
static Proc_CAEResetEng api_cae_reset_eng;
static Proc_CAESetRealBeam api_cae_set_real_beam;
static Proc_CAESetWParam api_cae_set_wparam;
static Proc_CAEGetWParam api_cae_get_wparam;	
static Proc_CAEGetVersion api_cae_get_version;
static Proc_CAEGetChannel api_cae_get_channel;
static Proc_CAESetShowLog api_cae_set_show_log;
static Proc_CAEDestroy api_cae_destroy;

static FILE * finput;
CAE_HANDLE cae = NULL;

int isFirstSample = 1;
int isWakeUp = 0;
static void CAEIvwCb(short angle, short channel, float power, short CMScore, short beam, char *param1, void *param2, void *userData)
{
    printf("\nCAEIvwCb ....\nangle:%d\n  param1:%s\n", angle, param1);
    CAEUserData *usDta = (CAEUserData*)userData;
    if(isWakeUp == 0) {
        isFirstSample = 1;
        isWakeUp = 1;
    }
    test_led_num(usDta->infoled,(beam+1));

}

static void CAEAudioCb(const void *audioData, unsigned int audioLen, int param1, const void *param2, void *userData)
{
    CAEUserData *usDta = (CAEUserData*)userData;
    char tmpData[1024];
    if(isWakeUp) {
        memcpy(tmpData,(char*)audioData,audioLen);
        int audio_write_res = conversation_write_in_data(tmpData,audioLen,isFirstSample);
        if(audio_write_res == -1||audio_write_res == 1) {
            isWakeUp = 0;
            test_led_off(usDta->infoled);
        }
        isFirstSample = 0;
    }
}

static int initFuncs()
{
    int ret = MSP_SUCCESS;
    const char* libname = "/usr/lib/libcae.so";
    void* hInstance = cae_LoadLibrary(libname);

    if(hInstance == NULL)
    {
        printf("Can not open library!\n");
        return MSP_ERROR_OPEN_FILE;
    }
    api_cae_new = (Proc_CAENew)cae_GetProcAddress(hInstance, "CAENew");
    api_cae_audio_write = (Proc_CAEAudioWrite)cae_GetProcAddress(hInstance, "CAEAudioWrite");
    api_cae_reset_eng = (Proc_CAEResetEng)cae_GetProcAddress(hInstance, "CAEResetEng");
    api_cae_set_real_beam = (Proc_CAESetRealBeam)cae_GetProcAddress(hInstance, "CAESetRealBeam");
    api_cae_set_wparam = (Proc_CAESetWParam)cae_GetProcAddress(hInstance, "CAESetWParam");
	api_cae_get_version = (Proc_CAEGetVersion)cae_GetProcAddress(hInstance, "CAEGetVersion");
	api_cae_get_channel= (Proc_CAEGetChannel)cae_GetProcAddress(hInstance, "CAEGetChannel");
	api_cae_destroy = (Proc_CAEDestroy)cae_GetProcAddress(hInstance, "CAEDestroy");
	return ret;
}

int  mChannels = 8;
int  mIs24bitMode = 0;
int preProcessBuffer(void *data, void *out, int bytes)
{
    int i = 0, j = 0;

    if (!mIs24bitMode) {
        for (i = 0; i < bytes / 2 ; ) {
            for (j = 0; j < mChannels; j++) {
                int tmp = 0;
                short tmp_data = (*((short *)data + i + j));
                tmp = ((tmp_data) << 16 | ((j+1) << 8)) & 0xffffff00;
                *((int *)out + i + j) = tmp;
            }
            i += mChannels;
        }
    } else {
        for (i = 0; i < bytes / 4 ; ) {
            for (j = 0; j < mChannels; j++) {
                int tmp = 0;
                int tmp_data = (*((int *)data + i + j)) << 1;
                tmp = ((((tmp_data & 0xfffff000) >> 12) << 12) | ((j+1) << 8)) & 0xffffff00;
                *((int *)out + i + j) = tmp;
            }
            i += mChannels;
        }
    }
    return 0;
}

void alsa_open(snd_pcm_t** capture_handle,int channels,uint32_t rate,snd_pcm_format_t format) {
    snd_pcm_hw_params_t *hw_params;
    int err;
    if ((err = snd_pcm_open(capture_handle, "default", SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        fprintf(stderr, "cannot open audio device %s (%s)\n",
                snd_strerror(err));
        exit(1);
    }

    if ((err = snd_pcm_hw_params_malloc(&hw_params)) < 0) {
        fprintf(stderr, "cannot allocate hardware parameter structure (%s)\n",
                snd_strerror(err));
        exit(1);
    }

    if ((err = snd_pcm_hw_params_any(*capture_handle, hw_params)) < 0) {
        fprintf(stderr, "cannot initialize hardware parameter structure (%s)\n",
                snd_strerror(err));
        exit(1);
    }

    if ((err = snd_pcm_hw_params_set_access(*capture_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        fprintf(stderr, "cannot set access type (%s)\n",
                snd_strerror(err));
        exit(1);
    }

    if ((err = snd_pcm_hw_params_set_format(*capture_handle, hw_params, format)) < 0) {
        fprintf(stderr, "cannot set sample format (%s)\n",
                snd_strerror(err));
        exit(1);
    }

    if ((err = snd_pcm_hw_params_set_channels(*capture_handle, hw_params, channels)) < 0) {
        fprintf(stderr, "cannot set channel count (%s)\n",
                snd_strerror(err));
        exit(1);
    }

    if ((err = snd_pcm_hw_params_set_rate_near(*capture_handle, hw_params, &rate, 0)) < 0) {
        fprintf(stderr, "cannot set sample rate (%s)\n",
                snd_strerror(err));
        exit(1);
    }

    if ((err = snd_pcm_hw_params(*capture_handle, hw_params)) < 0) {
        fprintf(stderr, "cannot set parameters (%s)\n",
                snd_strerror(err));
        exit(1);
    }
    snd_pcm_hw_params_free(hw_params);
}

void system_pause() {
    system("echo mem > /sys/power/state");
}

bool system_command(const char* cmd) {
    pid_t status = 0;
    bool ret_value = false;

    //fprintf(stderr,"System [%s]\n", cmd);

    status = system(cmd);

    if (-1 == status) {
        fprintf(stderr,"system failed!\n");
    } else {
        if (WIFEXITED(status)) {
            if (0 == WEXITSTATUS(status)) {
                ret_value = true;
            } else {
                fprintf(stderr,"System shell script failed:[%d].", WEXITSTATUS(status));
            }
        } else {
            //fprintf(stderr,"System status = [%d]\n", WEXITSTATUS(status));
        }
    }

    return ret_value;
}

int main(int argc, char *argv[])
{
    int i,  n = 0, buffer_frames = 1024, channels = 8;
    size_t BUFSIZE = (int)(buffer_frames * snd_pcm_format_width(SND_PCM_FORMAT_S16_LE) / 8 * channels);
    size_t b = BUFSIZE;
    char buffer[b];
    snd_pcm_t *capture_handle;
    unsigned int rate = 16000;
    snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;
    int err;

    int ret = MSP_SUCCESS;
    const char *resPath = "/etc/ivw_resource.jet";
    static CAEUserData userData;
    unsigned int pcmSize = 0;
    int totalsize = 0;
    int onec_szie = 0;
    char *pcmBuf = NULL;

    int ledNumber;
    auto infoled = std::make_shared<InfoLed>();

    //connect wifi
    system_command("ifconfig wlan0 0.0.0.0");
    system_command("killall dhcpcd");
    system_command("killall wpa_supplicant");
    sleep(1);
    system_command("wpa_supplicant -Dnl80211 -B -i wlan0 -c /data/cfg/wpa_supplicant.conf");
    system_command("dhcpcd -k wlan0");//udhcpc -b -i wlan0 -q ");
    sleep(1);
    system_command("dhcpcd wlan0");

    /**
     *
     * enum {
     *     LED_MULTI_PURE_COLOR_GREEN = 0,
     *     LED_MULTI_PURE_COLOR_RED,
     *     LED_MULTI_PURE_COLOR_BLUE,
     *     LED_MULTI_PURE_COLOR_WHITE,
     *     LED_MULTI_PURE_COLOR_BLACK,
     *     LED_MULTI_PURE_COLOR_NON_GREEN,
     *     LED_MULTI_PURE_COLOR_NON_RED,
     *     LED_MULTI_PURE_COLOR_NON_BLUE,
     *     LED_MULTI_PURE_COLOR_MAX,
     * };
     *
     */
    ledNumber = infoled->leds_multi_init();
    if(ledNumber < 0) {
        printf("led init failed\n");
        return -1;
    }

    test_led_on(infoled, LED_MULTI_PURE_COLOR_RED);

    //cae init
    if(initFuncs() != MSP_SUCCESS)
    {
        printf("load cae library failed\n");	
        return -1;
    }

    printf("api_cae_new in\n");
    userData.infoled = infoled;
    ret = api_cae_new(&cae, resPath, CAEIvwCb, NULL, CAEAudioCb, NULL, &userData);
    if (MSP_SUCCESS != ret)
    {
        printf("CAENew ....failed\n");
        ret = MSP_ERROR_FAIL;
        return ret;
    }
    printf("api_cae_new out\n");

    int supportChannel = api_cae_get_channel();
    printf("\n CAE current support channel num is : %d\n", supportChannel);

    alsa_open(&capture_handle,channels,rate,format);

    if ((err = snd_pcm_prepare(capture_handle)) < 0) {
        fprintf(stderr, "cannot prepare audio interface for use (%s)\n",
                snd_strerror(err));
        exit(1);
    }

    char temp[BUFSIZE*2];
    int count = 10;
    FILE *file = fopen("/tmp/vad_before.pcm","a+");
    fprintf(stderr,"write vad before data\n");

    int readed_frame;
    while ((readed_frame = snd_pcm_readi(capture_handle, buffer, buffer_frames)) >= 0 && count-- > 0)
    {		
        b = (int)(readed_frame * snd_pcm_format_width(SND_PCM_FORMAT_S16_LE) / 8 * channels);
#ifdef DEBUG_WRITE_DATA
        fwrite(buffer,1,b,file);
#endif
    }
    fclose(file);
    snd_pcm_close(capture_handle);

    test_led_off(infoled);

    //system_pause();

    test_led_on(infoled, LED_MULTI_PURE_COLOR_BLUE);

    alsa_open(&capture_handle,channels,rate,format);
    if ((err = snd_pcm_prepare(capture_handle)) < 0) {
        fprintf(stderr, "cannot prepare audio interface for use (%s)\n",
                snd_strerror(err));
        exit(1);
    }

    file = fopen("/tmp/vad.pcm","a+");
    fprintf(stderr,"write vad data\n");
    while ((readed_frame = snd_pcm_readi(capture_handle, buffer, buffer_frames)) >= 0)
    {		
        b = (int)(readed_frame * snd_pcm_format_width(SND_PCM_FORMAT_S16_LE) / 8 * channels);
#ifdef DEBUG_WRITE_DATA
        fwrite(buffer,1,b,file);
#endif
        preProcessBuffer(buffer, temp, BUFSIZE);
        err = api_cae_audio_write(cae, temp, BUFSIZE*2);
        if(err < 0) {
            err = api_cae_audio_write(cae, temp, BUFSIZE*2);
            printf("try again err:%d\n",err);
        }
    }
    fclose(file);
}
