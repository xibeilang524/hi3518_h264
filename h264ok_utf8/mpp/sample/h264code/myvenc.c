#ifdef __cplusplus
#if __cplusplus
extern "C"{
#endif
#endif /* End of #ifdef __cplusplus */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include "hi_mipi.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <math.h>
#include <time.h>

#include "hi_common.h"
#include "sample_comm.h"
static combo_dev_attr_t MIPI_CMOS3V3_ATTR =
{
    /* input mode */
    .input_mode = INPUT_MODE_CMOS_33V,
    {    
    }
};

/*9M034 DC 12bit input 720P@30fps*/
static VI_DEV_ATTR_S DEV_ATTR_9M034_DC_720P_BASE =
{
	/* interface mode */
	VI_MODE_DIGITAL_CAMERA,
	/* multiplex mode */
	VI_WORK_MODE_1Multiplex,
	/* r_mask    g_mask    b_mask*/
	{0xFFF0000,    0x0},
	/* progessive or interleaving */
	VI_SCAN_PROGRESSIVE,
	/*AdChnId*/
	{-1, -1, -1, -1},
	/*enDataSeq, only support yuv*/
	VI_INPUT_DATA_YUYV,

	/* synchronization information */
	{
	/*port_vsync   port_vsync_neg     port_hsync        port_hsync_neg        */
	VI_VSYNC_PULSE, VI_VSYNC_NEG_LOW, VI_HSYNC_VALID_SINGNAL,VI_HSYNC_NEG_HIGH,VI_VSYNC_VALID_SINGAL,VI_VSYNC_VALID_NEG_HIGH,

	/*hsync_hfb    hsync_act    hsync_hhb*/
	{370,            1280,        0,
	/*vsync0_vhb vsync0_act vsync0_hhb*/
	6,            720,        6,
	/*vsync1_vhb vsync1_act vsync1_hhb*/
	0,            0,            0}
	},
	/* use interior ISP */
	VI_PATH_ISP,
	/* input data type */
	VI_DATA_TYPE_RGB,
	/* Data Reverse */
	HI_FALSE,
	{0, 0, 1280, 720}
};

VIDEO_NORM_E gs_enNorm = VIDEO_ENCODING_MODE_NTSC;
HI_U32 g_u32BlkCnt = 4;
static char filename[200]={0};
static pthread_t gs_VencPid;
static SAMPLE_VENC_GETSTREAM_PARA_S gs_stPara;

HI_VOID* VENC_GetVencStreamProc(HI_VOID *p);
void SAMPLE_VENC_HandleSig(HI_S32 signo);
HI_S32 H264_Venc(HI_VOID);
void gettime(char* buf);

/******************************************************************************
* function : to process abnormal case                                         
******************************************************************************/
void SAMPLE_VENC_HandleSig(HI_S32 signo)
{
    if (SIGINT == signo || SIGTERM == signo)
    {
        SAMPLE_COMM_ISP_Stop();
        SAMPLE_COMM_SYS_Exit();
        printf("\033[0;31mprogram termination abnormally!\033[0;39m\n");
    }
    exit(-1);
}



HI_S32 H264_Venc(HI_VOID)
{
//为了简单点，编码类型就选择PT_H264，图片大小就选择PIC_HD720，通道也只用1路s32ChnNum=1
	PAYLOAD_TYPE_E enPayLoad=PT_H264;//264编码
	PIC_SIZE_E	enSize=PIC_HD720;//摄像头拍摄图片的大小，这里只用720P
	HI_S32	s32ChnNum=1;//支持一路摄像
	HI_S32 s32Ret=HI_FAILURE;

	/******************************************
	mpp system init. 
	******************************************/
	HI_MPI_SYS_Exit();
	HI_MPI_VB_Exit();
	
	VB_CONF_S stVbConf;//缓存池参数结构体
	HI_U32 u32BlkSize;//一张图片占多少字节
	memset(&stVbConf,0,sizeof(VB_CONF_S));
	//根据制式，图片大小，图片格式及对齐方式确定图片缓存大小
	//这里用NTSC,720P,YUV420,64字节对齐
	u32BlkSize=SAMPLE_COMM_SYS_CalcPicVbBlkSize(gs_enNorm,enSize, PIXEL_FORMAT_YUV_SEMIPLANAR_420, SAMPLE_SYS_ALIGN_WIDTH);
	printf("u32BlkSize=%d\n",u32BlkSize);
	stVbConf.u32MaxPoolCnt = 128;//用默认值，3518默认是128
	stVbConf.astCommPool[0].u32BlkSize = u32BlkSize;
	stVbConf.astCommPool[0].u32BlkCnt = g_u32BlkCnt;
	s32Ret = HI_MPI_VB_SetConf(&stVbConf);//设置 MPP 视频缓存池属性
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("HI_MPI_VB_SetConf failed!\n");
		return HI_FAILURE;
	}
	s32Ret = HI_MPI_VB_Init();//初始化 MPP 视频缓存池
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("HI_MPI_VB_Init failed!\n");
		return HI_FAILURE;
	}
	//定义结构体，对结构体赋值，然后设置，最后初始化
	MPP_SYS_CONF_S stSysConf = {0};
	stSysConf.u32AlignWidth = SAMPLE_SYS_ALIGN_WIDTH;
	s32Ret = HI_MPI_SYS_SetConf(&stSysConf);//配置系统控制参数
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("HI_MPI_SYS_SetConf failed\n");
		return HI_FAILURE;
	}
	s32Ret = HI_MPI_SYS_Init();//初始化 MPP 系统
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("HI_MPI_SYS_Init failed!\n");
		return HI_FAILURE;
	}

	/******************************************
	打开mipi
	******************************************/
	HI_S32 fdmipi;
	combo_dev_attr_t *pstcomboDevAttr = NULL;
	fdmipi = open("/dev/hi_mipi", O_RDWR);
	if (fdmipi < 0)
	{
		printf("warning: open hi_mipi dev failed\n");
		return -1;
	}
	pstcomboDevAttr = &MIPI_CMOS3V3_ATTR;//AR0130模组属性
	if (ioctl(fdmipi, HI_MIPI_SET_DEV_ATTR, pstcomboDevAttr))
	{
		printf("set mipi attr failed\n");
		close(fdmipi);
	return HI_FAILURE;
	}
	close(fdmipi);

	HI_S32 i,ViChn=0;//只有一路输出，所以ViChn=0
	//设置输入配置参数
	SAMPLE_VI_CONFIG_S stViConfig = {0};
	stViConfig.enViMode   = APTINA_AR0130_DC_720P_30FPS;//摄像头是AR0130模组
	stViConfig.enRotate   = ROTATE_NONE;//不翻转
	stViConfig.enNorm     = VIDEO_ENCODING_MODE_AUTO;//编码模式自动
	stViConfig.enViChnSet = VI_CHN_SET_NORMAL;//普通
	stViConfig.enWDRMode  = WDR_MODE_NONE;//不设置宽动态

	VI_DEV_ATTR_S  stViDevAttr;
	VI_DEV ViDev=0;//只有一个摄像头设备，所以设备序号为0
	ISP_DEV s32IspDev=0;//同样，ISP序号也为0
	memset(&stViDevAttr,0,sizeof(stViDevAttr));
	memcpy(&stViDevAttr,&DEV_ATTR_9M034_DC_720P_BASE,sizeof(stViDevAttr));
	/******************************************
	step 1: mipi configure
	******************************************/
	/*
	s32Ret = SAMPLE_COMM_VI_StartMIPI(&stViConfig);
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("%s: MIPI init failed!\n", __FUNCTION__);
		return HI_FAILURE;
	}  
	*/
	/******************************************
	step 2: configure sensor and ISP (include WDR mode).
	note: you can jump over this step, if you do not use Hi3516A interal isp. 
	//虽然说了不用，但还要需要，因为后面HI_MPI_ISP_GetWDRMode函数会调用ISP_CHECK_MEM_INIT
	//检测内存，所以还是在这里使用
	******************************************/
	s32Ret = SAMPLE_COMM_ISP_Init(stViConfig.enWDRMode);
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("%s: Sensor init failed!\n", __FUNCTION__);
		return HI_FAILURE;
	}
	/******************************************
	step 3: run isp thread 
	note: you can jump over this step, if you do not use Hi3516A interal isp.
	******************************************/
	s32Ret = SAMPLE_COMM_ISP_Run();
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("%s: ISP init failed!\n", __FUNCTION__);
		/* disable videv */
		return HI_FAILURE;
	}
	
	s32Ret=HI_MPI_VI_SetDevAttr(ViDev, &stViDevAttr);//只有一个设备，所以设备号为0
	if (s32Ret != HI_SUCCESS)
	{
		SAMPLE_PRT("HI_MPI_VI_SetDevAttr failed with %#x!\n", s32Ret);
		return HI_FAILURE;
	}
	ISP_WDR_MODE_S stWdrMode;
	s32Ret = HI_MPI_ISP_GetWDRMode(s32IspDev, &stWdrMode);
	if (s32Ret != HI_SUCCESS)
	{
		SAMPLE_PRT("HI_MPI_ISP_GetWDRMode failed with %#x!\n", s32Ret);
		return HI_FAILURE;
	}
	VI_WDR_ATTR_S stWdrAttr;
	stWdrAttr.enWDRMode = stWdrMode.enWDRMode;
	stWdrAttr.bCompress = HI_FALSE;
	s32Ret = HI_MPI_VI_SetWDRAttr(ViDev, &stWdrAttr);
	if (s32Ret)
	{
		SAMPLE_PRT("HI_MPI_VI_SetWDRAttr failed with %#x!\n", s32Ret);
		return HI_FAILURE;
	}
	s32Ret = HI_MPI_VI_EnableDev(ViDev);
	if (s32Ret != HI_SUCCESS)
	{
		SAMPLE_PRT("HI_MPI_VI_EnableDev failed with %#x!\n", s32Ret);
		return HI_FAILURE;
	}


	RECT_S stCapRect;
	SIZE_S stTargetSize;
	stCapRect.s32X = 0;
       stCapRect.s32Y = 0;
	stCapRect.u32Width = 1280;
       stCapRect.u32Height = 720;//AR030输出1280*720图像
       stTargetSize.u32Width = stCapRect.u32Width;
       stTargetSize.u32Height = stCapRect.u32Height;
	//设置通道属性
	VI_CHN_ATTR_S stChnAttr;
	memcpy(&stChnAttr.stCapRect, &stCapRect, sizeof(RECT_S));
	stChnAttr.enCapSel = VI_CAPSEL_BOTH;
	stChnAttr.stDestSize.u32Width = stTargetSize.u32Width;
	stChnAttr.stDestSize.u32Height = stTargetSize.u32Height;
	stChnAttr.enPixFormat = PIXEL_FORMAT_YUV_SEMIPLANAR_420; 
	stChnAttr.bMirror = HI_FALSE;
	stChnAttr.bFlip = HI_FALSE;
	stChnAttr.s32SrcFrameRate = -1;
	stChnAttr.s32DstFrameRate = -1;
	stChnAttr.enCompressMode = COMPRESS_MODE_NONE;
	s32Ret = HI_MPI_VI_SetChnAttr(ViChn, &stChnAttr);//设置通道属性
	if (s32Ret != HI_SUCCESS)
	{
		SAMPLE_PRT("in HI_MPI_VI_SetChnAttr failed with %#x!\n", s32Ret);
		SAMPLE_COMM_ISP_Stop();
		return HI_FAILURE;
	}
	s32Ret = HI_MPI_VI_EnableChn(ViChn);//使能通道
	if (s32Ret != HI_SUCCESS)
	{
		SAMPLE_PRT("HI_MPI_VI_EnableChn failed with %#x!\n", s32Ret);
		SAMPLE_COMM_ISP_Stop();
		return HI_FAILURE;
	}
	SIZE_S stSize;
	stSize.u32Width  = 1280;
	stSize.u32Height = 720;

	VPSS_GRP_ATTR_S stVpssGrpAttr;
	VPSS_NR_PARAM_U unNrParam = {{0}};
	VPSS_GRP VpssGrp=0;//只有一个设备，序号为0
	stVpssGrpAttr.u32MaxW = stSize.u32Width;
	stVpssGrpAttr.u32MaxH = stSize.u32Height;
	stVpssGrpAttr.bIeEn = HI_FALSE;
	stVpssGrpAttr.bNrEn = HI_TRUE;
	stVpssGrpAttr.bHistEn = HI_FALSE;
	stVpssGrpAttr.bDciEn = HI_FALSE;
	stVpssGrpAttr.enDieMode = VPSS_DIE_MODE_NODIE;
	stVpssGrpAttr.enPixFmt = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
	s32Ret = HI_MPI_VPSS_CreateGrp(VpssGrp, &stVpssGrpAttr);//创建组的同时将组的属性设置进去
	if (s32Ret != HI_SUCCESS)
	{
		SAMPLE_PRT("HI_MPI_VPSS_CreateGrp failed with %#x!\n", s32Ret);
		return HI_FAILURE;
	}
	s32Ret = HI_MPI_VPSS_GetNRParam(VpssGrp, &unNrParam);
	if (s32Ret != HI_SUCCESS)
	{
		SAMPLE_PRT("HI_MPI_VPSS_GetNRParam failed with %#x!\n", s32Ret);
		return HI_FAILURE;
	}
	s32Ret = HI_MPI_VPSS_SetNRParam(VpssGrp, &unNrParam);
	if (s32Ret != HI_SUCCESS)
	{
		SAMPLE_PRT("HI_MPI_VPSS_SetNRParam failed with %#x!\n", s32Ret);
		return HI_FAILURE;
	}
	s32Ret = HI_MPI_VPSS_StartGrp(VpssGrp);//启动VPSS组
	if (s32Ret != HI_SUCCESS)
	{
		SAMPLE_PRT("HI_MPI_VPSS_StartGrp failed with %#x\n", s32Ret);
		return HI_FAILURE;
	}

	SAMPLE_VI_PARAM_S stViParam;
	stViParam.s32ViDevCnt      = 1;
	stViParam.s32ViDevInterval = 1;
	stViParam.s32ViChnCnt      = 1;
	stViParam.s32ViChnInterval = 1;
	MPP_CHN_S stSrcChn;
    	MPP_CHN_S stDestChn;
	stSrcChn.enModId  = HI_ID_VIU;
	stSrcChn.s32DevId = 0;
	stSrcChn.s32ChnId = ViChn;

	stDestChn.enModId  = HI_ID_VPSS;
	stDestChn.s32DevId = VpssGrp;
	stDestChn.s32ChnId = 0;
	s32Ret = HI_MPI_SYS_Bind(&stSrcChn, &stDestChn);//将VI的0通道与VPSS的组的0通道绑定起来
	if (s32Ret != HI_SUCCESS)
	{
		SAMPLE_PRT("failed with %#x!\n", s32Ret);
		return HI_FAILURE;
	}
	//设置VPSS通道，只有一个通道，所以VpssChn=0
	VPSS_CHN VpssChn=0;
	VPSS_CHN_ATTR_S stVpssChnAttr;
	VPSS_CHN_MODE_S stVpssChnMode;
	stVpssChnMode.enChnMode      = VPSS_CHN_MODE_USER;
	stVpssChnMode.bDouble        = HI_FALSE;
	stVpssChnMode.enPixelFormat  = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
	stVpssChnMode.u32Width       = stSize.u32Width;
	stVpssChnMode.u32Height      = stSize.u32Height;
	stVpssChnMode.enCompressMode = COMPRESS_MODE_SEG;
	memset(&stVpssChnAttr, 0, sizeof(stVpssChnAttr));
	stVpssChnAttr.s32SrcFrameRate = -1;
	stVpssChnAttr.s32DstFrameRate = -1;
	s32Ret = HI_MPI_VPSS_SetChnAttr(VpssGrp, VpssChn, &stVpssChnAttr);
	if (s32Ret != HI_SUCCESS)
	{
		SAMPLE_PRT("HI_MPI_VPSS_SetChnAttr failed with %#x\n", s32Ret);
		return HI_FAILURE;
	}
	s32Ret = HI_MPI_VPSS_SetChnMode(VpssGrp, VpssChn, &stVpssChnMode);
	if (s32Ret != HI_SUCCESS)
	{
		SAMPLE_PRT("%s failed with %#x\n", __FUNCTION__, s32Ret);
		return HI_FAILURE;
	}  
	s32Ret = HI_MPI_VPSS_EnableChn(VpssGrp, VpssChn);
	if (s32Ret != HI_SUCCESS)
	{
		SAMPLE_PRT("HI_MPI_VPSS_EnableChn failed with %#x\n", s32Ret);
		return HI_FAILURE;
	}
	//定义结构体，给结构体赋值，创建通道同时将结构体设置进去
	SAMPLE_RC_E enRcMode= SAMPLE_RC_CBR;
	VENC_CHN VencChn=0;
	VENC_CHN_ATTR_S stVencChnAttr;
	VENC_ATTR_H264_S stH264Attr;
	VENC_ATTR_H264_CBR_S    stH264Cbr;
	stVencChnAttr.stVeAttr.enType =enPayLoad;
	stH264Attr.u32MaxPicWidth =stSize.u32Width;
	stH264Attr.u32MaxPicHeight = stSize.u32Height;
	stH264Attr.u32PicWidth = stSize.u32Width;/*the picture width*/
	stH264Attr.u32PicHeight = stSize.u32Height;/*the picture height*/
	stH264Attr.u32BufSize  = stSize.u32Width * stSize.u32Height;/*stream buffer size*/
	stH264Attr.u32Profile  = 0;/*0: baseline; 1:MP; 2:HP;  3:svc_t */
	stH264Attr.bByFrame = HI_TRUE;/*get stream mode is slice mode or frame mode?*/
	stH264Attr.u32BFrameNum = 0;/* 0: not support B frame; >=1: number of B frames */
	stH264Attr.u32RefNum = 1;/* 0: default; number of refrence frame*/
	memcpy(&stVencChnAttr.stVeAttr.stAttrH264e, &stH264Attr, sizeof(VENC_ATTR_H264_S));
	
	stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
	stH264Cbr.u32Gop            = (VIDEO_ENCODING_MODE_PAL== gs_enNorm)?25:30;
	stH264Cbr.u32StatTime       = 1; /* stream rate statics time(s) */
	stH264Cbr.u32SrcFrmRate      = (VIDEO_ENCODING_MODE_PAL== gs_enNorm)?25:30;/* input (vi) frame rate */
	stH264Cbr.fr32DstFrmRate = (VIDEO_ENCODING_MODE_PAL== gs_enNorm)?25:30;/* target frame rate */
	stH264Cbr.u32BitRate = 1024*2;
	stH264Cbr.u32FluctuateLevel = 0; /* average bit rate */
	memcpy(&stVencChnAttr.stRcAttr.stAttrH264Cbr, &stH264Cbr, sizeof(VENC_ATTR_H264_CBR_S));
	s32Ret = HI_MPI_VENC_CreateChn(VencChn, &stVencChnAttr);//创建通道同时将结构体设置进去
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("HI_MPI_VENC_CreateChn [%d] faild with %#x!\n",VencChn, s32Ret);
		return s32Ret;
	}
	s32Ret = HI_MPI_VENC_StartRecvPic(VencChn);//开始接收图片
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("HI_MPI_VENC_StartRecvPic faild with%#x!\n", s32Ret);
		return HI_FAILURE;
	}
	//MPP_CHN_S stSrcChn;
	//MPP_CHN_S stDestChn;
	//将VPSS通道作为源通道与VENC通道作为目的通道绑定起来
	stSrcChn.enModId = HI_ID_VPSS;
	stSrcChn.s32DevId = VpssGrp;
	stSrcChn.s32ChnId = VpssChn;
	stDestChn.enModId = HI_ID_VENC;
	stDestChn.s32DevId = 0;
	stDestChn.s32ChnId = VencChn;
	s32Ret=HI_MPI_SYS_Bind(&stSrcChn, &stDestChn);
	if (s32Ret != HI_SUCCESS)
	{
		SAMPLE_PRT("failed with %#x!\n", s32Ret);
		return HI_FAILURE;
	}
	gs_stPara.bThreadStart = HI_TRUE;
	gs_stPara.s32Cnt = s32ChnNum;
	pthread_create(&gs_VencPid, 0, VENC_GetVencStreamProc, (HI_VOID*)&gs_stPara);
	
	printf("please press twice ENTER to exit this sample\n");
	//getchar（）函数的执行模式是阻塞式的，当需要接收字符流的时候，当前线程就会
	//被挂起，其后的所有代码均要等待用户输入回车表示输入完毕后，线程才会被调
	//度进入CPU时钟内执行其余的代码
	getchar();
	getchar();
	if (HI_TRUE == gs_stPara.bThreadStart)
	{
		gs_stPara.bThreadStart = HI_FALSE;
		pthread_join(gs_VencPid, 0);
	}

	stSrcChn.enModId = HI_ID_VPSS;
	stSrcChn.s32DevId = VpssGrp;
	stSrcChn.s32ChnId = VpssChn;
	stDestChn.enModId = HI_ID_VENC;
	stDestChn.s32DevId = 0;
	stDestChn.s32ChnId = VencChn;
	s32Ret = HI_MPI_SYS_UnBind(&stSrcChn, &stDestChn);//先解绑VPSS和VENC的绑定，后面再解绑VPSS和VI的绑定
	if (s32Ret != HI_SUCCESS)
	{
		SAMPLE_PRT("failed with %#x!\n", s32Ret);
		return HI_FAILURE;
	}
	s32Ret = HI_MPI_VENC_StopRecvPic(VencChn);//前面开启了接收图片，这里就要停止
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("HI_MPI_VENC_StopRecvPic vechn[%d] failed with %#x!\n",VencChn, s32Ret);
		return HI_FAILURE;
	}

	/******************************************
	Distroy Venc Channel
	******************************************/
	s32Ret = HI_MPI_VENC_DestroyChn(VencChn);
	//前面创建了VENC通道，这里就要销毁VENC通道，而且最先销毁，后面再销毁VI通道
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("HI_MPI_VENC_DestroyChn vechn[%d] failed with %#x!\n",VencChn, s32Ret);
		return HI_FAILURE;
	}
	stSrcChn.enModId = HI_ID_VIU;
	stSrcChn.s32DevId = ViDev;
	stSrcChn.s32ChnId = ViChn;
	stDestChn.enModId = HI_ID_VPSS;
	stDestChn.s32DevId = VpssGrp;
	stDestChn.s32ChnId = 0;
	s32Ret = HI_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
	//后面再解绑VPSS和VI的绑定,在解绑VPSS和VENC的绑定之后，顺序与绑定顺序相反
	if (s32Ret != HI_SUCCESS)
	{
		SAMPLE_PRT("failed with %#x!\n", s32Ret);
		return HI_FAILURE;
	}
	s32Ret = HI_MPI_VPSS_DisableChn(VpssGrp, VpssChn);//失能VPSS组和VPSS通道
	if (s32Ret != HI_SUCCESS)
	{
		SAMPLE_PRT("%s failed with %#x\n", __FUNCTION__, s32Ret);
		return HI_FAILURE;
	}
	s32Ret = HI_MPI_VPSS_StopGrp(VpssGrp);//停止VPSS组
	if (s32Ret != HI_SUCCESS)
	{
		SAMPLE_PRT("%s failed with %#x\n", __FUNCTION__, s32Ret);
		return HI_FAILURE;
	}
	s32Ret = HI_MPI_VPSS_DestroyGrp(VpssGrp);//销毁组
	if (s32Ret != HI_SUCCESS)
	{
		SAMPLE_PRT("%s failed with %#x\n", __FUNCTION__, s32Ret);
		return HI_FAILURE;
	}
	s32Ret = HI_MPI_VI_DisableChn(ViChn);//失能通道
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("HI_MPI_VI_DisableChn failed with %#x\n",s32Ret);
		return HI_FAILURE;
	}
	s32Ret = HI_MPI_VI_DisableDev(ViDev);//失能VI设备即摄像头
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("HI_MPI_VI_DisableDev failed with %#x\n", s32Ret);
		return HI_FAILURE;
	}
	SAMPLE_COMM_ISP_Stop();//停止ISP
	HI_MPI_SYS_Exit();//去初始化
	HI_MPI_VB_Exit();
	//由以上几个步骤可以看出，设置与销毁顺序相反，而且是成对出现
	return HI_SUCCESS;
}



