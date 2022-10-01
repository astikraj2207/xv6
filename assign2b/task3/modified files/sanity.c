#include "types.h"
#include "stat.h"
#include "user.h"

char p_typename[3][10] = {"CPU", "S-CPU", "IO"};

int sum_sleep[3] = {0};
int sum_wait[3] = {0};
int sum_turn[3] = {0};

void printChildStats(void) {
    int retime, rutime, stime, ctime;
    int expid = wait2(&retime, &rutime, &stime, &ctime);
    if(expid < 0) printf(1, "Error receiving stats\n");
    else {
        int ptype = expid % 3;
        sum_sleep[ptype] += stime;
        sum_wait[ptype] += retime;
        sum_turn[ptype] += retime + rutime + stime;
        printf(1, "pid:%d %s w:%d ru:%d io:%d\n", expid, p_typename[ptype], retime, rutime, stime);
    }
}

int main(int argc, char *argv[]){
    if(argc <= 1){
        exit();
    }
    int n = atoi(argv[1]);
    int pid;
    for(int i=0; i<3*n; ++i){
        pid = fork();
        if(pid == 0){
            // Inside child, get own pid
            pid = getpid();
            if(pid % 3 == 0){
                for(int i = 0; i < 100; ++i)
                    for (int j = 0; j < 1000000; ++j)
                        __asm__ volatile("" : "+g"(j) : :);
                exit();
            }
            else if(pid % 3 == 1){
                for(int i = 0; i < 100; ++i){
                    for (int j = 0; j < 1000000; ++j)
                        __asm__ volatile("" : "+g"(j) : :);
                    yield();
                }
                exit();
            }
            else {
                for(int i = 0; i < 100; ++i)
                    sleep(1);
                exit();
            }
        }
    }

    for(int i=0; i<3*n; ++i){
        printChildStats();
    }

    for(int ptype=0; ptype<3; ++ptype){
        int avg_s = sum_sleep[ptype] / n;
        int avg_w = sum_wait[ptype] / n;
        int avg_t = sum_turn[ptype] / n;
        printf(1, "Group:%s: slp:%d, rdy:%d, trnarnd:%d\n", p_typename[ptype], avg_s, avg_w, avg_t);
    }
    
    exit();
}