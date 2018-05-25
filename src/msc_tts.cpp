#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <alsa/asoundlib.h>

#include "msc_tts.h"
#include "log.h"
#include "qtts.h"
#include "msp_cmn.h"
#include "msp_errors.h"
#include "constant.h"

/**
 * @brief TTSProcess::textToSpeech
 * @param text
 * @return success
 */
bool TTSProcess::textToSpeech(const char* text)
{
    int ret;
    const char* sessionID  = NULL;
    unsigned int audio_len  = 0;
    int  synth_status = MSP_TTS_FLAG_STILL_HAVE_DATA;

    if (text == NULL) {
        Log::info("Params is error!\n");
        return false;
    }

    sessionID = QTTSSessionBegin(Constant::kTTSsessionParams, &ret);
    if (ret != MSP_SUCCESS) {
        Log::info("QTTSSessionBegin failed errCode: %d.\n", ret);
        return false;
    }

    ret = QTTSTextPut(sessionID, text, (unsigned int)strlen(text), NULL);
    if (ret != MSP_SUCCESS) {
        Log::error("QTTSTextPut failed, error code: %d.\n", ret);
        QTTSSessionEnd(sessionID, "TextPutError");
        return false;
    }

    Log::debug("TTSProcess QTTSTextPut put: %s\n", text);

    int err;
    snd_pcm_t *handle;
    snd_pcm_sframes_t frames;
    const char *device = "default";

    if ((err = snd_pcm_open(&handle, device, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        Log::error("Playback open error: %s\n", snd_strerror(err));
        return false;
    }

    if ((err = snd_pcm_set_params(handle, SND_PCM_FORMAT_S16_LE,
                                  SND_PCM_ACCESS_RW_INTERLEAVED, 1, 16000, 1, 500000)) < 0)
    {
        Log::error("playback open error: %s\n", snd_strerror(err));
        return false;
    }

    while (1) {
        const void* data = QTTSAudioGet(sessionID, &audio_len, &synth_status, &ret);
        if (ret != MSP_SUCCESS) {
            Log::error("QTTSAudioGet failed \n");
            break;
        }

        uint32_t frame_size = audio_len / 2; //16bit
        if (data != NULL) {
            frames = snd_pcm_writei(handle, data, frame_size);
            if (frames < 0) {
                frames = snd_pcm_recover(handle, frames, 0);
            }

            if (frames < 0) {
                Log::error("snd_pcm_writei failed: %s\n", snd_strerror(frames));
                break;
            }

            if (frames > 0 && frames < (long)frame_size) {
                Log::error("Short write (expected %li, wrote %li)\n", (long)frame_size, frames);
            }
        }

        if (synth_status == MSP_TTS_FLAG_DATA_END)
            break;

        usleep(150 * 1000); // Prevent frequent CPU usage
    }

    usleep(1000 * 1000);

    // release
    snd_pcm_close(handle);
    QTTSSessionEnd(sessionID, "Normal");

    return true;
}
