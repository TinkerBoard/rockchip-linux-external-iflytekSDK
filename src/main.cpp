/* =========================================================================

   DESCRIPTION
   iflytek SDK Demo

   Copyright (c) 2016 by iFLYTEK, Co,LTD.  All Rights Reserved.
   ============================================================================ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <alsa/asoundlib.h>

#include "cae_intf.h"
#include "cae_lib.h"
#include "cae_errors.h"

#include "constant.h"
#include "info_led.h"
#include "wpa_manager.h"
#include "audio_process.h"
#include "log.h"

#define SAVE_CAE_OUT_PCM 0

static int initCaeFuncs()
{
    int ret = MSP_SUCCESS;
    void* hInstance = cae_LoadLibrary(Constant::kCaeLibPATH);

    if (hInstance == NULL) {
        Log::error("load cae library failed with path: %s\n", Constant::kCaeLibPATH);
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

/**
 * The callback when the cae wakes up successfully.
 * The parameters below carrys all kinds of audio data.
 *
 * @brief CAEIvwCb
 * @param angle
 * @param channel
 * @param power
 * @param CMScore
 * @param beam
 * @param param1
 * @param param2
 * @param userData
 */
static void CAEIvwCb(short angle, short channel, float power,
                     short CMScore, short beam, char *param1,
                     void *param2, void *userData)
{
    Log::debug("CAEIvwCb .... angle:%d, channel:%d, power:%f, CMScore: %d, beam: %d\n",
               angle, channel, power, CMScore, beam);

    CAEUserData *usDta = (CAEUserData*)userData;

    // Check network status
    if (WPAManager::getInstance()->isNetConnected()) {
        if (!usDta->wakeUpState) {
            usDta->newSession = true;
            usDta->wakeUpState = true;
        }

        // Led_Style show
        usDta->infoled->leds_multi_all_blink(LED_MULTI_PURE_COLOR_BLUE, 500, 500);
    } else {
        // TODO Network disconnected
    }
}

/**
 * The callback when audioData come in after the wake_up.
 *
 * @brief CAEAudioCb
 * @param audioData
 * @param audioLen
 * @param param1
 * @param param2
 * @param userData
 */
static void CAEAudioCb(const void *audioData, unsigned int audioLen,
                       int param1, const void *param2, void *userData)
{
    CAEUserData *usDta = (CAEUserData*)userData;
    bool audioPadding;

    if (usDta->wakeUpState) {
        audioPadding = AudioProcess::paddingIatAudio((char*)audioData, audioLen, usDta);

        if (!audioPadding) {
            usDta->wakeUpState = false;
            usDta->infoled->leds_multi_all_off();
        }

        usDta->newSession = false;
    }

#if SAVE_CAE_OUT_PCM
    fwrite(audioData, audioLen, 1, usDta->fp_out);
#endif
}

