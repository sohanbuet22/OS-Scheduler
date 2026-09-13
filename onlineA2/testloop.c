#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define BOLD_YELLOW "\033[1;33m"
#define BOLD_RED "\033[1;31m"
#define RESET "\033[0m"

int main(int argc, char* argv[]){
    int pid = getpid();
    uint32 iters = atoi(argv[1]);
    int priority = atoi(argv[2]);
    setpriority(priority);
    sleep(5); // to let the scheduler run and set the priority
    int entry_time = uptime();
    printf(BOLD_YELLOW"PID %d (%d): Starting %u iterations at time %d\n"RESET, pid, priority, iters, entry_time);
    for(int i = 0; i < iters; i++){
        // do some dummy work
        if(i%10 == 0) 
        printf("PID %d (%d): Iteration %d\n", pid, priority, i); // comment out this line if needed, this is only for testing
        for(int j = 0; j < 50000000; j++){
            int x = j * j;
            x = x + 1;
        }
    }
    int exit_time = uptime();
    printf(BOLD_RED"PID %d (%d): Finished at time %d\n"RESET, pid, priority, exit_time);
    exit(0);
}