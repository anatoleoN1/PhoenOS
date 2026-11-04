/* PhoenOS - tests : demarrage d'un programme 64 bits sous Linux x86-64 : pile et arguments recopies sous 4 Gio (int 0x80 tronque les pointeurs) */
#include "phoenos.h"
extern int main(int, char **);
#define LOW 0x20000000UL
static long sys6(long n, long a, long b, long c, long d, long e, long f)
{
    long r;
    register long r10 __asm__("r10") = d; register long r8 __asm__("r8") = e; register long r9 __asm__("r9") = f;
    __asm__ volatile ("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return r;
}
static void __attribute__((noreturn, used)) run_main(int argc, char **av) { exit(main(argc, av)); }

void __attribute__((noreturn, used)) start_c(unsigned long *sp)
{
    int argc = (int)sp[0]; char **av = (char **)(sp + 1);
    long m = sys6(9, LOW, 0x200000, 3, 0x32 /*PRIVATE|ANON|FIXED*/, -1, 0);
    char *top = (char *)(LOW + 0x200000), **nav; int i;
    if (m != (long)LOW) exit(99);
    char *p = top - 4096; nav = (char **)(LOW + 0x1F0000);
    for (i = 0; i < argc; i++) { char *s = av[i]; int n = 0; while (s[n]) n++; p -= (n + 1); for (int k = 0; k <= n; k++) p[k] = s[k]; nav[i] = p; }
    nav[argc] = 0;
    unsigned long *nsp = (unsigned long *)(LOW + 0x1E0000); nsp[0] = argc;
    for (i = 0; i <= argc; i++) nsp[1 + i] = (unsigned long)nav[i];
    __asm__ volatile ("movq %0, %%rsp\n xorl %%ebp, %%ebp\n call *%1" : : "r"((unsigned long)nsp - 64), "r"(run_main), "D"(argc), "S"(nav) : "memory");
    for (;;) ;
}
__asm__(".global _start\n_start:\n xorl %ebp,%ebp\n movq %rsp,%rdi\n andq $-16,%rsp\n call start_c\n");
