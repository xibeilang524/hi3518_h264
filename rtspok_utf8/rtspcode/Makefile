INCLUDE=-I./include/
LIBS=./lib/libmpi.a ./lib/libive.a ./lib/libmd.a ./lib/libVoiceEngine.a ./lib/libupvqe.a ./lib/libdnvqe.a ./lib/libisp.a ./lib/libsns_ar0130.a ./lib/lib_cmoscfg.a ./lib/lib_iniparser.a ./lib/lib_hiae.a ./lib/lib_hiawb.a ./lib/lib_hiaf.a ./lib/lib_hidefog.a -lpthread -lm -ldl
all:rtsp
rtsp:
	arm-hisiv300-linux-gcc -o rtsp -Dhi3518e -DHICHIP=0x3518E200 -DSENSOR_TYPE=APTINA_AR0130_DC_720P_30FPS -DHI_RELEASE -DHI_XXXX -DCHIP_ID=CHIP_HI3518E_V200 -lpthread -DISP_V2 -Dhi3518ev200 -DHI_ACODEC_TYPE_INNER main.c ringfifo.c rtputils.c rtspservice.c rtsputils.c loadbmp.c sample_comm_audio.c sample_comm_isp.c sample_comm_sys.c sample_comm_ive.c sample_comm_venc.c sample_comm_vi.c sample_comm_vo.c sample_comm_vpss.c sample_venc.c $(INCLUDE) $(LIBS)
clean:
	rm -rfv rtsp
