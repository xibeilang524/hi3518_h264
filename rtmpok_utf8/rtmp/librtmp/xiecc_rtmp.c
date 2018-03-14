/***************************************************************
 Author: xiecc
 Date: 2014-04-03
 E-mail: xiechc@gmail.com
 Notice:
 *  you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation;
 *  flvmuxer is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY;
 *************************************************************/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rtmp.h"
#include "log.h"
#include "xiecc_rtmp.h"

#define AAC_ADTS_HEADER_SIZE 7
#define FLV_TAG_HEAD_LEN 11
#define FLV_PRE_TAG_LEN 4

typedef struct {
    uint8_t audio_object_type;
    uint8_t sample_frequency_index;
    uint8_t channel_configuration;
}AudioSpecificConfig;

typedef struct 
{
    RTMP *rtmp;
    AudioSpecificConfig config;
    uint32_t audio_config_ok;
    uint32_t video_config_ok;
}RTMP_XIECC;


static AudioSpecificConfig gen_config(uint8_t *frame)
{
    AudioSpecificConfig config = {0, 0, 0};

    if (frame == NULL) {
        return config;
    }
    config.audio_object_type =(frame[2] & 0xc0) >> 6;
    config.sample_frequency_index =11;// (frame[2] & 0x3c) >> 2;
    config.channel_configuration = (frame[3] & 0xc0) >> 6;
    return config;
}

static uint8_t gen_audio_tag_header(AudioSpecificConfig config)
{
     uint8_t soundType = config.channel_configuration - 1; //0 mono, 1 stero
     uint8_t soundRate = 0;
     uint8_t val = 0;


     switch (config.sample_frequency_index) {
         case 10: { //11.025k
             soundRate = 1;
             break;
         }
         case 7: { //22k
             soundRate = 2;
             break;
         }
         case 4: { //44k
             soundRate = 3;
             break;
         }
		 case 11: { //8k
				soundRate = 0;
				break;
	     }
         default:
         { 
            // return val;
                         soundRate = 2;
             break;
         }
    }
    val = 0xA0 | (soundRate << 2) | 0x02 | soundType;
    return val;
}
static uint8_t *get_adts(uint32_t *len, uint8_t **offset, uint8_t *start, uint32_t total)
{
    uint8_t *p  =  *offset;
    uint32_t frame_len_1;
    uint32_t frame_len_2;
    uint32_t frame_len_3;
    uint32_t frame_length;
   
    if (total < AAC_ADTS_HEADER_SIZE) {
        return NULL;
    }
    if ((p - start) >= total) {
        return NULL;
    }
    
    if (p[0] != 0xff) {
        return NULL;
    }
    if ((p[1] & 0xf0) != 0xf0) {
        return NULL;
    }
    frame_len_1 = p[3] & 0x03;
    frame_len_2 = p[4];
    frame_len_3 = (p[5] & 0xe0) >> 5;
    frame_length = (frame_len_1 << 11) | (frame_len_2 << 3) | frame_len_3;
    *offset = p + frame_length;
    *len = frame_length;
    return p;
}



// @brief alloc function
// @param [in] url     : RTMP URL, rtmp://127.0.0.1/live/xxx
// @return             : rtmp_sender handler
//rtmp_sender_alloc("rtmp://192.168.1.100/live/stream")
/*
功能：根据url分配一个RTMP发送器并设置
参数：url	in，输入的网址
返回值：指向rtmp_xiecc结构体的指针
rtmp_xiecc结构体会包含RTMP结构体
*/
void *rtmp_sender_alloc(const char *url) //return handle
{
    RTMP_XIECC *rtmp_xiecc; 
    RTMP *rtmp; 

    if (url == NULL) 
	{
        return NULL;
    }
    RTMP_LogSetLevel(RTMP_LOGDEBUG);
    rtmp = RTMP_Alloc();
    RTMP_Init(rtmp);
    rtmp->Link.timeout = 10; //10seconds
    rtmp->Link.lFlags |= RTMP_LF_LIVE;
//RTMP_SetupURL(rtmp,"rtmp://192.168.1.100/live/stream")
    if (!RTMP_SetupURL(rtmp, (char *)url)) 
	{
        RTMP_Log(RTMP_LOGWARNING, "Couldn't set the specified url (%s)!", url);
        RTMP_Free(rtmp);
        return NULL;
    }

    RTMP_EnableWrite(rtmp);//这里r->Link.protocol=0
    rtmp_xiecc = calloc(1, sizeof(RTMP_XIECC));
    rtmp_xiecc->rtmp = rtmp;
    return (void *)rtmp_xiecc;
}

