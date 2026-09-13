#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#define BYEL "\e[1;33m"
#define BRED "\e[1;31m"
#define CRESET "\e[0m"

int main(int argc, char* argv[]){
    int pid = getpid();
    uint32 iters = atoi(argv[1]);
    int priority = atoi(argv[2]);
    setpriority(priority);
    sleep(5); // to let the scheduler run and set the priority
    int entry_time = uptime();
    printf(BYEL "PID %d: Starting %u iterations at time %d. Initial priority: %d, current: %d\n" CRESET, pid, iters, entry_time, priority, getpriority());
    for(int i = 0; i < iters; i++){
        // do some dummy work
        for(int j = 0; j < 50000000; j++){
            int x = j * j;
            x = x + 1;
        }
    }
    int exit_time = uptime();
    printf(BRED "PID %d: Finished at time %d. Initial pr: %d, current: %d\n" CRESET, pid, exit_time, priority, getpriority());
    exit(0);
}