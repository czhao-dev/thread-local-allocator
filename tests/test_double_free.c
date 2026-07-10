/* Freeing the same free-list pointer twice must be detected and aborted
 * rather than silently corrupting the heap. Runs the double-free in a child
 * process and checks that it was killed by SIGABRT. */

#include <sys/wait.h>
#include <unistd.h>

#include <signal.h>
#include <stdio.h>

#include "memalloc/free_list.h"

int main(void) {
    pid_t pid = fork();
    if (pid == 0) {
        freopen("/dev/null", "w", stderr);  /* expected diagnostic, not a test failure */
        MemallocFreeList fl;
        memalloc_freelist_init(&fl);
        void* p = memalloc_freelist_allocate(&fl, 64);
        memalloc_freelist_deallocate(&fl, p);
        memalloc_freelist_deallocate(&fl, p);  /* double free -> should abort */
        _exit(0);                              /* should not be reached */
    }

    int status = 0;
    waitpid(pid, &status, 0);

    if (WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT) {
        printf("double_free: OK (double free correctly aborted)\n");
        return 0;
    }
    fprintf(stderr, "expected double free to abort with SIGABRT, child status=%d\n", status);
    return 1;
}