// @brief start publish
// @param [in] rtmp_sender handler
// @param [in] flag        stream falg
// @param [in] ts_us       timestamp in us
// @return             : 0: OK; others: FAILED
//rtmp_sender_start_publish(prtmp, 0, 0)
/*
功能：根据传入的结构体的内容开始发布，建立连接
参数：
	handle:	in,传入的结构体指针
	flag:	in,
	ts_us:	in,
返回值：
*/
int rtmp_sender_start_publish(void *handle, uint32_t flag, int64_t ts_us)
{
    RTMP_XIECC *rtmp_xiecc = (RTMP_XIECC *)handle; 
    RTMP *rtmp; 

    if (rtmp_xiecc == NULL) {
        return 1;
    }
    rtmp = rtmp_xiecc->rtmp; 
	//RTMP要先建立连接connect，然后在连接里建立网络流stream
    if (!RTMP_Connect(rtmp, NULL) || !RTMP_ConnectStream(rtmp, 0))  {
        return 1;
    }
    return 0;
}

// @brief stop publish
// @param [in] rtmp_sender handler
// @return             : 0: OK; others: FAILED
int rtmp_sender_stop_publish(void *handle)
{
    RTMP_XIECC *rtmp_xiecc = (RTMP_XIECC *)handle; 
    RTMP *rtmp ;

    if (rtmp_xiecc == NULL) {
        return 1;
    }

    rtmp = rtmp_xiecc->rtmp; 
    RTMP_Close(rtmp);
    return 0;
}

