#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include "ringfifo.h"
#include "sample_comm.h"
#include "xiecc_rtmp.h"
//#include "librtmp_send264.h"
extern int g_s32Quit ;

/**************************************************************************************************
**
**
**
**************************************************************************************************/
extern void * SAMPLE_VENC_1080P_CLASSIC(void *p);
uint32_t timeCount = 0;
uint32_t start_time =0;
void *prtmp=NULL;
struct ringbuf ringinfo;
int ringbuflen=0;

int SendH264Packet(unsigned char *data,unsigned int size,int bIsKeyFrame,unsigned int nTimeStamp);
void SendRtmpH264(void *data,int len,int key,unsigned int nTimeStamp)
{
	printf("len=%d\n",len);
	//SendH264Packet(data,len,key,nTimeStamp);
	rtmp_sender_write_video_frame(prtmp,data,len,nTimeStamp,key,start_time);
}
//rtmp 192.168.1.100
int main(int argc, char *argv[])
{
	pthread_t id;
	char serverStrBuf[100];
	if(argc!=2)
	{
		printf("Usage: rtmp serverip -eg<< rtmp 192.168.1.100 >>");
		return -1;
	}
	sprintf(serverStrBuf, "rtmp://%s/live/stream",argv[1]);
	printf("Server=%s\n",serverStrBuf);
	void*prtmp = rtmp_sender_alloc(serverStrBuf); //return handle   rtmp_sender_alloc("rtmp://192.168.1.100/live/stream")
	if(rtmp_sender_start_publish(prtmp, 0, 0)!=0)
	{
		printf("connect %s fail\n",serverStrBuf);
		return -1;
	}
	ringmalloc(128*1024);//建立环形缓冲区
	start_time = RTMP_GetTime();
	/*
	线程1:main，初始化RTMP,创建SAMPLE_VENC_1080P_CLASSIC线程，usleep休眠，超时唤醒
	线程2:SAMPLE_VENC_1080P_CLASSIC,主要是初始化摄像头，然后sleep休眠，超时唤醒
	线程3:SAMPLE_COMM_VENC_GetVencStreamProc，获取H264视频数据，select休眠，超时或有数据唤醒
	*/
	pthread_create(&id,NULL,SAMPLE_VENC_1080P_CLASSIC,NULL);
	//SAMPLE_VENC_1080P_CLASSIC会将视频数据放到环形缓冲区ringinfo里
	while(1)
	{
		ringbuflen = ringget(&ringinfo);
		if(ringbuflen !=0)
		{
			//printf("len=%d\n",ringbuflen);
			rtmp_sender_write_video_frame(prtmp,ringinfo.buffer, ringinfo.size,timeCount, 0,start_time);
			timeCount += 2;
		}   
		usleep(1);
	}
	return 0;
}

