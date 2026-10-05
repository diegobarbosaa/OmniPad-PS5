#ifndef SHELLUI_INJECT_H
#define SHELLUI_INJECT_H

#include <stdint.h>
#include <sys/types.h>

/* Elevated authid & capability helpers */
int elevate_privileges(void);
void restore_privileges(void);

/* Process search */
pid_t find_process_by_name(const char *name);

/* Inject synthesized PS button tap to smoothly trigger the assignment screen.
 * If handle <= 0 or not plausible, searches inside SceShellUI using scePadGetHandle.
 * Returns the confirmed valid handle (>0) on success, or -1 on error. */
int shellui_press_ps_button(int32_t handle);

/* Direct remote bind in SceShellCore/SceShellUI */
int shellui_remote_bind_device(uint64_t device_id, int32_t user_id);

/* Remote unbind / disconnect */
int shellui_remote_disconnect_device(uint64_t device_id);

/* Remote scePadVirtualDeviceAddDevice execution inside SceShellCore */
int shellcore_vda(int *code);

#endif /* SHELLUI_INJECT_H */
