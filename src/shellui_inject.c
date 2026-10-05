#include "shellui_inject.h"
#include "log.h"
#include "pad_types.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <sys/mman.h>
#include <sys/wait.h>

#ifdef __PROSPERO__
#include <machine/reg.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <ps5/kernel.h>
#include <ps5/mdbg.h>
#include <ps5/nid.h>
extern uint64_t sceKernelGetProcessTime(void);
#endif

static uint64_t g_orig_authid = 0;
static uint8_t  g_orig_caps[16] = {0};
static int      g_has_orig = 0;
static int      g_is_elevated = 0;

int elevate_privileges(void)
{
#ifdef __PROSPERO__
    if (g_is_elevated) return 1;

    pid_t me = getpid();
    static const uint8_t root_caps[16] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
    };

    if (!g_has_orig) {
        g_orig_authid = kernel_get_ucred_authid(me);
        if (g_orig_authid && kernel_get_ucred_caps(me, g_orig_caps) == 0) {
            g_has_orig = 1;
        }
    }

    /* System service credentials for MBus and ptrace */
    int set_auth_res = kernel_set_ucred_authid(me, 0x4800000000010003ULL);
    int set_caps_res = kernel_set_ucred_caps(me, root_caps);

    if (set_auth_res == 0 && set_caps_res == 0) {
        g_is_elevated = 1;
        return 1;
    }

    /* Under kstuff 1.13, process may already hold root credentials */
    if (getuid() == 0 || geteuid() == 0) {
        g_is_elevated = 1;
        return 1;
    }

    log_line("inject: kernel_set_ucred failed and process is not root");
    return 0;
#else
    return 1;
#endif
}

void restore_privileges(void)
{
#ifdef __PROSPERO__
    if (g_has_orig && g_is_elevated) {
        pid_t me = getpid();
        kernel_set_ucred_authid(me, g_orig_authid);
        kernel_set_ucred_caps(me, g_orig_caps);
        g_is_elevated = 0;
    }
#endif
}

pid_t find_process_by_name(const char *name)
{
    int mib[4] = { 1, 14, 8, 0 }; /* CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0 */
    size_t size = 0;
    uint8_t *buf = NULL;
    pid_t found_pid = -1;

    if (sysctl(mib, 4, NULL, &size, NULL, 0) != 0 || size == 0) return -1;
    size += 4096; /* Headroom for processes spawned between calls */
    buf = (uint8_t *)malloc(size);
    if (!buf) return -1;

    if (sysctl(mib, 4, buf, &size, NULL, 0) == 0) {
        uint8_t *ptr = buf;
        while (ptr + 8 < buf + size) {
            int stride = *(int *)ptr;
            if (stride < 64 || stride > 4096) break;
            if (ptr + stride > buf + size) break;

            pid_t pid = *(pid_t *)(ptr + 72);
            const char *tdname = (const char *)(ptr + 447);
            struct kinfo_proc *ki = (struct kinfo_proc *)ptr;

            int match = 0;
            if (tdname[0] && (strcmp(tdname, name) == 0 || strstr(tdname, name) != NULL)) match = 1;
            if (!match && ki->ki_comm[0] && (strcmp(ki->ki_comm, name) == 0 || strstr(ki->ki_comm, name) != NULL)) match = 1;

            if (pid > 0 && match) {
                if (found_pid < 0 || pid < found_pid) {
                    found_pid = pid;
                }
            }
            ptr += stride;
        }
    }
    free(buf);
    if (found_pid > 0) {
        log_line("inject: found '%s' at pid %d", name, found_pid);
    }
    return found_pid;
}

#ifdef __PROSPERO__

static int sys_ptrace_wrap(int req, pid_t pid, caddr_t addr, int data)
{
    elevate_privileges();
    int ret = (int)__syscall(SYS_ptrace, req, pid, addr, data);
    return ret;
}

static int pt_io_rw(pid_t pid, int op, intptr_t addr, void *buf, size_t len)
{
    struct ptrace_io_desc desc;
    desc.piod_op = op;
    desc.piod_offs = (void *)addr;
    desc.piod_addr = buf;
    desc.piod_len = len;
    return sys_ptrace_wrap(PT_IO, pid, (caddr_t)&desc, 0);
}

static intptr_t resolve_process_symbol(pid_t pid, const char *lib_name, const char *sym_name)
{
    uint32_t handle = 0;
    char sprx_name[64];
    snprintf(sprx_name, sizeof(sprx_name), "%s.sprx", lib_name);

    if (kernel_dynlib_handle(pid, lib_name, &handle) != 0 || handle == 0) {
        if (kernel_dynlib_handle(pid, sprx_name, &handle) != 0 || handle == 0)
            return 0;
    }

    intptr_t addr = kernel_dynlib_dlsym(pid, handle, sym_name);
    if (addr) return addr;

    char nid[12];
    nid_encode(sym_name, nid);
    return kernel_dynlib_resolve(pid, handle, nid);
}

