/*
 * 语音听写(iFly Auto Transform)技术能够实时地将语音转换成对应的文字。
 * 本程序用于将麦克风输入的语音输出成文本
 */

#ifdef __cplusplus
extern "C" {
#endif
	
enum {
	IAT_RESULT_COMPLETE,
	IAT_RESULT_ERROR,	
	IAT_RESULT_VAD,
	IAT_RESULT_CONTINUE
};

/**
 *  将麦克风输入的语音转换为对应的文字
 */
char* mic_ifly_to_text(const char* session_begin_params);

/**
 *  开始一次读取会话，成功则返回session_id 
 */
const char* iat_session_begin(const char* old_session_id,const char* session_begin_params);

int write_audio_data(const char* session_id,char* sample_data,int sample_length,int is_first_sample);

char* get_iat_result();


#ifdef __cplusplus
} /* extern "C" */	
#endif /* C++ */