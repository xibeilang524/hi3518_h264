#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "hi_mem.h"
#include "rtmpstream.h"   
#include "rtmp.h"  
#include "rtmp_sys.h"  
#include "amf.h"  
#include "SpsDecode.h"
#include "usetime.h"

#define FLV_CODECID_H264 7  

typedef struct _NaluUnit  
{  
    int type;  
    int size;  
    unsigned char *data;  
}NaluUnit;  
int m_nCurPos=0;
bool ReadOneNaluFromBuf(NaluUnit &nalu,uint8 *data,int len)  
{  
    int i = m_nCurPos;  
    while(i<len)  
    {  
        if(data[i++] == 0x00 &&  
            data[i++] == 0x00 &&  
            data[i++] == 0x00 &&  
            data[i++] == 0x01  
            )  
        {  
            int pos = i;  
            while (pos<len)  
            {  
                if(data[pos++] == 0x00 &&  
                    data[pos++] == 0x00 &&  
                    data[pos++] == 0x00 &&  
                    data[pos++] == 0x01  
                    )  
                {  
                    break;  
                }  
            }  
            if(pos == len )  
            {  
                nalu.size = pos-i;    
            }  
            else  
            {  
                nalu.size = (pos-4)-i;  
            }  
            nalu.type = data[i]&0x1f;  
            nalu.data = &data[i];  
  
            m_nCurPos = pos-4;  
            return TRUE;  
        }  
    }  
    return FALSE;  
}  

char * put_byte( char *output, uint8_t nVal )    
{    
    output[0] = nVal;    
    return output+1;    
}    
char * put_be16(char *output, uint16_t nVal )    
{    
    output[1] = nVal & 0xff;    
    output[0] = nVal >> 8;    
    return output+2;    
}    
char * put_be24(char *output,uint32_t nVal )    
{    
    output[2] = nVal & 0xff;    
    output[1] = nVal >> 8;    
    output[0] = nVal >> 16;    
    return output+3;    
}    
char * put_be32(char *output, uint32_t nVal )    
{    
    output[3] = nVal & 0xff;    
    output[2] = nVal >> 8;    
    output[1] = nVal >> 16;    
    output[0] = nVal >> 24;    
    return output+4;    
}    
char *  put_be64( char *output, uint64_t nVal )    
{    
    output=put_be32( output, nVal >> 32 );    
    output=put_be32( output, nVal );    
    return output;    
}    
char * put_amf_string( char *c, const char *str )    
{    
    uint16_t len = strlen( str );    
    c=put_be16( c, len );    
    memcpy(c,str,len);    
    return c+len;    
}    
char * put_amf_double( char *c, double d )    
{    
    *c++ = AMF_NUMBER;  /* type: Number */    
    {    
        unsigned char *ci, *co;    
        ci = (unsigned char *)&d;    
        co = (unsigned char *)c;    
        co[0] = ci[7];    
        co[1] = ci[6];    
        co[2] = ci[5];    
        co[3] = ci[4];    
        co[4] = ci[3];    
        co[5] = ci[2];    
        co[6] = ci[1];    
        co[7] = ci[0];    
    }    
    return c+8;    
}  