void alsa_open(snd_pcm_t** capture_handle, int channels,
               uint32_t rate, snd_pcm_format_t format)
{
    snd_pcm_hw_params_t *hw_params;
    int err;

    if ((err = snd_pcm_open(capture_handle, "default",
                            SND_PCM_STREAM_CAPTURE, 0)) < 0)
    {
        fprintf(stderr, "cannot open audio device %s\n",
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

    if ((err = snd_pcm_hw_params_set_access(*capture_handle, hw_params,
                                            SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
    {
        fprintf(stderr, "cannot set access type (%s)\n",
                snd_strerror(err));
        exit(1);
    }

    if ((err = snd_pcm_hw_params_set_format(*capture_handle,
                                            hw_params, format)) < 0)
    {
        fprintf(stderr, "cannot set sample format (%s)\n",
                snd_strerror(err));
        exit(1);
    }

    if ((err = snd_pcm_hw_params_set_channels(*capture_handle,
                                              hw_params, channels)) < 0)
    {
        fprintf(stderr, "cannot set channel count (%s)\n",
                snd_strerror(err));
        exit(1);
    }

    if ((err = snd_pcm_hw_params_set_rate_near(*capture_handle,
                                               hw_params, &rate, 0)) < 0)
    {
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

int preProcessBuffer(void *data, void *out, int bytes)
{
    int channels = 8;
    int is24bitMode = 0;
    int i = 0, j = 0;

    if (!is24bitMode) {
        for (i = 0; i < bytes / 2; ) {
            for (j = 0; j < channels; j++) {
                int tmp = 0;
                short tmp_data = (*((short *)data + i + j));
                tmp = ((tmp_data) << 16 | ((j+1) << 8)) & 0xffffff00;
                *((int *)out + i + j) = tmp;
            }
            i += channels;
        }
    } else {
        for (i = 0; i < bytes / 4; ) {
            for (j = 0; j < channels; j++) {
                int tmp = 0;
                int tmp_data = (*((int *)data + i + j)) << 1;
                tmp = ((((tmp_data & 0xfffff000) >> 12) << 12) | ((j+1) << 8)) & 0xffffff00;
                *((int *)out + i + j) = tmp;
            }

            i += channels;
        }
    }

    return 0;
}

/**
 * CAEAudioWrite
 *
 * @brief caeAudioWrite
 * @param cae
 */
void caeAudioWrite(CAE_HANDLE cae)
{
    int capture_len = 1024, channels = 8, err;
    size_t BUFSIZE = (int)(capture_len * snd_pcm_format_width(SND_PCM_FORMAT_S16_LE) / 8 * channels);
    char buffer[BUFSIZE];

    snd_pcm_t *capture_handle;
    snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;
    unsigned int rate = 16000;
    int frame_len;
    size_t buffer_len;
    char cae_audio_data[BUFSIZE * 2];

    alsa_open(&capture_handle, channels, rate, format);
    if ((err = snd_pcm_prepare(capture_handle)) < 0) {
        fprintf(stderr, "cannot prepare audio interface for use (%s)\n",
                snd_strerror(err));
        exit(1);
    }

    while ((frame_len = snd_pcm_readi(capture_handle, buffer, capture_len)) >= 0) {
        buffer_len = (int)(frame_len * snd_pcm_format_width(SND_PCM_FORMAT_S16_LE) / 8 * channels);
        preProcessBuffer(buffer, cae_audio_data, buffer_len);

        err = api_cae_audio_write(cae, cae_audio_data, buffer_len * 2);
        if (err != 0)
            Log::error("CAEAudioWrite with errCode:%d\n", err);
    }
}

int main(int argc, char *argv[])
{
    static CAEUserData userData;
    int ret = MSP_SUCCESS;
    int leds_num = 0;
    CAE_HANDLE cae = NULL;

    userData.wakeUpState = false;
    userData.newSession = false;

    /* Initialize Log class */
    Log::init("iflytec_SDK_Demo", true);

#if SAVE_CAE_OUT_PCM
    userData.fp_out = fopen(Constant::kCaeOutPcmPath, "wb");
    if (userData.fp_out != NULL)
        Log::error("fopen %d file ....failed\n", Constant::kCaeOutPcmPath);
#endif

    // RK_Led multi_init
    auto infoled = std::make_shared<InfoLed>();
    leds_num = infoled->leds_multi_init();
    if (leds_num <= 0) {
        Log::error("RKLed init failed\n");
        return -1;
    }
    userData.infoled = infoled;
    Log::info("RK_Led multi_init success with leds_num: %d\n", leds_num);

    /**
     * Load cae_library and initialize functions for project cae operations
     */
    if (initCaeFuncs() != MSP_SUCCESS) {
        Log::error("initCaeFuncs() failed\n");
        return -1;
    }

    ret = api_cae_new(&cae, Constant::kIwvResPath, CAEIvwCb, NULL, CAEAudioCb, NULL, &userData);
    if (ret != MSP_SUCCESS) {
        Log::error("CAENew ... failed!\n");
        return -1;
    }

    if (!AudioProcess::MSP_Login(Constant::kMSCLoginParams)) {
        return -1;
    }

    Log::info("CAENew success and Write_Cae_Audio_Data begin\n");
    caeAudioWrite(cae);

    return 0;
}