/* Safe code cave in existing executable text segment (bypasses W^X stack protection) */
static intptr_t find_code_cave(pid_t pid, const char *lib_name)
{
    uint32_t handle = 0;
    char sprx_name[64];
    snprintf(sprx_name, sizeof(sprx_name), "%s.sprx", lib_name);

    if (kernel_dynlib_handle(pid, lib_name, &handle) != 0 || handle == 0) {
        if (kernel_dynlib_handle(pid, sprx_name, &handle) != 0 || handle == 0)
            return 0;
    }

    intptr_t addr = kernel_dynlib_init_addr(pid, handle);
    if (!addr) addr = kernel_dynlib_fini_addr(pid, handle);
    return addr;
}

static int64_t pt_call_remote(pid_t pid, intptr_t fn, intptr_t trap_rip,
                              uint64_t a1, uint64_t a2, uint64_t a3)
{
    struct reg regs, saved;
    int status;

    if (sys_ptrace_wrap(PT_GETREGS, pid, (caddr_t)&regs, 0) != 0) return -1;
    memcpy(&saved, &regs, sizeof(regs));

    intptr_t new_rsp = (regs.r_rsp - 256) & ~(intptr_t)0xF;
    if (pt_io_rw(pid, PIOD_WRITE_D, new_rsp, &trap_rip, sizeof(trap_rip)) != 0) return -1;

    regs.r_rsp = new_rsp;
    regs.r_rip = fn;
    regs.r_rdi = a1;
    regs.r_rsi = a2;
    regs.r_rdx = a3;
    regs.r_rcx = 0;
    regs.r_r8 = 0;
    regs.r_r9 = 0;

    if (sys_ptrace_wrap(PT_SETREGS, pid, (caddr_t)&regs, 0) != 0) return -1;
    if (sys_ptrace_wrap(PT_CONTINUE, pid, (caddr_t)1, 0) != 0) return -1;

    int got_trap = 0;
    for (int ms = 0; ms < 3000; ms++) {
        int r = waitpid(pid, &status, WNOHANG);
        if (r > 0) {
            if (WIFSTOPPED(status)) {
                if (WSTOPSIG(status) == SIGTRAP) {
                    got_trap = 1;
                    break;
                }
                int sig = WSTOPSIG(status);
                sys_ptrace_wrap(PT_CONTINUE, pid, (caddr_t)1, (sig == SIGCHLD) ? 0 : sig);
            } else {
                sys_ptrace_wrap(PT_SETREGS, pid, (caddr_t)&saved, 0);
                return -1;
            }
        }
        usleep(1000);
    }

    if (!got_trap) {
        sys_ptrace_wrap(PT_SETREGS, pid, (caddr_t)&saved, 0);
        return -1;
    }

    struct reg finished;
    sys_ptrace_wrap(PT_GETREGS, pid, (caddr_t)&finished, 0);
    int64_t result = (int64_t)finished.r_rax;
    sys_ptrace_wrap(PT_SETREGS, pid, (caddr_t)&saved, 0);
    return result;
}

#endif /* __PROSPERO__ */

