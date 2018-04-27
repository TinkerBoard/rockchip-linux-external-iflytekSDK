/*
* ——语音听写(iFly Auto Transform,iat)技术能够实时地将语音转换成对应的文字
* ——文本语义技术能将文本内容进行语义解析
* ——语音合成(Text To Speech,tts)技术能够自动将任意文字实时转换为连续的自然语音
*
* 示例Demo融合语音听写、文字语义与语音合成，输入为麦克风中的采集语音，输出为语音云端返回的语音反馈数据，分步骤如下：
* 语音听写: 从麦克风中采集声音送到语音云处理,并返回文字。
* 文字语义：将语音听写(iat)返回的文字信息进行语义处理，并返回解析后的文本信息
* 语音合成：将文字语义中返回的文字信息送到云端识别并返回声音。
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include "speech_conversation.h"
#include "ifly_auto_transform.h"
#include "text_to_speech.h"
#include "msp_cmn.h"
#include "msp_errors.h"
#include "cJSON.h"


static int is_login_in = 0;

int ret = MSP_SUCCESS;
/* 登录参数，appid与msc库绑定,请勿随意改动 */
const char* login_params = "appid = 58bcdd98, work_dir = ."; 
const char* iat_session_id = NULL; 
const char* text_from_audio_data = NULL;

const char* iat_session_begin_params =
	"sub = iat, domain = iat, language = zh_cn, "
	"accent = mandarin, sample_rate = 16000, "
	"result_type = plain, result_encoding = utf8";
/*
* rdn:           合成音频数字发音方式
* volume:        合成音频的音量
* pitch:         合成音频的音调
* speed:         合成音频对应的语速
* voice_name:    合成发音人
* sample_rate:   合成音频采样率
* text_encoding: 合成文本编码格式
*
* 详细参数说明请参阅《讯飞语音云MSC--API文档》
*/
const char* tts_session_begin_params = "voice_name = xiaoyan, text_encoding = utf8, sample_rate = 16000, speed = 50, volume = 50, pitch = 50, rdn = 2";
const char* filename   = "/tmp/example.wav"; //合成的语音文件名称

/*
 * 从语音云json数据中返回所关心的数据结果
 */
const char* parse_json_result(const char* json_string)
{
	const char *result;
	if(NULL == json_string)
    {
        return NULL;
    }
    cJSON *pJson = cJSON_Parse(json_string);
    if(NULL == pJson)                                                                                         
    {
 	    return NULL;
   	}
    cJSON *pAnswer = cJSON_GetObjectItem(pJson, "answer");
    if(NULL == pAnswer)
    {
  		result = "未找到问题答案";
  		return result;
    }
    cJSON *pAnswer_text = cJSON_GetObjectItem(pAnswer, "text");
    result = pAnswer_text->valuestring;
    printf("\n转换JOSN结果：%s\n",result);
    return result;
}

/**
 *  根据audio_text进行一次语音交互,交互成功返回1，否则返回0
 */
int voice_conversation_from_audio_text(const char* audio_text)
{
	int ret = MSP_SUCCESS;
	int audio_text_length =	0;
	const char* session_rec_text = NULL; // 语音交互返回的文本数据,格式为JSON
	const char* json_answer_text = NULL; // JSON数据中获取到的answer字段数据

	audio_text_length = strlen(audio_text);

	session_rec_text = MSPSearch("nlp_version=2.0",audio_text,&audio_text_length,&ret);
	if(MSP_SUCCESS !=ret)
	{
		printf("MSPSearch failed ,error code is:%d\n",ret);
		return 0;
	}
	printf("\n语义解析完成,解析结果为：%s\n",session_rec_text);

	// 解析获取到的JSON数据
	json_answer_text = parse_json_result(session_rec_text);

	ret = text_to_speech(json_answer_text,filename,tts_session_begin_params);
	if (MSP_SUCCESS != ret)
	{
		printf("text_to_speech failed, error code: %d.\n", ret);
		return 0;
	}
	system("aplay /tmp/example.wav");
	remove(filename);
    return 1;
}

int play_text_speak(const char* text)
{
	int ret = MSP_SUCCESS;
	ret = text_to_speech(text,filename,tts_session_begin_params);
	if (MSP_SUCCESS != ret)
	{
		printf("text_to_speech failed, error code: %d.\n", ret);
		return 0;
	}
	system("aplay /tmp/example.wav");
	remove(filename);
    return 1;
}

int conversation_write_in_data(char* sample_data,int sample_length,int is_first_sample)
{
	int res = 0;

	if(!is_login_in)
	{
		ret = MSPLogin(NULL, NULL, login_params);
		if (MSP_SUCCESS != ret)	{
			printf("MSPLogin failed , Error code %d.\n",ret);
			goto exit; // 登录失败，exit
		}
		is_login_in = 1;
	}
	
	if(is_first_sample)
	{
		iat_session_id = iat_session_begin(iat_session_id,iat_session_begin_params);
		if(iat_session_id == NULL)
		{
			goto exit;
		}
	}

	res = write_audio_data(iat_session_id,sample_data,sample_length,is_first_sample);
	if(res == IAT_RESULT_COMPLETE)   // 音频数据传输完成.结果可见
	{
		pthread_t thread;
		text_from_audio_data = get_iat_result();
		res = pthread_create(&thread,NULL,(void*)voice_conversation_from_audio_text,(void*)text_from_audio_data);
		if(ret!=0)  
    	{  
        	printf("Create pthread error!\n");  
    		goto exit;
    	}  
		return 1;
	}
	if(res == IAT_RESULT_VAD)   //  VAD退出
	{
		pthread_t thread;
		text_from_audio_data = "没听到声音,请重试";
		res = pthread_create(&thread,NULL,(void*)play_text_speak,(void*)text_from_audio_data);
		if(ret!=0)  
    	{  
        	printf("Create pthread error!\n");  
    		goto exit;  
    	}  
		return 1;
	}
	return 0;
exit:
	MSPLogout(); // Login out...
	return -1;
}
