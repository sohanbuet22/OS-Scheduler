#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char* argv[]){
    int pid = getpid();
    int entry_time = uptime();
    uint32 iters = atoi(argv[1]);
    setlength(iters);
    printf("Process %d: Starting %u iterations at time %d\n", pid, iters, entry_time);
    for(int i = 0; i < iters; i++){
        // do some dummy work
        for(int j = 0; j < 50000000; j++){
            int x = j * j;
            x = x + 1;
        }
    }
    int exit_time = uptime();
    printf("Process %d: Finished at time %d\n", pid, exit_time);
    exit(0);
}