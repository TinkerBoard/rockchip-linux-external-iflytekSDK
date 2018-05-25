#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <thread>

#include "audio_process.h"
#include "log.h"
#include "msc_iat.h"
#include "msc_tts.h"
#include "constant.h"
#include "qisr.h"
#include "msp_cmn.h"
#include "msp_errors.h"
#include "cJSON.h"

const char* AudioProcess::iatSessionId = NULL;

const char* parseJsonStr(const char* JsonStr)
{
    const char *sAnswer;

    cJSON *pJson = cJSON_Parse(JsonStr);
    if (pJson == NULL) {
        Log::error("cJSON_Parse failed\n");
        return NULL;
    }

    cJSON *pAnswer = cJSON_GetObjectItem(pJson, "answer");
    if (pAnswer != NULL) {
        cJSON *pAnswer_text = cJSON_GetObjectItem(pAnswer, "text");
        sAnswer = pAnswer_text->valuestring;
    } else {
        sAnswer = Constant::kJsonQuestionNotFound;
    }

    return sAnswer;
}

/**
 * Semantic parsing
 *
 * @brief MSP_Process
 * @param src_text
 * @return
 */
bool MSP_Process(const char* iatResult)
{
    int ret = MSP_SUCCESS;
    const char* jsonStr = NULL;
    const char* sAnswer = NULL;
    unsigned int strLen =  0;

    strLen = strlen(iatResult);
    jsonStr = MSPSearch("nlp_version=2.0", iatResult, &strLen, &ret);
    if (ret != MSP_SUCCESS) {
        Log::error("MSPSearch failed errCode:%d\n", ret);
        return false;
    }

    Log::debug("Semantic parsing finished with JsonStr: %s\n", jsonStr);
    sAnswer = parseJsonStr(jsonStr);
    Log::debug("parse_Json_Str: %s\n", sAnswer);

    ret = TTSProcess::textToSpeech(sAnswer);
    if (!ret) {
        Log::error("TTSProcess::textToSpeech() failed, error code: %d.\n", ret);
        return false;
    }

    return true;
}

void textToSpeech(const char* text)
{
    TTSProcess::textToSpeech(text);
}

bool AudioProcess::MSP_Login(const char *loginParams)
{
    int ret;

    ret = MSPLogin(NULL, NULL, loginParams);
    if (ret != MSP_SUCCESS) {
        Log::error("MSPLogin failed\n");
        MSPLogout();
        return false;
    }

    return true;
}

/**
 *  Padding audio data from the cae into the isr
 *  _ QISRAudioWrite()
 *
 * @brief AudioProcess::paddingIatAudio
 * @param audioData
 * @param audioLen
 * @param userData
 * @return isContinue
 */
bool AudioProcess::paddingIatAudio(const char* audioData, unsigned int audioLen,
                                   CAEUserData *userData)
{
    IatState iatState;
    bool isContinue = true;

    if (userData->newSession) {
        AudioProcess::iatSessionId = IatProcess::beginQISRSession(Constant::kIatSessionParams);
        if (AudioProcess::iatSessionId == NULL) {
            Log::error("IatProcess::beginQISRSession faied\n");
            return false;
        }
    }

    // Write audio data and get returned for communicate with thread_semantic
    iatState = IatProcess::writeISRAudioData(AudioProcess::iatSessionId, audioData,
                                             audioLen, userData->newSession);

    switch (iatState) {
    case IAT_STATE_ERROR:
        isContinue = false;
        Log::debug("=> IAT_STATE_ERROR \n");
        break;
    case IAT_STATE_VAD:
    {
        std::thread thread(textToSpeech, Constant::kMSCVadPrompt);
        thread.detach();
        isContinue = false;
        break;
    }
    case IAT_STATE_COMPLETE:
    {
        Log::debug("IatResult available: %s\n", IatProcess::iatResult);
        // Semantic parsing process and stop transmitting data
        std::thread thread(MSP_Process, IatProcess::iatResult);
        thread.detach();
        isContinue = false;
        break;
    }
    default:
        isContinue = true;
        break;
    }

    return isContinue;
}
