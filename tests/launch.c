/* watch_launch: starts the compiled planner under a seccomp filter. The plan has to come out of
   the C++ that was built, so a planner that carries machine code of its own and runs it through
   an unnamed file (memfd_create, then execveat on the descriptor) is refused here with EPERM, as
   are a few shared-memory neighbours of that route. Every other call is allowed, so reading the
   survey and writing the plan are untouched. The filter needs no privilege beyond no-new-privs,
   and if it will not install the planner is never started. */
#define _GNU_SOURCE
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>

#ifndef SECCOMP_RET_KILL_PROCESS
#define SECCOMP_RET_KILL_PROCESS 0x80000000U
#endif

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "watch_launch: nothing to run\n");
        return 2;
    }

    struct sock_filter filter[] = {
        /* keep this to the architecture we built for; anything else is refused outright */
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_X86_64, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
        /* load the syscall number */
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
        /* the anonymous-executable route, and neighbours, all jump to the deny leaf */
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_memfd_create, 5, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_execveat,     4, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_shmget,       3, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_shmat,        2, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_userfaultfd,  1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)),
    };
    struct sock_fprog prog = {
        .len = (unsigned short)(sizeof(filter) / sizeof(filter[0])),
        .filter = filter,
    };

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        fprintf(stderr, "watch_launch: PR_SET_NO_NEW_PRIVS failed: %s\n", strerror(errno));
        return 111;
    }
    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog, 0, 0) != 0) {
        fprintf(stderr, "watch_launch: seccomp filter install failed: %s\n", strerror(errno));
        return 111;
    }

    execv(argv[1], &argv[1]);
    fprintf(stderr, "watch_launch: exec %s failed: %s\n", argv[1], strerror(errno));
    return 127;
}
