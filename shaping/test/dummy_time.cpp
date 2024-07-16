#include <time.h>

static unsigned long long curr_time_ns = 2000000000;//2s
static unsigned int curr_time_s = 0;

void stub_curr_time_ns_inc(unsigned long long time_ns)
{
    curr_time_ns += time_ns;

    return;
}

void stub_curr_time_s_inc(int time_s)
{
    curr_time_s += time_s;

    return;
}

unsigned long long stub_curr_time_ns_get()
{
    return curr_time_ns;
}

/****************stub of time.h****************/

int clock_gettime (clockid_t __clock_id, struct timespec *__tp)
{
    __tp->tv_sec = curr_time_s;
    __tp->tv_nsec = curr_time_ns;

    return 0;
}

/**********************************************/