int shellui_press_ps_button(int32_t handle)
{
#ifdef __PROSPERO__
    pid_t pid = find_process_by_name("SceShellUI");
    if (pid <= 0) pid = find_process_by_name("SceShellCore");
    if (pid <= 0) {
        log_line("inject: SceShellUI / SceShellCore not found");
        return -1;
    }

    if (sys_ptrace_wrap(PT_ATTACH, pid, 0, 0) != 0) {
        log_line("inject: failed to attach to pid %d (%s)", pid, strerror(errno));
        return -1;
    }
    waitpid(pid, NULL, 0);

    intptr_t fn_get = resolve_process_symbol(pid, "libScePad", "scePadGetHandle");
    intptr_t fn_vdi = resolve_process_symbol(pid, "libScePad", "scePadVirtualDeviceInsertData");
    intptr_t trap = find_code_cave(pid, "libScePad");
    if (!fn_vdi || !trap) {
        log_line("inject: symbols or code cave not resolved in libScePad");
        sys_ptrace_wrap(PT_DETACH, pid, (caddr_t)1, 0);
        return -1;
    }

    struct reg regs;
    if (sys_ptrace_wrap(PT_GETREGS, pid, (caddr_t)&regs, 0) != 0) {
        sys_ptrace_wrap(PT_DETACH, pid, (caddr_t)1, 0);
        return -1;
    }

    /* Save original byte from code cave and place INT3 breakpoint */
    uint8_t orig_byte = 0x90;
    uint8_t int3 = 0xCC;
    kernel_set_vmem_protection(pid, trap, 16, PROT_READ | PROT_WRITE | PROT_EXEC);
    pt_io_rw(pid, PIOD_READ_D, trap, &orig_byte, 1);
    pt_io_rw(pid, PIOD_WRITE_D, trap, &int3, 1);

    /* If handle not known, query scePadGetHandle inside SceShellUI */
    if (handle <= 0 && fn_get) {
        int users[3] = { -1, 1, 0x10000000 };
        for (int type = 0; type < 2 && handle <= 0; type++) {
            int port = (type == 0) ? 3 : 0; /* 3 = Virtual DualSense, 0 = Standard */
            int first = (port == 0) ? 1 : 0;
            for (int u = 0; u < 3 && handle <= 0; u++) {
                for (int idx = first; idx < 4 && handle <= 0; idx++) {
                    int64_t got = pt_call_remote(pid, fn_get, trap,
                                                 (uint64_t)(uint32_t)users[u],
                                                 (uint64_t)port, (uint64_t)idx);
                    if (got > 0) {
                        handle = (int32_t)got;
                        log_line("inject: resolved handle %d via SceShellUI scePadGetHandle", handle);
                    }
                }
            }
        }
    }

    if (handle <= 0) {
        pt_io_rw(pid, PIOD_WRITE_D, trap, &orig_byte, 1);
        sys_ptrace_wrap(PT_DETACH, pid, (caddr_t)1, 0);
        log_line("inject: unable to resolve valid handle for PS button injection");
        return -1;
    }

    /* Construct synthesized PS button PadData frame (exact 120-byte ABI) */
    PadData pad;
    pad_state_t st;
    pad_state_neutral(&st);
    st.buttons = PAD_BTN_PS;
    uint64_t pad_time = 0;
#ifdef __PROSPERO__
    pad_time = sceKernelGetProcessTime();
    if (pad_time > 1000) pad_time -= 1000;
#else
    pad_time = (uint64_t)now_ms() * 1000ULL;
#endif
    pad_data_from_state(&pad, &st, pad_time);

    intptr_t data_addr = (regs.r_rsp - 4096) & ~(intptr_t)0xF;
    pt_io_rw(pid, PIOD_WRITE_D, data_addr, &pad, sizeof(pad));

    /* Send PS button DOWN for 12 frames to ensure SceShellUI user picker acknowledges it */
    for (int f = 0; f < 12; f++) {
        pt_call_remote(pid, fn_vdi, trap, (uint64_t)handle, (uint64_t)data_addr, 0);
    }

    /* Release PS button */
    st.buttons = 0;
#ifdef __PROSPERO__
    pad_time = sceKernelGetProcessTime();
    if (pad_time > 1000) pad_time -= 1000;
#else
    pad_time = (uint64_t)now_ms() * 1000ULL;
#endif
    pad_data_from_state(&pad, &st, pad_time);
    pt_io_rw(pid, PIOD_WRITE_D, data_addr, &pad, sizeof(pad));
    pt_call_remote(pid, fn_vdi, trap, (uint64_t)handle, (uint64_t)data_addr, 0);

    /* Restore original instruction in code cave */
    pt_io_rw(pid, PIOD_WRITE_D, trap, &orig_byte, 1);
    sys_ptrace_wrap(PT_DETACH, pid, (caddr_t)1, 0);

    log_line("inject: synthesized PS button sent to shell UI (handle %d)", handle);
    return handle;
#else
    (void)handle;
    return handle;
#endif
}

