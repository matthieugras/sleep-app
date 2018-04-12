#include "sleep.h"

struct rtc_time tm_to_rtc(struct tm arg){
    return (struct rtc_time){
        arg.tm_sec, arg.tm_min, arg.tm_hour,
        arg.tm_mday, arg.tm_mon, arg.tm_year,
        arg.tm_wday, arg.tm_yday, arg.tm_isdst
    };
}

struct tm rtc_to_tm(struct rtc_time arg){
    return (struct tm){
        arg.tm_sec, arg.tm_min, arg.tm_hour,
        arg.tm_mday, arg.tm_mon, arg.tm_year,
        arg.tm_wday, arg.tm_yday, arg.tm_isdst,
        0,NULL
    };
}

static struct rtc_time sa_rtc_rd_time(){
    struct rtc_time rtt;

    int fd;
    if((fd = open("/dev/rtc", O_RDONLY)) == -1){
        fprintf(stderr, "Error open(): %s", strerror(errno));
        exit(1);
    }
    
    if(ioctl(fd, RTC_RD_TIME, &rtt)){
        fprintf(stderr, "Error ioctl(): %s", strerror(errno));
        exit(1);
    }

    if(close(fd) == -1){
        fprintf(stderr, "Error close(): %s", strerror(errno));
        exit(1);
    }

    return rtt;
}

static void sa_rtc_set_wkalm_from_time(struct rtc_time rtt){
    struct rtc_wkalrm rtw;
    memset(&rtw, 0, sizeof(struct rtc_wkalrm));
    rtw.enabled = (unsigned char) 1;
    rtw.pending = (unsigned char) 1;
    rtw.time = rtt;
    fprintf(stdout, "UTC wake time kernel %2d:%2d\n", rtw.time.tm_hour, rtw.time.tm_min);
    fflush(stdout);

    int fd;
    if((fd = open("/dev/rtc", O_RDONLY)) == -1){
        fprintf(stderr, "Error open(): %s", strerror(errno));
        exit(1);
    }
    
    if(ioctl(fd, RTC_WKALM_SET, &rtw)){
        fprintf(stderr, "Error ioctl(): %s", strerror(errno));
        exit(1);
    }

    if(close(fd) == -1){
        fprintf(stderr, "Error close(): %s", strerror(errno));
        exit(1);
    }
}

static void sa_rtc_set_wkalm_from_sec(time_t sec){
    time_t conv_rtc_new = time(NULL) + sec;
    struct tm *new_tm = gmtime(&conv_rtc_new);
    fprintf(stdout, "DEBUG: %2d:%2d\n", new_tm->tm_hour, new_tm->tm_min);
    fflush(stdout);
    struct rtc_time new_rtc = tm_to_rtc(*new_tm);
    sa_rtc_set_wkalm_from_time(new_rtc);
}

void put_to_wk_sleep(int tm_hour, int tm_min){
    time_t now;
    time(&now);
    struct tm *now_struct = localtime(&now);
    
    int day_offset = 86400;

    now_struct->tm_hour = tm_hour;
    now_struct->tm_min = tm_min;

    time_t wk_time = mktime(now_struct);
    time_t t_diff = wk_time - now;
    if(t_diff < 0){
        fprintf(stdout, "put_to_wk_sleep(): Time difference is negative. Setting for next day.\n");
        t_diff += day_offset;
    }

    fprintf(stdout, "put_to_wk_sleep(): Calculated wakeup time +%lu seconds\n", t_diff);

    sa_rtc_set_wkalm_from_sec(t_diff);
    FILE *f;
    if(!(f = fopen("/sys/power/state", "w"))){
        fprintf(stderr, "Error fopen(): %s", strerror(errno));
        exit(1);
    }
    if(fwrite("mem", 1, 3, f) != 3){
        fprintf(stderr, "Error fwrite(): %s", strerror(ferror(f)));
        exit(1);
    }
    fflush(f);
    fclose(f);
}