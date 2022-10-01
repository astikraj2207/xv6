#include "types.h"
#include "stat.h"
#include "user.h"

void printChildStats(void) {
    int retime, rutime, stime;
    int expid = wait2(&retime, &rutime, &stime);
    printf(1, "%d: re:%d, ru:%d, s:%d\n", expid, retime, rutime, stime);
}

// The __asm__ volatile statement is used to prevent gcc from optimizing out the dummy loop

int main(void) {
    // Zombie TEST
    printf(1, "Zombie time test\n");
    int pid = fork();
    if (pid == 0) {
        for (int i = 0; i < 10000; ++i)
            for (int j = 0; j < 10000; ++j) __asm__ volatile("" : "+g"(j) : :);
        exit();
    }
    printChildStats();

    pid = fork();
    if (pid == 0) {
        for (int i = 0; i < 10000; ++i)
            for (int j = 0; j < 10000; ++j) __asm__ volatile("" : "+g"(j) : :);
        exit();
    }
    sleep(100);
    printChildStats();

    // Parent-Child Test
    printf(1, "\nParent Child Test\n");
    pid = fork();
    if (pid == 0) {
        pid = fork();
        if (pid == 0) {
            for (int i = 0; i < 10000; ++i) {
                for (int j = 0; j < 10000; ++j) {
                    __asm__ volatile("" : "+g"(j) : :);
                }
            }
            exit();
        }
        printChildStats();
        exit();
    }
    printChildStats();

    // Multiple processes Test
    printf(1, "\nMultiple processes Test\n");
    for (int n = 1; n < 8; ++n) {
        printf(1, "n=%d\n", n);
        int pnum = 0;
        for (; pnum < n; ++pnum) {
            int pid = fork();
            if (pid == 0) {
                for (int i = 0; i < 10000; ++i) {
                    for (int j = 0; j < 10000; ++j) {
                        __asm__ volatile("" : "+g"(j) : :);
                    }
                }
                exit();
            }
        }
        // sleep(100);
        for (; pnum > 0; --pnum) {
            printChildStats();
        }
    }
    exit();
}