// @brief send audio frame
// @param [in] rtmp_sender handler
// @param [in] data       : AACAUDIODATA
// @param [in] size       : AACAUDIODATA size
// @param [in] dts_us     : decode timestamp of frame
int rtmp_sender_write_audio_frame(void *handle,
        uint8_t *data,
        int size,
        uint64_t dts_us,
        uint32_t start_time)
{
    RTMP_XIECC *rtmp_xiecc = (RTMP_XIECC *)handle; 
    RTMP *rtmp ;
    uint32_t audio_ts = (uint32_t)dts_us;
    uint8_t * audio_buf = data; 
    uint32_t audio_total = size;
    uint8_t *audio_buf_offset = audio_buf;
    uint8_t *audio_frame;
    uint32_t adts_len;
    uint32_t offset;
    uint32_t body_len;
    uint32_t output_len;
    char *output ; 
    //audio_ts = RTMP_GetTime() - start_time;
    if ((data == NULL) || (rtmp_xiecc == NULL)) {
        return 1;
    }
    rtmp = rtmp_xiecc->rtmp; 
    while (1) {
    //Audio OUTPUT
    offset = 0;
    audio_frame = get_adts(&adts_len, &audio_buf_offset, audio_buf, audio_total);
    if (audio_frame == NULL) break;
    if (rtmp_xiecc->audio_config_ok == 0) {
        rtmp_xiecc->config = gen_config(audio_frame);
        body_len = 2 + 2; //AudioTagHeader + AudioSpecificConfig
        output_len = body_len + FLV_TAG_HEAD_LEN + FLV_PRE_TAG_LEN;
        output = malloc(output_len);
        // flv tag header
        output[offset++] = 0x08; //tagtype video
        output[offset++] = (uint8_t)(body_len >> 16); //data len
        output[offset++] = (uint8_t)(body_len >> 8); //data len
        output[offset++] = (uint8_t)(body_len); //data len
        output[offset++] = (uint8_t)(audio_ts >> 16); //time stamp
        output[offset++] = (uint8_t)(audio_ts >> 8); //time stamp
        output[offset++] = (uint8_t)(audio_ts); //time stamp
        output[offset++] = (uint8_t)(audio_ts >> 24); //time stamp
        output[offset++] = 0x00; //stream id 0
        output[offset++] = 0x00; //stream id 0
        output[offset++] = 0x00; //stream id 0

        //flv AudioTagHeader
        output[offset++] = gen_audio_tag_header(rtmp_xiecc->config); // sound format aac
        output[offset++] = 0x00; //aac sequence header

        //flv VideoTagBody --AudioSpecificConfig
        uint8_t audio_object_type = rtmp_xiecc->config.audio_object_type + 1;
        output[offset++] = (audio_object_type << 3)|(rtmp_xiecc->config.sample_frequency_index >> 1); 
        output[offset++] = ((rtmp_xiecc->config.sample_frequency_index & 0x01) << 7) \
                           | (rtmp_xiecc->config.channel_configuration << 3) ;
        //no need to set pre_tag_size
        /*
        uint32_t fff = body_len + FLV_TAG_HEAD_LEN;
        output[offset++] = (uint8_t)(fff >> 24); //data len
        output[offset++] = (uint8_t)(fff >> 16); //data len
        output[offset++] = (uint8_t)(fff >> 8); //data len
        output[offset++] = (uint8_t)(fff); //data len
        */
        RTMP_Write(rtmp, output, output_len);
        free(output);
        rtmp_xiecc->audio_config_ok = 1;
    }else
    {
        body_len = 2 + adts_len - AAC_ADTS_HEADER_SIZE; // remove adts header + AudioTagHeader
        output_len = body_len + FLV_TAG_HEAD_LEN + FLV_PRE_TAG_LEN;
        output = malloc(output_len);
        // flv tag header
        output[offset++] = 0x08; //tagtype video
        output[offset++] = (uint8_t)(body_len >> 16); //data len
        output[offset++] = (uint8_t)(body_len >> 8); //data len
        output[offset++] = (uint8_t)(body_len); //data len
        output[offset++] = (uint8_t)(audio_ts >> 16); //time stamp
        output[offset++] = (uint8_t)(audio_ts >> 8); //time stamp
        output[offset++] = (uint8_t)(audio_ts); //time stamp
        output[offset++] = (uint8_t)(audio_ts >> 24); //time stamp
        output[offset++] = 0x00; //stream id 0
        output[offset++] = 0x00; //stream id 0
        output[offset++] = 0x00; //stream id 0

        //flv AudioTagHeader
        output[offset++] = gen_audio_tag_header(rtmp_xiecc->config); // sound format aac
        output[offset++] = 0x01; //aac raw data 

        //flv VideoTagBody --raw aac data
        memcpy(output + offset, audio_frame + AAC_ADTS_HEADER_SIZE,\
                 (adts_len - AAC_ADTS_HEADER_SIZE)); //H264 sequence parameter set
        /*
        //previous tag size 
        uint32_t fff = body_len + FLV_TAG_HEAD_LEN;
        offset += (adts_len - AAC_ADTS_HEADER_SIZE);
        output[offset++] = (uint8_t)(fff >> 24); //data len
        output[offset++] = (uint8_t)(fff >> 16); //data len
        output[offset++] = (uint8_t)(fff >> 8); //data len
        output[offset++] = (uint8_t)(fff); //data len
        */
        RTMP_Write(rtmp, output, output_len);
        free(output);
     }
    } //end while 1
    return 0;
}
/*
功能：在数据中找起始码00 00 00 01
参数：
	buf:			in,要查找的数据
	zeros_in_startcode:in,起始数据包含0的个数，一般为3
返回：成功返回1，失败返回0
*/
static uint32_t find_start_code(uint8_t *buf, uint32_t zeros_in_startcode)   
{   
  uint32_t info;   
  uint32_t i;   
   
  info = 1;   
  if ((info = (buf[zeros_in_startcode] != 1)? 0: 1) == 0)   //buf[3]=1
      return 0;   
       
  for (i = 0; i < zeros_in_startcode; i++)   
    if (buf[i] != 0)   
    { 
        info = 0;
        break;
    };   
     
  return info;   
}   
/*
功能：start为一段数据的头，开始offset和start一样指向同一段数据的头，找到一个nal后，offset指向去除第一个nal的数据的头，
	同时参数len保存第一个nal的长度，
参数：
	len:	out，原始数据第一个nal的长度(不包含起始码)
	offset:	in/out，指向新数据的第一个字节
	start:	in，原始数据的第一个字节
	total:	in，原始数据的总长度
返回值：返回指向被找出来的nal的第一个字节(不包含起始码)
*/
static uint8_t * get_nal(uint32_t *len, uint8_t **offset, uint8_t *start, uint32_t total)
{
    uint32_t info;
    uint8_t *q ;
    uint8_t *p  =  *offset;
    *len = 0;
    
    if ((p - start) >= total)
        return NULL;
    
    while(1) 
	{
        info =  find_start_code(p, 3);
        if (info == 1)
            break;
        p++;
        if ((p - start) >= total)
            return NULL;
    }
    q = p + 4;
    p = q;
    while(1) 
	{
        info =  find_start_code(p, 3);
        if (info == 1)
            break;
        p++;
        if ((p - start) >= total)
            break;
    }
    //前面找起始码00 00 00 01，后面又找一个起始码00 00 00 01，二者一减就是第一个nal的长度(不包含起始码)
    *len = (p - q);
    *offset = p;
    return q;
}