int shellcore_vda(int *code)
{
#ifdef __PROSPERO__
    pid_t pid = find_process_by_name("SceShellCore");
    if (pid <= 0) pid = find_process_by_name("SceShellUI");
    if (pid <= 0) {
        *code = -1;
        return -1;
    }

    if (sys_ptrace_wrap(PT_ATTACH, pid, 0, 0) != 0) {
        *code = -errno;
        return -1;
    }
    waitpid(pid, NULL, 0);

    intptr_t fn = resolve_process_symbol(pid, "libScePad", "scePadVirtualDeviceAddDevice");
    intptr_t trap = find_code_cave(pid, "libScePad");
    struct reg regs;
    if (!fn || !trap || sys_ptrace_wrap(PT_GETREGS, pid, (caddr_t)&regs, 0) != 0) {
        sys_ptrace_wrap(PT_DETACH, pid, (caddr_t)1, 0);
        *code = -2;
        return -1;
    }

    struct {
        int32_t size;
        int32_t user_id;
        int32_t pad[6];
    } param, out;
    const int32_t sentinel = 0x7EADBEEF;

    memset(&param, 0, sizeof(param));
    param.size = (int32_t)sizeof(param);
    param.user_id = 1;
    for (int i = 0; i < 6; i++) param.pad[i] = sentinel;

    intptr_t param_addr = (regs.r_rsp - 4096) & ~(intptr_t)0xF;
    uint8_t orig_byte = 0x90;
    uint8_t int3 = 0xCC;
    kernel_set_vmem_protection(pid, trap, 16, PROT_READ | PROT_WRITE | PROT_EXEC);
    pt_io_rw(pid, PIOD_READ_D, trap, &orig_byte, 1);
    pt_io_rw(pid, PIOD_WRITE_D, trap, &int3, 1);
    pt_io_rw(pid, PIOD_WRITE_D, param_addr, &param, sizeof(param));

    int64_t result = pt_call_remote(pid, fn, trap, (uint64_t)param_addr, 3, 0);

    memset(&out, 0, sizeof(out));
    pt_io_rw(pid, PIOD_READ_D, param_addr, &out, sizeof(out));
    pt_io_rw(pid, PIOD_WRITE_D, trap, &orig_byte, 1);
    sys_ptrace_wrap(PT_DETACH, pid, (caddr_t)1, 0);

    *code = (int)result;
    if ((int)result > 0 && (int)result < 64) return (int)result;
    for (int i = 0; i < 6; i++) {
        if (out.pad[i] != sentinel && out.pad[i] > 0 && out.pad[i] < 64)
            return out.pad[i];
    }
    return -1;
#else
    *code = 0;
    return -1;
#endif
}

static int call_remote_mbus(const char *proc_name, const char *sym, uint64_t a1, uint64_t a2, int64_t *out_res)
{
    pid_t pid = find_process_by_name(proc_name);
    if (pid <= 0) return -1;

    if (sys_ptrace_wrap(PT_ATTACH, pid, 0, 0) != 0) return -1;
    waitpid(pid, NULL, 0);

    intptr_t fn = resolve_process_symbol(pid, "libSceMbus", sym);
    intptr_t trap = find_code_cave(pid, "libSceMbus");
    if (!trap) trap = find_code_cave(pid, "libScePad");
    if (!trap) trap = find_code_cave(pid, "libkernel");
    if (!trap) trap = find_code_cave(pid, "libkernel_sys");

    struct reg regs;
    if (!fn || !trap || sys_ptrace_wrap(PT_GETREGS, pid, (caddr_t)&regs, 0) != 0) {
        sys_ptrace_wrap(PT_DETACH, pid, (caddr_t)1, 0);
        return -1;
    }

    uint8_t orig_byte = 0x90;
    uint8_t int3 = 0xCC;
    kernel_set_vmem_protection(pid, trap, 16, PROT_READ | PROT_WRITE | PROT_EXEC);
    pt_io_rw(pid, PIOD_READ_D, trap, &orig_byte, 1);
    pt_io_rw(pid, PIOD_WRITE_D, trap, &int3, 1);

    *out_res = pt_call_remote(pid, fn, trap, a1, a2, 0);

    pt_io_rw(pid, PIOD_WRITE_D, trap, &orig_byte, 1);
    sys_ptrace_wrap(PT_DETACH, pid, (caddr_t)1, 0);
    return 0;
}

int shellui_remote_bind_device(uint64_t device_id, int32_t user_id)
{
#ifdef __PROSPERO__
    int64_t res = -1;
    /* Try SceShellUI first, fallback to SceShellCore */
    if (call_remote_mbus("SceShellUI", "sceMbusBindDeviceWithUserId", device_id, (uint64_t)(uint32_t)user_id, &res) != 0 || res != 0) {
        call_remote_mbus("SceShellCore", "sceMbusBindDeviceWithUserId", device_id, (uint64_t)(uint32_t)user_id, &res);
    }
    log_line("inject: remote sceMbusBindDeviceWithUserId(0x%llx, 0x%x) -> %lld",
             (unsigned long long)device_id, (unsigned)user_id, (long long)res);
    return (int)res;
#else
    (void)device_id;
    (void)user_id;
    return 0;
#endif
}

int shellui_remote_disconnect_device(uint64_t device_id)
{
#ifdef __PROSPERO__
    int64_t res = -1;
    /* Try SceShellUI first, fallback to SceShellCore */
    if (call_remote_mbus("SceShellUI", "sceMbusDisconnectDevice", device_id, 0, &res) != 0 || res != 0) {
        call_remote_mbus("SceShellCore", "sceMbusDisconnectDevice", device_id, 0, &res);
    }
    return (int)res;
#else
    (void)device_id;
    return 0;
#endif
}
