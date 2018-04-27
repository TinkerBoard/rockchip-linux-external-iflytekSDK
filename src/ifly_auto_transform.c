/*
 * 语音听写(iFly Auto Transform)技术能够实时地将语音转换成对应的文字。
 * 本程序用于将麦克风输入的语音输出成文本
 */

#include "ifly_auto_transform.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "qisr.h"
#include "msp_cmn.h"
#include "msp_errors.h"
#include "speech_recognizer.h"


#define HINTS_SIZE  100
#define FRAME_LEN	640 
#define	BUFFER_SIZE	4096

static char *g_result = NULL;
static unsigned int g_buffersize = BUFFER_SIZE;
/* 麦克风的一次输入等待时间在本程序中设定为15s,flag用于有数据返回时从该15s等待时间中返回 */
static int is_mic_data_ready = 0;    
/* 标识是否检测到静音时，检测到静音时置为1从15S的等待时间中返回 */
static int is_vad_end_come = 0;

static void show_result(char *string, char is_over)
{
	printf("\rResult: [ %s ]", string);
	if(is_over)
	{
		putchar('\n');
	}
}

void on_result(const char *result, char is_last)
{
	if (result) {
		size_t left = g_buffersize - 1 - strlen(g_result);
		size_t size = strlen(result);
		if (left < size) {
			g_result = (char*)realloc(g_result, g_buffersize + BUFFER_SIZE);
			if (g_result)
				g_buffersize += BUFFER_SIZE;
			else {
				printf("mem alloc failed\n");
				return;
			}
		}
		strncat(g_result, result, size);
		show_result(g_result, is_last);
		is_mic_data_ready = 1;
	}
}

void on_speech_begin()
{
	if (g_result)
	{
		free(g_result);
	}
	g_result = (char*)malloc(BUFFER_SIZE);
	g_buffersize = BUFFER_SIZE;
	memset(g_result, 0, g_buffersize);

	printf("Start Listening...\n");
}

void on_speech_end(int reason)
{
	if (reason == END_REASON_VAD_DETECT)
	{	
		is_vad_end_come = 1;
		printf("\nSpeaking done \n");
	}
	else
	{
		printf("\nRecognizer error %d\n", reason);
	}
}

/**
 *  将麦克风输入的语音转换为对应的文字
 */
char* mic_ifly_to_text(const char* session_begin_params)
{
	is_mic_data_ready = 0;
	is_vad_end_come = 0;

	int errcode;
	int i = 0;

	struct speech_rec iat;

	struct speech_rec_notifier recnotifier = {
		on_result,
		on_speech_begin,
		on_speech_end
	};

	errcode = sr_init(&iat, session_begin_params, SR_MIC, &recnotifier);
	if (errcode) {
		printf("speech recognizer init failed\n");
		return NULL;
	}
	errcode = sr_start_listening(&iat);
	if (errcode) {
		printf("start listen failed %d\n", errcode);
		return NULL;
	}

	// 等待语音输入时间为15,在语音数据到来时,置标记is_mic_data_ready为1提前退出
	while(i++ < 15)
	{
		printf("%d\n",i);
		sleep(1);
		if(is_mic_data_ready|is_vad_end_come)
		{
			break;
		}
	}

	errcode = sr_stop_listening(&iat);
	if (errcode) {
		printf("stop listening failed %d\n", errcode);
		return NULL;
	}

	sr_uninit(&iat);
	if(!is_mic_data_ready)
	{
		printf("\nno volume data come in!!!\n");
		return NULL;
	}
	return g_result;
}

/**
 *  开始一次读取会话，成功则返回session_id 
 */
const char* iat_session_begin(const char* old_session_id,const char* session_begin_params)
{
	if(old_session_id != NULL){
		QISRSessionEnd(old_session_id, NULL);
	}
	const char* session_id = NULL;
	int	errcode	= MSP_SUCCESS ;

	session_id = QISRSessionBegin(NULL, session_begin_params, &errcode); //听写不需要语法，第一个参数为NULL
	if (MSP_SUCCESS != errcode)
	{
		printf("\nQISRSessionBegin failed! error code:%d\n", errcode);
		QISRSessionEnd(session_id, NULL);
		return NULL;
	}
	return session_id;
}

char* get_iat_result()
{
	return g_result;
}

int write_audio_data(const char* session_id,char* sample_data,int sample_length,int is_first_sample)
{
	int				aud_stat					=	MSP_AUDIO_SAMPLE_CONTINUE ;		//音频状态
	int				ep_stat						=	MSP_EP_LOOKING_FOR_SPEECH;		//端点检测
	int				rec_stat					=	MSP_REC_STATUS_SUCCESS ;		//识别状态
	int				errcode						=	MSP_SUCCESS ;
	int 			ret   						=   0;
	
	aud_stat = MSP_AUDIO_SAMPLE_CONTINUE;
	if (is_first_sample)
	{
		aud_stat = MSP_AUDIO_SAMPLE_FIRST;
		on_speech_begin();
	}

	ret = QISRAudioWrite(session_id, sample_data, sample_length, aud_stat, &ep_stat, &rec_stat);
	if (MSP_SUCCESS != ret)
	{
		printf("\nQISRAudioWrite failed! error code:%d\n", ret);
		goto iat_exit;
	}
			
		
	if (MSP_REC_STATUS_SUCCESS == rec_stat) //已经有部分听写结果
	{
		const char *rslt = QISRGetResult(session_id, &rec_stat, 0, &errcode);
		if (MSP_SUCCESS != errcode)
		{
			printf("\nQISRGetResult failed! error code: %d\n", errcode);
			goto iat_exit;
		}
		if (NULL != rslt)
		{
			on_result(rslt,rec_stat == MSP_REC_STATUS_COMPLETE ? 1 : 0);
			return IAT_RESULT_COMPLETE;
		}
	}

	if (MSP_EP_AFTER_SPEECH == ep_stat)
	{
		return IAT_RESULT_VAD;
	}

	return IAT_RESULT_CONTINUE;

iat_exit:
	QISRSessionEnd(session_id, NULL);
	return IAT_RESULT_ERROR;
}