// @brief send video frame, now only H264 supported
// @param [in] rtmp_sender handler
// @param [in] data       : video data, (Full frames are required)
// @param [in] size       : video data size
// @param [in] dts_us     : decode timestamp of frame
// @param [in] key        : key frame indicate, [0: non key] [1: key]
//rtmp_sender_write_video_frame(prtmp,ringinfo.buffer, ringinfo.size,timeCount, 0,start_time);
/*
功能：按FLV格式填充内容并发送出去
参数：
	handle:		in/our,rtmp结构体，根据rtmp结构体里的配置来进行不同的处理
	data:		in,要发送的视频数据
	size:		in,要发送的视频数据的大小
	dts_us:		in,时间戳
	key:		是否是关键帧，这里不需要，因为在函数里通过读取nalu[0]来判断是否是关键帧
	start_time:	时间戳的起始时间
返回值：成功返回0，失败返回1
*/
int rtmp_sender_write_video_frame(void *handle,uint8_t *data,int size,uint64_t dts_us,int key,uint32_t start_time)
{
	uint8_t * buf; 
	uint8_t * buf_offset;
	int total;
	uint32_t ts;
	uint32_t nal_len;
	uint32_t nal_len_n;
	uint8_t *nal; 
	uint8_t *nal_n;
	char *output ; 
	uint32_t offset = 0;
	uint32_t body_len;
	uint32_t output_len;
	RTMP_XIECC *rtmp_xiecc;
	RTMP *rtmp;


	buf = data;
	buf_offset = data;
	total = size;
	ts = (uint32_t)dts_us;
	rtmp_xiecc = (RTMP_XIECC *)handle; 
	if ((data == NULL) || (rtmp_xiecc == NULL)) 
	{
		return 1;
	}
	rtmp = rtmp_xiecc->rtmp; 
	//printf("ts is %d\n",ts);

	while (1) 
	{
		//ts = RTMP_GetTime() - start_time;  //
		//by ssy
		offset = 0;
		nal = get_nal(&nal_len, &buf_offset, buf, total);
		if (nal == NULL) 
			break;
		if (nal[0] == 0x67) //打包sps，pps这里没有if (nal[0] == 0x68)，但这里直接处理了两个nal即把nal[0] == 0x68也处理了
		{
			if (rtmp_xiecc->video_config_ok > 0) 
			{
				continue; //only send video sequence set one time
			}
			nal_n  = get_nal(&nal_len_n, &buf_offset, buf, total); //get pps//sps后面一般是紧跟pps 0x68
			if (nal_n == NULL) 
			{
				RTMP_Log(RTMP_LOGERROR, "No Nal after SPS");
				break;
			}
			/*
			FLVtag：其实就是消息=消息头flvtag header+载荷
			*/
			body_len = nal_len + nal_len_n + 16;//h264封装成rtmp包：16字节信息头+数据
			output_len = body_len + FLV_TAG_HEAD_LEN + FLV_PRE_TAG_LEN;
			//0x67+0x68两帧的长度+16字节+FLV_TAG_HEAD_LEN+FLV_PRE_TAG_LEN
			output = malloc(output_len);
			/////////////////////////// flv tag header////////////////////////////////////////
			//消息头=标志消息类型的Message Type ID(1byte)+标志载荷长度的Payload Length(3byte)+标识时间戳的Timestamp(4byte)+标识消息所属媒体流的Stream ID(3byte)
			output[offset++] = 0x09; //tagtype video  
			//output[0]=0x09消息类型,9代表视频，1字节
			output[offset++] = (uint8_t)(body_len >> 16); //data len
			output[offset++] = (uint8_t)(body_len >> 8); //data len
			output[offset++] = (uint8_t)(body_len); //data len
			//output[1-3]有效数据长度，3字节
			output[offset++] = (uint8_t)(ts >> 16); //time stamp
			output[offset++] = (uint8_t)(ts >> 8); //time stamp
			output[offset++] = (uint8_t)(ts); //time stamp
			output[offset++] = (uint8_t)(ts >> 24); //time stamp
			//output[4-7]时间戳，4字节
			output[offset++] = 0x00; //stream id 0
			output[offset++] = 0x00; //stream id 0
			output[offset++] = 0x00; //stream id 0
			//output[8-10]媒体流ID，3字节
			//////////////////////////////////////////////////////////////////////////////

			///////////////////////////flv VideoTagHeader///////////////////////////////////
			///////////////////flvvideotagheader/////////////////////////
			//sps,pps填0x17 00,composit time无效填0
			output[offset++] = 0x17; //key frame, AVC
			//output[11]高4位表示帧类型，这里是关键帧 1，低4位表示编码模式，这里是AVC,7
			output[offset++] = 0x00; //avc sequence header
			//output[12]
			output[offset++] = 0x00; //composit time ??????????
			output[offset++] = 0x00; // composit time
			output[offset++] = 0x00; //composit time
			//output[13-15]AVC时，composit time没有意义，所以全填为0
			/////////////////////////////////////////////////////////

			/////////////////flvvideotagbody/////////////////////////////
			//flv VideoTagBody --AVCDecoderCOnfigurationRecord
			output[offset++] = 0x01; //configurationversion//sps,pps专用，填版本号1
			output[offset++] = nal[1]; //avcprofileindication
			output[offset++] = nal[2]; //profilecompatibilty
			output[offset++] = nal[3]; //avclevelindication
			output[offset++] = 0xff; //reserved + lengthsizeminusone//一般填ff
			output[offset++] = 0xe1; //numofsequenceset//一般填e1
			//sps len
			output[offset++] = (uint8_t)(nal_len >> 8); //sequence parameter set length high 8 bits
			output[offset++] = (uint8_t)(nal_len); //sequence parameter set  length low 8 bits
			//sps data
			memcpy(output + offset, nal, nal_len); //H264 sequence parameter set
			offset += nal_len;
			//pps一个数，一般为1
			output[offset++] = 0x01; //numofpictureset
			//pps len
			output[offset++] = (uint8_t)(nal_len_n >> 8); //picture parameter set length high 8 bits
			output[offset++] = (uint8_t)(nal_len_n); //picture parameter set length low 8 bits
			//pps data
			memcpy(output + offset, nal_n, nal_len_n); //H264 picture parameter set
			/////////////////////////////////////////////////////////
			//////////////////////////////////////////////////////////////////////////////
			//no need set pre_tag_size ,RTMP NO NEED
			// flv test 
			/*
			offset += nal_len_n;
			uint32_t fff = body_len + FLV_TAG_HEAD_LEN;
			output[offset++] = (uint8_t)(fff >> 24); //data len
			output[offset++] = (uint8_t)(fff >> 16); //data len
			output[offset++] = (uint8_t)(fff >> 8); //data len
			output[offset++] = (uint8_t)(fff); //data len
			*/
			RTMP_Write(rtmp, output, output_len);
			//RTMP Send out
			free(output);
			rtmp_xiecc->video_config_ok = 1;
			continue;
		}

		if (nal[0] == 0x65)//打包I帧
		{
			body_len = nal_len + 5 + 4; //flv VideoTagHeader +  NALU length
			output_len = body_len + FLV_TAG_HEAD_LEN + FLV_PRE_TAG_LEN;
			output = malloc(output_len);
			// flv tag header
			output[offset++] = 0x09; //tagtype video
			output[offset++] = (uint8_t)(body_len >> 16); //data len
			output[offset++] = (uint8_t)(body_len >> 8); //data len
			output[offset++] = (uint8_t)(body_len); //data len
			output[offset++] = (uint8_t)(ts >> 16); //time stamp
			output[offset++] = (uint8_t)(ts >> 8); //time stamp
			output[offset++] = (uint8_t)(ts); //time stamp
			output[offset++] = (uint8_t)(ts >> 24); //time stamp
			output[offset++] = 0x00; //stream id 0
			output[offset++] = 0x00; //stream id 0
			output[offset++] = 0x00; //stream id 0

			//flv VideoTagHeader
			output[offset++] = 0x17; //key frame, AVC
			output[offset++] = 0x01; //avc NALU unit
			output[offset++] = 0x00; //composit time ??????????
			output[offset++] = 0x00; // composit time
			output[offset++] = 0x00; //composit time

			output[offset++] = (uint8_t)(nal_len >> 24); //nal length 
			output[offset++] = (uint8_t)(nal_len >> 16); //nal length 
			output[offset++] = (uint8_t)(nal_len >> 8); //nal length 
			output[offset++] = (uint8_t)(nal_len); //nal length 
			memcpy(output + offset, nal, nal_len);

			//no need set pre_tag_size ,RTMP NO NEED
			/*
			offset += nal_len;
			uint32_t fff = body_len + FLV_TAG_HEAD_LEN;
			output[offset++] = (uint8_t)(fff >> 24); //data len
			output[offset++] = (uint8_t)(fff >> 16); //data len
			output[offset++] = (uint8_t)(fff >> 8); //data len
			output[offset++] = (uint8_t)(fff); //data len
			*/
			RTMP_Write(rtmp, output, output_len);
			//RTMP Send out
			free(output);
			continue;
		}

		if ((nal[0] & 0x1f) == 0x01)//打包其它帧
		{
			body_len = nal_len + 5 + 4; //flv VideoTagHeader +  NALU length
			output_len = body_len + FLV_TAG_HEAD_LEN + FLV_PRE_TAG_LEN;
			output = malloc(output_len);
			// flv tag header
			output[offset++] = 0x09; //tagtype video
			output[offset++] = (uint8_t)(body_len >> 16); //data len
			output[offset++] = (uint8_t)(body_len >> 8); //data len
			output[offset++] = (uint8_t)(body_len); //data len
			output[offset++] = (uint8_t)(ts >> 16); //time stamp
			output[offset++] = (uint8_t)(ts >> 8); //time stamp
			output[offset++] = (uint8_t)(ts); //time stamp
			output[offset++] = (uint8_t)(ts >> 24); //time stamp
			output[offset++] = 0x00; //stream id 0
			output[offset++] = 0x00; //stream id 0
			output[offset++] = 0x00; //stream id 0

			//flv VideoTagHeader
			output[offset++] = 0x27; //not key frame, AVC
			output[offset++] = 0x01; //avc NALU unit
			output[offset++] = 0x00; //composit time ??????????
			output[offset++] = 0x00; // composit time
			output[offset++] = 0x00; //composit time

			output[offset++] = (uint8_t)(nal_len >> 24); //nal length 
			output[offset++] = (uint8_t)(nal_len >> 16); //nal length 
			output[offset++] = (uint8_t)(nal_len >> 8); //nal length 
			output[offset++] = (uint8_t)(nal_len); //nal length 
			memcpy(output + offset, nal, nal_len);

			//no need set pre_tag_size ,RTMP NO NEED
			/*
			offset += nal_len;
			uint32_t fff = body_len + FLV_TAG_HEAD_LEN;
			output[offset++] = (uint8_t)(fff >> 24); //data len
			output[offset++] = (uint8_t)(fff >> 16); //data len
			output[offset++] = (uint8_t)(fff >> 8); //data len
			output[offset++] = (uint8_t)(fff); //data len
			*/
			RTMP_Write(rtmp, output, output_len);

			//RTMP Send out
			free(output);
			continue;
		}
	}
	return 0;
}

// @brief free rtmp_sender handler
// @param [in] rtmp_sender handler
void rtmp_sender_free(void *handle)
{
    RTMP_XIECC *rtmp_xiecc;
    RTMP *rtmp;

    if (handle == NULL) {
        return;
    }

    rtmp_xiecc = (RTMP_XIECC *)handle; 
    rtmp = rtmp_xiecc->rtmp; 
    if (rtmp != NULL) {
        RTMP_Free(rtmp);
    }
    free(rtmp_xiecc);
}