CRTMPStream::CRTMPStream()
{
	RtmpPort=0;
	m_pRtmp=NULL;
	memset(RtmpUrl,0,sizeof(RtmpUrl));
	memset(DestIp,0,sizeof(DestIp));
	run=false;
	Buffer=NULL;
}
CRTMPStream::~CRTMPStream()
{
	
}
bool CRTMPStream::InitRtmpStream(CMediaBuffer *buf,const char *destip,const char *url,uint32 port)
{
	if(Buffer==NULL)
		Buffer=buf;
	strcpy(RtmpUrl,url);
	strcpy(DestIp,destip);
	RtmpPort=port;
	m_pRtmp = (void *)RTMP_Alloc();    
    RTMP_Init((RTMP *)m_pRtmp);  
	run=false;
	return true;
}
bool CRTMPStream::StartRtmpStream()
{
	char str[64];
	snprintf(str,64,"rtmp://%s:%ld/live/%s",DestIp,RtmpPort,RtmpUrl);
	printf("str=%s\n",str);
	if(RTMP_SetupURL((RTMP *)m_pRtmp,str)<0)  
    {  
		printf("RTMP_SetupURL fail\n");
        return false;  
    }  
    RTMP_EnableWrite((RTMP *)m_pRtmp);  
    if(RTMP_Connect((RTMP *)m_pRtmp, NULL)<0)  
    {  
		printf("RTMP_Connect fail\n");
        return false;  
    }  
    if(RTMP_ConnectStream((RTMP *)m_pRtmp,0)<0)  
    {  
        printf("RTMP_ConnectStream fail\n");
        return false;    
    }
	run=true;
	pthread_create(&thread,NULL,RmtpStreamThread,this);
	return true;
}
bool CRTMPStream::StopRtmpStream()
{
	return true;
}
int CRTMPStream::SendPacket(unsigned int nPacketType,unsigned char *data,unsigned int size,unsigned int nTimestamp)  
{
	if (m_pRtmp == NULL)
	{
		return FALSE;
	}

	RTMPPacket packet;
	RTMPPacket_Reset(&packet);
	RTMPPacket_Alloc(&packet,size);

	packet.m_packetType = nPacketType;
	packet.m_nChannel = 0x04;
	packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
	packet.m_nTimeStamp = nTimestamp;
	packet.m_nInfoField2 = ((RTMP *)m_pRtmp)->m_stream_id;
	packet.m_nBodySize = size;
	memcpy(packet.m_body,data,size);

	int nRet = RTMP_SendPacket((RTMP *)m_pRtmp,&packet,0);

	RTMPPacket_Free(&packet);

	return nRet;
}
bool CRTMPStream::SendMetadata(LPRTMPMetadata lpMetaData)
{
	if(lpMetaData == NULL)  
    {  
        return false;  
    }  
    char body[1024] = {0};;  
      
    char * p = (char *)body;    
    p = put_byte(p, AMF_STRING );  
    p = put_amf_string(p , "@setDataFrame" );  
  
    p = put_byte( p, AMF_STRING );  
    p = put_amf_string( p, "onMetaData" );  
  
    p = put_byte(p, AMF_OBJECT );    
    p = put_amf_string( p, "copyright" );    
    p = put_byte(p, AMF_STRING );    
    p = put_amf_string( p, "firehood" );    
  
    p =put_amf_string( p, "width");  
    p =put_amf_double( p, lpMetaData->nWidth);  
  
    p =put_amf_string( p, "height");  
    p =put_amf_double( p, lpMetaData->nHeight);  
  
    p =put_amf_string( p, "framerate" );  
    p =put_amf_double( p, lpMetaData->nFrameRate);   
  
    p =put_amf_string( p, "videocodecid" );  
    p =put_amf_double( p, FLV_CODECID_H264 );  
  
    p =put_amf_string( p, "" );  
    p =put_byte( p, AMF_OBJECT_END  );  
  
    int index = p-body;  
  
    SendPacket(RTMP_PACKET_TYPE_INFO,(unsigned char*)body,p-body,0);  
  
    int i = 0;  
    body[i++] = 0x17; // 1:keyframe  7:AVC  
    body[i++] = 0x00; // AVC sequence header  
  
    body[i++] = 0x00;  
    body[i++] = 0x00;  
    body[i++] = 0x00; // fill in 0;  
  
    // AVCDecoderConfigurationRecord.  
    body[i++] = 0x01; // configurationVersion  
    body[i++] = lpMetaData->Sps[1]; // AVCProfileIndication  
    body[i++] = lpMetaData->Sps[2]; // profile_compatibility  
    body[i++] = lpMetaData->Sps[3]; // AVCLevelIndication   
    body[i++] = 0xff; // lengthSizeMinusOne    
  
    // sps nums  
    body[i++] = 0xE1; //&0x1f  
    // sps data length  
    body[i++] = lpMetaData->nSpsLen>>8;  
    body[i++] = lpMetaData->nSpsLen&0xff;  
    // sps data  
    memcpy(&body[i],lpMetaData->Sps,lpMetaData->nSpsLen);  
    i= i+lpMetaData->nSpsLen;  
  
    // pps nums  
    body[i++] = 0x01; //&0x1f  
    // pps data length   
    body[i++] = lpMetaData->nPpsLen>>8;  
    body[i++] = lpMetaData->nPpsLen&0xff;  
    // sps data  
    memcpy(&body[i],lpMetaData->Pps,lpMetaData->nPpsLen);  
    i= i+lpMetaData->nPpsLen;  
    printf("send packet\n");
    return SendPacket(RTMP_PACKET_TYPE_VIDEO,(unsigned char*)body,i,0);  
}
bool CRTMPStream::SendH264Packet(unsigned char *data,unsigned int size,bool bIsKeyFrame,unsigned int nTimeStamp)  
{  
    if(data == NULL && size<11)  
    {  
        return false;  
    }  
  
    unsigned char *body = new unsigned char[size+9];  
  
    int i = 0;  
    if(bIsKeyFrame)  
    {  
        body[i++] = 0x17;// 1:Iframe  7:AVC  
    }  
    else  
    {  
        body[i++] = 0x27;// 2:Pframe  7:AVC  
    }  
    body[i++] = 0x01;// AVC NALU  
    body[i++] = 0x00;  
    body[i++] = 0x00;  
    body[i++] = 0x00;  
  
    // NALU size  
    body[i++] = size>>24;  
    body[i++] = size>>16;  
    body[i++] = size>>8;  
    body[i++] = size&0xff;;  
  
	CUseTime time1;
	time1.Start();
    // NALU data  
    memcpy(&body[i],data,size);  
	printf("time1=%d\n",time1.GetUseTime());
	
	time1.Start();
    bool bRet = SendPacket(RTMP_PACKET_TYPE_VIDEO,body,i+size,nTimeStamp);  
	printf("time2=%d\n",time1.GetUseTime());
	
    delete[] body;  
  
    return bRet;  
}  


		m_nCurPos=0;
		NaluUnit naluUnit;  
		if(SendMeta)//SendMetadata
		{
			SendMeta=false;
			RTMPMetadata metaData;  
			memset(&metaData,0,sizeof(RTMPMetadata));  
  			
			// 读取SPS帧  
			ReadOneNaluFromBuf(naluUnit,u8ReadBuf,pHeader->FrameLen);  
			metaData.nSpsLen = naluUnit.size;  
			memcpy(metaData.Sps,naluUnit.data,naluUnit.size);  
  			// 读取PPS帧  
			ReadOneNaluFromBuf(naluUnit,u8ReadBuf,pHeader->FrameLen);  
			metaData.nPpsLen = naluUnit.size;  
			memcpy(metaData.Pps,naluUnit.data,naluUnit.size);  
		  
			// 解码SPS,获取视频图像宽、高信息  
			int width = 0,height = 0;  
			h264_decode_sps(metaData.Sps,metaData.nSpsLen,width,height);  
			printf("width=%d,height=%d\n",width,height);
			metaData.nWidth = width;  
			metaData.nHeight = height;  
			metaData.nFrameRate = 30;  
			// 发送MetaData  
			p->SendMetadata(&metaData);  
			printf("send SendMetadata ok\n");
			while(ReadOneNaluFromBuf(naluUnit,u8ReadBuf,pHeader->FrameLen))  
			{
				//printf("senda naluUnit.size=%d\n",naluUnit.size);
				bool bKeyframe  = (naluUnit.type == 0x05) ? true : false;
				// 发送H264数据帧
				p->SendH264Packet(naluUnit.data,naluUnit.size,bKeyframe,(uint32)(pHeader->Pts/1000));
			}
		}
		else
