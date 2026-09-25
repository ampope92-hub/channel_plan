/* Linked into the verifier's own build of the collected sources, first on the command line, so it
   runs out of .preinit_array before any constructor the planner or its libraries define. From
   there on the process may not start another program (execve, execveat), may not make an unnamed
   file or shared segment to carry code in, and may not turn any memory executable, new or old,
   file-backed or anonymous. Shared libraries the build links against are mapped by the loader
   before this runs and are unaffected, so nothing an ordinary compiled planner needs is taken
   away; what is taken away is running code that did not come out of the compiler.
   Every call is the syscall instruction itself rather than a libc function, because a source file
   in the same executable could define its own prctl and the linker would prefer it. The filter is
   read back from the kernel and the program stops if it is not in force. */
#include <errno.h>
#include <stddef.h>
#include <sys/syscall.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/prctl.h>
#include <linux/seccomp.h>

#ifndef SECCOMP_RET_KILL_PROCESS
#define SECCOMP_RET_KILL_PROCESS 0x80000000U
#endif

namespace {

/* The kernel entry, x86-64: the call number in rax, the arguments in rdi, rsi, rdx, r10, r8, and
   the result back in rax. Written here rather than called through the C library so that nothing
   in the program being built can stand in front of it. */
inline long kernel_call(long nr, long a1, long a2, long a3, long a4, long a5) {
    long ret;
    __asm__ volatile("movq %[arg4], %%r10\n\t"
                     "movq %[arg5], %%r8\n\t"
                     "syscall"
                     : "=a"(ret)
                     : "a"(nr), "D"(a1), "S"(a2), "d"(a3), [arg4] "r"(a4), [arg5] "r"(a5)
                     : "rcx", "r11", "r10", "r8", "memory");
    return ret;
}

void refuse(const char* msg, unsigned long len) {
    kernel_call(__NR_write, 2, (long)msg, (long)len, 0, 0);
    kernel_call(__NR_exit_group, 111, 0, 0, 0, 0);
    for (;;) {
    }
}

/* The memory bit written out here for the same reason the kernel calls are: PROT_EXEC. Neither
   the write bit nor the file-backing flag is read any more: every new executable mapping is
   refused outright, so what else the request carries makes no difference. */
enum { MEM_EXEC = 4 };

/* The filter is one program, so each test has to name the instruction it jumps to. The names are
   the program's own line numbers and the jumps are written as the distance between two of them,
   so a line added anywhere keeps every jump pointing where it reads. */
enum {
    L_ARCH = 0, L_ARCH_OK, L_ARCH_KILL, L_NR,
    L_EXECVE, L_EXECVEAT, L_MEMFD, L_SHMGET, L_SHMAT, L_UFFD,
    L_MPROTECT, L_PKEY, L_MMAP, L_ALLOW_REST,
    L_P_HI, L_P_HI_OK, L_P_LO, L_P_EXEC,
    L_M_HI, L_M_HI_OK, L_M_LO, L_M_EXEC,
    L_ALLOW, L_DENY
};
#define J(from, to) ((unsigned char)((to) - (from) - 1))

void close_exec(int, char**, char**) {
    /* read-only data, so the program is not copied onto the stack by a memcpy the executable
       could also define for itself */
    static const struct sock_filter filter[] = {
        /* the architecture we built for, and nothing else */
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_X86_64, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
        /* starting another program, by name or through an unnamed file, and the shared-memory
           neighbours that carry code the same way */
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_execve,        J(L_EXECVE, L_DENY), 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_execveat,      J(L_EXECVEAT, L_DENY), 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_memfd_create,  J(L_MEMFD, L_DENY), 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_shmget,        J(L_SHMGET, L_DENY), 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_shmat,         J(L_SHMAT, L_DENY), 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_userfaultfd,   J(L_UFFD, L_DENY), 0),
        /* and the two calls that decide what memory may be run, which are read further down */
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_mprotect,      J(L_MPROTECT, L_P_HI), 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_pkey_mprotect, J(L_PKEY, L_P_HI), 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_mmap,          J(L_MMAP, L_M_HI), 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        /* memory that already exists may not come back executable, whatever it holds now. The
           high half of the argument is read first: a request carrying anything up there is not
           one of the ordinary ones, and it is refused rather than read from its low half alone. */
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[2]) + 4),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0, J(L_P_HI_OK, L_P_LO), J(L_P_HI_OK, L_DENY)),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[2])),
        BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, MEM_EXEC, J(L_P_EXEC, L_DENY), J(L_P_EXEC, L_ALLOW)),
        /* new memory: no mmap may come back executable, whatever backs it. A shared library is
           already mapped by the time this filter goes on (the loader does that before any
           constructor runs, this one included), so refusing every new executable mapping here
           costs nothing a planner needs; it only closes the file a program could write its own
           code into and map back. */
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[2]) + 4),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0, J(L_M_HI_OK, L_M_LO), J(L_M_HI_OK, L_DENY)),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[2])),
        BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, MEM_EXEC, J(L_M_EXEC, L_DENY), J(L_M_EXEC, L_ALLOW)),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)),
    };
    static_assert(sizeof filter / sizeof filter[0] == L_DENY + 1,
                  "every line of the filter is named once and in order");
    struct sock_fprog prog;
    prog.len = (unsigned short)(sizeof filter / sizeof filter[0]);
    prog.filter = (struct sock_filter*)filter;

    /* no-new-privs is already set by the launcher; setting it again costs nothing and makes the
       filter installable on its own. Ordinary file work is untouched by either call. */
    if (kernel_call(__NR_prctl, PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0 ||
        kernel_call(__NR_prctl, PR_SET_SECCOMP, SECCOMP_MODE_FILTER, (long)&prog, 0, 0) != 0) {
        static const char msg[] = "the run policy would not install; the program is not starting\n";
        refuse(msg, sizeof msg - 1);
    }
    /* and the kernel is asked what it is holding, so a run only ever starts with the filter on */
    if (kernel_call(__NR_prctl, PR_GET_SECCOMP, 0, 0, 0, 0) != SECCOMP_MODE_FILTER) {
        static const char msg[] = "the run policy is not in force; the program is not starting\n";
        refuse(msg, sizeof msg - 1);
    }
}

__attribute__((section(".preinit_array"), used))
void (*const first_thing)(int, char**, char**) = close_exec;

}  // namespace
