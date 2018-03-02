#include <time.h>
gettime(char* buf)//main()
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
char filename[200]={0};
main()
{
        //filename=gettime();
        printf("%s\n",gettime(filename));
}
