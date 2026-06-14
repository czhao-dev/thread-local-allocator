// Freeing the same free-list pointer twice must be detected and aborted
// rather than silently corrupting the heap. Runs the double-free in a child
// process and checks that it was killed by SIGABRT.

#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>

#include "memalloc/free_list.h"

int main() {
    pid_t pid = fork();
    if (pid == 0) {
        std::freopen("/dev/null", "w", stderr);  // expected diagnostic, not a test failure
        memalloc::FreeListAllocator fl;
        void* p = fl.allocate(64);
        fl.deallocate(p);
        fl.deallocate(p);  // double free -> should abort
        _exit(0);          // should not be reached
    }

    int status = 0;
    waitpid(pid, &status, 0);

    if (WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT) {
        std::printf("double_free: OK (double free correctly aborted)\n");
        return 0;
    }
    std::fprintf(stderr, "expected double free to abort with SIGABRT, child status=%d\n", status);
    return 1;
}
