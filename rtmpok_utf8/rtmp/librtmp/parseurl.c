/*
 *  Copyright (C) 2009 Andrej Stepanchuk
 *  Copyright (C) 2009-2010 Howard Chu
 *
 *  This file is part of librtmp.
 *
 *  librtmp is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation; either version 2.1,
 *  or (at your option) any later version.
 *
 *  librtmp is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with librtmp see the file COPYING.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA  02110-1301, USA.
 *  http://www.gnu.org/copyleft/lgpl.html
 */

#include <stdlib.h>
#include <string.h>

#include <assert.h>
#include <ctype.h>

#include "rtmp_sys.h"
#include "log.h"
/*
功能:根据url得到协议，主机名，端口，播放路径，app名
参数:
	url:		in,要解析的命令
	protocol:	out，协议
	host:		out,主机名
	port:		out，端口
	playpath:	out，播放路径
	app:		out
返回值:成功返回1，失败返回0
//RTMP_ParseURL("rtmp://192.168.1.100/live/stream",pro,host,port,xx,xx)
*/
int RTMP_ParseURL(const char *url, int *protocol, AVal *host, unsigned int *port,AVal *playpath, AVal *app)
{
	char *p, *end, *col, *ques, *slash;

	RTMP_Log(RTMP_LOGDEBUG, "Parsing...");

	*protocol = RTMP_PROTOCOL_RTMP;//有多个RTMP协议，只分析第1个
	*port = 0;
	playpath->av_len = 0;
	playpath->av_val = NULL;
	app->av_len = 0;
	app->av_val = NULL;

	/* Old School Parsing */

	/* look for usual :// pattern */
	p = strstr(url, "://");//p="://192.168.1.100/live/stream"
	printf("in RTMP_ParseURL,p1=%s\n",p);//in RTMP_ParseURL,p1=://192.168.1.101/live/stream
	if(!p) {
		RTMP_Log(RTMP_LOGERROR, "RTMP URL: No :// in url!");
		return FALSE;
	}
	{
	int len = (int)(p-url);//因为p,usr都是字符型指针，都指向同一字符串，所以二者一减就是地址偏差，就是字符长度

	if(len == 4 && strncasecmp(url, "rtmp", 4)==0)//strncasecmp:比较url与"rtmp"前面4个字符，忽略大小写
		*protocol = RTMP_PROTOCOL_RTMP;
	else if(len == 5 && strncasecmp(url, "rtmpt", 5)==0)
		*protocol = RTMP_PROTOCOL_RTMPT;
	else if(len == 5 && strncasecmp(url, "rtmps", 5)==0)
	        *protocol = RTMP_PROTOCOL_RTMPS;
	else if(len == 5 && strncasecmp(url, "rtmpe", 5)==0)
	        *protocol = RTMP_PROTOCOL_RTMPE;
	else if(len == 5 && strncasecmp(url, "rtmfp", 5)==0)
	        *protocol = RTMP_PROTOCOL_RTMFP;
	else if(len == 6 && strncasecmp(url, "rtmpte", 6)==0)
	        *protocol = RTMP_PROTOCOL_RTMPTE;
	else if(len == 6 && strncasecmp(url, "rtmpts", 6)==0)
	        *protocol = RTMP_PROTOCOL_RTMPTS;
	else {
		RTMP_Log(RTMP_LOGWARNING, "Unknown protocol!\n");
		goto parsehost;
	}
	}

	RTMP_Log(RTMP_LOGDEBUG, "Parsed protocol: %d", *protocol);
	//DEBUG: Parsed protocol: 0

parsehost:
	/* let's get the hostname */
	p+=3;//p="://192.168.1.100/live/stream",移3个，所以p="192.168.1.100/live/stream"
	printf("in RTMP_ParseURL,p2=%s\n",p);//in RTMP_ParseURL,p2=192.168.1.101/live/stream
	/* check for sudden death */
	if(*p==0)//不执行
	{
		RTMP_Log(RTMP_LOGWARNING, "No hostname in URL!");
		return FALSE;
    }
    else if (*p == '[') //不执行
	{
        //IPV6
        col   = strstr(p, "]:");
        if (col != NULL)
            col++;
    } 
	else 
	{ 
	    col   = strchr(p, ':');//"rtmp://192.168.1.100/live/stream"这里col为空，但p还是"192.168.1.100/live/stream"
	    printf("in RTMP_ParseURL,col1=%s\n",col);//in RTMP_ParseURL,col1=(null)
    }

    end = p + strlen(p);
    ques = strchr(p, '?');//空
    slash = strchr(p, '/');//slash="/live/stream",p还是"192.168.1.100/live/stream"
    printf("in RTMP_ParseURL,slash=%s\n",slash);//in RTMP_ParseURL,slash=/live/stream


	{
	int hostlen;
	if(slash)//slash=/live/stream  执行这个里
		hostlen = slash - p;//hostlen=13
	else//不执行
		hostlen = end - p;
	if(col && col -p < hostlen)//col为空，不执行
		hostlen = col - p;

	if(hostlen < 256) 
	{
		host->av_val = p;//="192.168.1.100/live/stream"
		printf("in RTMP_ParseURL,host->av_val1=%s\n",host->av_val);//in RTMP_ParseURL,host->av_val1=192.168.1.101/live/stream
		host->av_len = hostlen;
		RTMP_Log(RTMP_LOGDEBUG, "Parsed host    : %.*s", hostlen, host->av_val);//只打印hostlen个字符，即IP地址
		//DEBUG: Parsed host    : 192.168.1.101
	} else 
	{
		RTMP_Log(RTMP_LOGWARNING, "Hostname exceeds 255 characters!");
	}

	p+=hostlen;//p="/live/stream"
	}

	/* get the port number if available */
	if(*p == ':') //不执行
	{//p="/live/stream"
		unsigned int p2;
		p++;
		p2 = atoi(p);
		if(p2 > 65535)
		{
			RTMP_Log(RTMP_LOGWARNING, "Invalid port number!");
		}
		else 
		{
			*port = p2;
		}
	}

	if(!slash) //slash="/live/stream"
	{
		RTMP_Log(RTMP_LOGWARNING, "No application or playpath in URL!");
		return TRUE;
	}
	p = slash+1;//p="live/stream"

	{
	/* parse application
	 *
	 * rtmp://host[:port]/app[/appinstance][/...]
	 * application = app[/appinstance]
	 */

	char *slash2, *slash3 = NULL, *slash4 = NULL;
	int applen, appnamelen;

	slash2 = strchr(p, '/');//slash2="/stream",但p还是"live/stream"
	if(slash2)
		slash3 = strchr(slash2+1, '/');//slash2+1=“stream”，在“stream”里找'/',找不到，所以slash3=空
	if(slash3) //不执行
	{
		slash4 = strchr(slash3+1, '/');
	}
	else
	{
		printf("slash3 is null\n");//slash3 is null
	}

	applen = end-p; /* ondemand, pass all parameters as app */
	//p还是"live/stream",所以applen=strlen("live/stream")=11
	appnamelen = applen; /* ondemand length */

	if(ques && strstr(p, "slist=")) //前面可知ques为空，不执行
	{ /* whatever it is, the '?' and slist= means we need to use everything as app and parse plapath from slist= */
		appnamelen = ques-p;
	}
	else if(strncmp(p, "ondemand/", 9)==0) //不执行
	{
		/* app = ondemand/foobar, only pass app=ondemand */
		applen = 8;
		appnamelen = 8;
	}
	else 
	{ /* app!=ondemand, so app is app[/appinstance] */
		if(slash4)//slash4=null
			appnamelen = slash4-p;
		else if(slash3)//slash3=null
			appnamelen = slash3-p;
		else if(slash2)//slash2="/stream"
			appnamelen = slash2-p;//p="live/stream",二者一减为strlen("live")=4
		applen = appnamelen;
	}

	app->av_val = p;//app->av_val="live/stream"
	app->av_len = applen;
	RTMP_Log(RTMP_LOGDEBUG, "Parsed app     : %.*s", applen, p);//打印4个字符即为live
	//DEBUG: Parsed app     : live

	p += appnamelen;//p="/stream"
	}

	if (*p == '/')//p="/stream"
		p++;//p="stream"

	if (end-p) 
	{
		AVal av = {p, end-p};//p="stream"	av={"stream",6}
		RTMP_ParsePlaypath(&av, playpath);//av.av_val=p="stream"
	}

	return TRUE;
}