HI_VOID* VENC_GetVencStreamProc(HI_VOID *p)
{
	SAMPLE_VENC_GETSTREAM_PARA_S *pstPara;
	HI_S32 s32ChnTotal;
	VENC_CHN VencChn;
	HI_S32 s32Ret;
	PAYLOAD_TYPE_E enPayLoadType[VENC_MAX_CHN_NUM];
	HI_CHAR aszFileName[VENC_MAX_CHN_NUM][64];
	VENC_CHN_ATTR_S stVencChnAttr;
	struct timeval TimeoutVal;
	fd_set read_fds;
	HI_S32 VencFd[VENC_MAX_CHN_NUM],maxfd;
	VENC_STREAM_S stStream;
	VENC_CHN_STAT_S stStat;
	FILE *pFile[VENC_MAX_CHN_NUM];
	HI_S32 i;

	pstPara = (SAMPLE_VENC_GETSTREAM_PARA_S*)p;
	s32ChnTotal = pstPara->s32Cnt;//pstPara->s32Cnt是由参数传进来的，为1
	for (i = 0; i < s32ChnTotal; i++)
	{
		VencChn = i;
		s32Ret = HI_MPI_VENC_GetChnAttr(VencChn, &stVencChnAttr);
		//这里是为了取得是什么编码类型，以便确定保存文件的后缀名
		//比如这里是H264编码，所以保存文件的后缀后就是.h264
		if(s32Ret != HI_SUCCESS)
		{
			SAMPLE_PRT("HI_MPI_VENC_GetChnAttr chn[%d] failed with %#x!\n", \
			VencChn, s32Ret);
			return NULL;
		}
		enPayLoadType[i] = stVencChnAttr.stVeAttr.enType;
		gettime(filename);//获取时间如20171101085639为文件名
		sprintf(aszFileName[i], "%s_%d%s",filename, i, ".h264");//20171101085639.h264
		
		pFile[i] = fopen(aszFileName[i], "wb");
		if (!pFile[i])
		{
			SAMPLE_PRT("open file[%s] failed!\n", aszFileName[i]);
			return NULL;
		}
		VencFd[i] = HI_MPI_VENC_GetFd(i);//获取文件句柄，以便后面能用select来IO复用
		if (VencFd[i] < 0)
		{
			SAMPLE_PRT("HI_MPI_VENC_GetFd failed with %#x!\n", VencFd[i]);
			return NULL;
		}
		if (maxfd <= VencFd[i])
		{
			maxfd = VencFd[i];
		}
	}

	while (HI_TRUE == pstPara->bThreadStart)
	//当main函数所在的线程接收到两个键盘字符或ctrl+c时，pstPara->bThreadStart会为假，跳出while循环
	//然后往下执行关闭前面打开的文件，执行完这个VENC_GetVencStreamProc线程函数，线程结束
	{
	/*IO复用4步骤
		1.清空文件集合FD_ZERO(&read_fds);
		2.将文件加入文件集合FD_SET(VencFd[i], &read_fds);
		3.设置超时时间并用select(maxfd + 1, &read_fds, NULL, NULL, &TimeoutVal)来等待文件状态有变化唤醒线程
		或超时唤醒
		4.FD_ISSET查询文件状态是否有变化，有变化则处理
	*/
		FD_ZERO(&read_fds);
		for (i = 0; i < s32ChnTotal; i++)
		{
			FD_SET(VencFd[i], &read_fds);
		}
		TimeoutVal.tv_sec  = 2;
		TimeoutVal.tv_usec = 0;
		s32Ret = select(maxfd + 1, &read_fds, NULL, NULL, &TimeoutVal);
		if (s32Ret < 0)
		{
			SAMPLE_PRT("select failed!\n");
			break;
		}
		else if (s32Ret == 0)
		{
			SAMPLE_PRT("get venc stream time out, exit thread\n");
			continue;
		}
		else
		{
			for (i = 0; i < s32ChnTotal; i++)
			{
				if (FD_ISSET(VencFd[i], &read_fds))
				{
					memset(&stStream, 0, sizeof(stStream));
					s32Ret = HI_MPI_VENC_Query(i, &stStat);//查询是否有码流，并将码流信息填充到stStat结构体中
					if (HI_SUCCESS != s32Ret)
					{
						SAMPLE_PRT("HI_MPI_VENC_Query chn[%d] failed with %#x!\n", i, s32Ret);
						break;
					}
					if(0 == stStat.u32CurPacks)
					{
						SAMPLE_PRT("NOTE: Current  frame is NULL!\n");
						continue;
					}
					stStream.pstPack = (VENC_PACK_S*)malloc(sizeof(VENC_PACK_S) * stStat.u32CurPacks);
					//分配内存以便保存码流包数据
					if (NULL == stStream.pstPack)
					{
						SAMPLE_PRT("malloc stream pack failed!\n");
						break;
					}

					stStream.u32PackCount = stStat.u32CurPacks;
					//printf("stStream.u32PackCount=%d\n",stStream.u32PackCount);
					s32Ret = HI_MPI_VENC_GetStream(i, &stStream, HI_TRUE);//获取码流数据并保存到stStream结构体中
					if (HI_SUCCESS != s32Ret)
					{
						free(stStream.pstPack);//获取失败则要释放前面分配的内存，否则会造成内存溢出
						stStream.pstPack = NULL;
						SAMPLE_PRT("HI_MPI_VENC_GetStream failed with %#x!\n", s32Ret);
					break;
					}
					HI_S32 u32PackIndex;
					for (u32PackIndex= 0;u32PackIndex < stStream.u32PackCount; u32PackIndex++)
					{
						fwrite(	stStream.pstPack[u32PackIndex].pu8Addr+stStream.pstPack[u32PackIndex].u32Offset,\
								stStream.pstPack[u32PackIndex].u32Len-  stStream.pstPack[u32PackIndex].u32Offset, \
								1, pFile[i]);
						fflush(pFile[i]);
						#if 1
						printf("stStream.u32PackCount=%d,stStream.pstPack[%d].pu8Addr=0x%08x,\
							stStream.pstPack[%d].u32Offset=%d,stStream.pstPack[%d].u32Len=%d\n",\
							stStream.u32PackCount,u32PackIndex,stStream.pstPack[u32PackIndex].pu8Addr,\
							u32PackIndex,stStream.pstPack[u32PackIndex].u32Offset,u32PackIndex,stStream.pstPack[u32PackIndex].u32Len);
						//添加打印信息，查看保存码流内容的内存是怎么样的
						#endif
					}

					s32Ret = HI_MPI_VENC_ReleaseStream(i, &stStream);//保存后要释放码流
					if (HI_SUCCESS != s32Ret)
					{
						free(stStream.pstPack);//获取失败则要释放前面分配的内存，否则会造成内存溢出
						stStream.pstPack = NULL;
						break;
					}

					free(stStream.pstPack);//释放码流后，也要释放分配的内存，避免内存溢出
					stStream.pstPack = NULL;

				}
			}
		}

	}

	for (i = 0; i < s32ChnTotal; i++)
	{
		fclose(pFile[i]);
	}
	return NULL;
}
void gettime(char* buf)
{
        char szContentBuf[200] ="";
        time_t timep;
        struct tm *p;
        time(&timep);
        p=gmtime(&timep);
        sprintf(szContentBuf,"%04d%02d%02d%02d%02d%02d",(1900+p->tm_year), (1+p->tm_mon),p->tm_mday,p->tm_hour,p->tm_min,p->tm_sec);
        strcpy(buf,szContentBuf);//,buf,strlen(szContentBuf));
        //return szContentBuf;
//printf("%s\n", szContentBuf);
}

//./myvenc 
int main(int argc, char *argv[])//main()
{
	HI_S32 s32Ret;
	signal(SIGINT, SAMPLE_VENC_HandleSig);//ctrl+c,delete
	signal(SIGTERM, SAMPLE_VENC_HandleSig);//shell命令kill缺省产生这个信号.
	s32Ret = H264_Venc();
			
	if(s32Ret==HI_SUCCESS)
		printf("normally\n");
	else
		printf("unnormally\n");
	return -1;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */

