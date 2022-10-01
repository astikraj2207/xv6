#include "types.h"
#include "stat.h"
#include "user.h"

void printChildStats(void){
    int retime, rutime, stime, ctime;
    int pid = wait2(&retime, &rutime, &stime, &ctime);
    int termination_time = ctime + rutime + retime + stime;
    printf(1, "id:%d P:%d @ %d\n", pid, pid % 3 + 1, termination_time);
}

#ifdef SML
int main(void){
    set_prio(3);
    int n = 5;
    for(int i=0; i<3*n; ++i){
        if(fork() == 0){
            int pid = getpid();
            set_prio(pid % 3 + 1);
            for(int k = 0; k < 100; ++k)
                for (int j = 0; j < 1000000; ++j)
                    __asm__ volatile("" : "+g"(j) : :);
            exit();
        }
    }
    for(int i=0; i<3*n; ++i){
        printChildStats();
    }
    exit();
}
#else
    int main(void){
        printf(1, "Set schedflag to SML\n");
        exit();
    }
#endif