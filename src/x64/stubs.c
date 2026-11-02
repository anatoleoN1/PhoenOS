/*
 * PhoenOS perf64 - x64/stubs.c
 * Versions "64 bits" des modules qui ne sont pas encore portes en 0.11.0 :
 *   - pagination : le code de demarrage (boot.S) a deja mappe 4 Gio en identite (pages de 2 Mio, >= 1 Gio sans cache) ;
 *     il n'y a donc rien a construire ici (la protection page par page viendra avec le mode utilisateur) ;
 *   - programmes utilisateur (ring 3), appels systeme, bibliotheques .psl : 0.11.1. Le shell reste utilisable ;
 *     lancer un programme donne l'erreur "non implemente".
 */
#include "types.h"
#include "error.h"
#include "user.h"
#include "paging.h"
#include "libmgr.h"

/* ---------- pagination ---------- */
err_t paging_init(void)                    { return E_OK; }
int   paging_enabled(void)                 { return 1; }
u32   paging_table_bytes(void)             { return 3u * 4096u; }
u32   paging_mapped_top(void)              { return PAGING_MAX_TOP; }
err_t paging_map_mmio(u32 phys, u32 size)  { (void)phys; (void)size; return E_OK; }   /* deja mappe (4 Gio) */

/* ---------- programmes utilisateur ---------- */
err_t user_init(void)       { return E_NOSYS; }
int   user_available(void)  { return 0; }
void  user_sched_init(void) {}
void  user_preempt(void)    {}

void user_set_needs(char needs[][POE_LIBNAME_MAX], int n) { (void)needs; (void)n; }
void user_set_stdin(const u8 *buf, u32 len, int set)      { (void)buf; (void)len; (void)set; }

err_t user_spawn_bg(const u8 *image, u32 size, const char *name, int argc, char **argv, u32 *pid)
{ (void)image; (void)size; (void)name; (void)argc; (void)argv; (void)pid; return E_NOSYS; }

err_t user_spawn_child(struct uproc *parent, const char *path, int argc, char **argv, u32 *pid)
{ (void)parent; (void)path; (void)argc; (void)argv; (void)pid; return E_NOSYS; }

i32 user_waitpid(struct uproc *parent, i32 pid, u32 options, int *code)
{ (void)parent; (void)pid; (void)options; (void)code; return E_NOSYS; }

err_t user_wait_pid(u32 pid, int *code, int *known)
{ (void)pid; (void)code; if (known) *known = 0; return E_NOENT; }

err_t user_kill_tree(u32 pid) { (void)pid; return E_NOENT; }
err_t user_kill(u32 pid)      { (void)pid; return E_NOENT; }

err_t user_exec(const u8 *image, u32 size, const char *name, int argc, char **argv, struct user_result *res)
{ (void)image; (void)size; (void)name; (void)argc; (void)argv; (void)res; return E_NOSYS; }

struct uproc *user_current(void) { return 0; }
void syscall_entry(struct regs *r)              { (void)r; }
void user_fault(struct regs *r, const char *w)  { (void)r; (void)w; }

/* ---------- bibliotheques partagees ---------- */
int libmgr_info(int i, const char **name, u32 *refs, u32 *lo, u32 *hi, u32 *shared_pages)
{ (void)i; (void)name; (void)refs; (void)lo; (void)hi; (void)shared_pages; return 0; }

/* perf64 0.11.0 : pas de code de relance (voir update.c) ; ces symboles gardent seulement l'edition de liens possible. */
char relaunch_stub_start[1], relaunch_stub_end[1];