/*
 * Extracts playpath from RTMP URL. playpath is the file part of the
 * URL, i.e. the part that comes after rtmp://host:port/app/
 *
 * Returns the stream name in a format understood by FMS. The name is
 * the playpath part of the URL with formatting depending on the stream
 * type:
 *
 * mp4 streams: prepend "mp4:", remove extension
 * mp3 streams: prepend "mp3:", remove extension
 * flv streams: remove extension
 */
 /*
功能： 从URL中获取播放路径（playpath）。播放路径是URL中“rtmp://host:port/app/”后面的部分,这里为stream
//rtmp 192.168.1.100/live/stream
参数:
	in:	in,输入的播放路径
	out: out,解析后的存放播放路径
返回值：无
*/
//av={"stream",6}
void RTMP_ParsePlaypath(AVal *in, AVal *out) {
	int addMP4 = 0;
	int addMP3 = 0;
	int subExt = 0;
	const char *playpath = in->av_val;
	const char *temp, *q, *ext = NULL;
	const char *ppstart = playpath;//ppstart="stream"
	char *streamname, *destptr, *p;

	int pplen = in->av_len;
	printf("ppstart=%s,pplen=%d\n",ppstart,pplen);//ppstart=stream,pplen=6
	out->av_val = NULL;
	out->av_len = 0;

	if ((*ppstart == '?') &&(temp=strstr(ppstart, "slist=")) != 0) //不执行
	{
		printf("in ?\n");
		ppstart = temp+6;
		pplen = strlen(ppstart);

		temp = strchr(ppstart, '&');
		if (temp) 
		{
			pplen = temp-ppstart;
		}
	}
	//ppstart="stream"
	q = strchr(ppstart, '?');//q=null
	if (pplen >= 4) 
	{
		if (q)
		{
			ext = q-4;
			printf("has ?\n");
		}
		else//执行else
		{
			printf("has no ?\n");//has no ?
			ext = &ppstart[pplen-4];//ext=&ppstart[2],ppstart="stream",所以ext=“ream” 
			printf("ext=%s\n",ext);//ext=ream
		}
		if ((strncmp(ext, ".f4v", 4) == 0) ||(strncmp(ext, ".mp4", 4) == 0)) //不执行
		{printf("f4v\n");
			addMP4 = 1;
			subExt = 1;
		/* Only remove .flv from rtmp URL, not slist params */
		}
		else if ((ppstart == playpath) &&(strncmp(ext, ".flv", 4) == 0)) //不执行
		{printf("flv\n");
			subExt = 1;
		} 
		else if (strncmp(ext, ".mp3", 4) == 0) //不执行
		{
			printf("mp3\n");
			addMP3 = 1;
			subExt = 1;
		}
	}

	streamname = (char *)malloc((pplen+4+1)*sizeof(char));
	if (!streamname)
		return;

	destptr = streamname;//两个都是指针，都指向同一块内存，所以对destptr赋值，streamname也会跟着改变
	if (addMP4) //不执行
	{
		printf("addMP4");
		if (strncmp(ppstart, "mp4:", 4)) 
		{
			printf(" a");
			strcpy(destptr, "mp4:");
			destptr += 4;
		} 
		else 
		{
			printf(" b");
			subExt = 0;
		}
	} 
	else if (addMP3)//不执行
	{
		printf("addMP3");
		if (strncmp(ppstart, "mp3:", 4)) 
		{
			printf(" a");
			strcpy(destptr, "mp3:");
			destptr += 4;
		}
		else 
		{
			printf(" b");
			subExt = 0;
		}
	}
	printf("\n");
	printf("subExt=%d\n",subExt);//subExt=0，为初值0
 	for (p=(char *)ppstart; pplen >0;) //ppstart="stream"
	{
		/* skip extension */
		if (subExt && p == ext) //不执行
		{
			printf("x");
			p += 4;
			pplen -= 4;
			continue;
		}
		if (*p == '%') //不执行
		{printf("y");
			unsigned int c;
			sscanf(p+1, "%02x", &c);
			*destptr++ = c;
			pplen -= 3;
			p += 3;
		} 
		else //执行这里，总共6个z
		{printf("z");
			*destptr++ = *p++;
			pplen--;
		}
	}
	*destptr = '\0';//destptr=“stream”

	out->av_val = streamname;//streamname与destptr都指向同一个地方，所以streamname=“stream”
	out->av_len = destptr - streamname;
	printf("out->av_val=%s,out->av_len=%d\n",out->av_val,out->av_len);//out->av_val=stream,out->av_len=6
}
