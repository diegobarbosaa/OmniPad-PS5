/* Exercise the real slot lifecycle. Console calls are stubs, not hardware validation. */
#include <assert.h>
#include <stdio.h>
#define now_ms controlled_now
#include "../src/ps5_vpad.c"
#undef now_ms
static long clock_ms=1000;
long controlled_now(void){return clock_ms;}
int elevate_privileges(void){return 0;}
void restore_privileges(void){}
int shellcore_vda(int *c){*c=0;return -1;}
int shellui_press_ps_button(int32_t h){return h;}
int shellui_remote_bind_device(uint64_t d,int32_t u){(void)d;(void)u;return 0;}
int shellui_remote_unbind_device(uint64_t d,int32_t u){(void)d;(void)u;return 0;}
int shellui_remote_disconnect_device(uint64_t d){(void)d;return 0;}
static void ready(int i){g_slots[i].status=VP_READY;g_slots[i].handle=i+1;}
int main(void){
    g_ready=1;
    /* Names never select virtual policy. Existing callers always get 500. */
    assert(vpad_add(0,CONN_USB_WIRED,"GameSir G7 Pro (USB HID)"));
    assert(g_slots[0].stale_timeout_ms==500);
    assert(vpad_add_with_timeout(1,CONN_USB_WIRED,"arbitrary",20000));
    assert(g_slots[1].stale_timeout_ms==20000);
    ready(0);ready(1);
    pad_state_t st;pad_state_neutral(&st);st.buttons=PAD_BTN_CROSS;
    vpad_update(0,&st);vpad_update(1,&st);
    clock_ms=1500;vpad_poll(clock_ms);
    assert(g_slots[0].pad_state.buttons && g_slots[1].pad_state.buttons);
    clock_ms=1501;vpad_poll(clock_ms);
    /* Integration: default-bound USB pads are event-driven (#18) and are NOT cleared by silence;
     * only CONN_NETWORK_STREAM and explicit driver bounds are watched (see HANDOFF decision #19). */
    assert(g_slots[0].pad_state.buttons && g_slots[1].pad_state.buttons);
    assert(g_slots[1].last_update_time==1000); /* Cached polls do not renew. */
    clock_ms=21000;vpad_poll(clock_ms);assert(g_slots[1].pad_state.buttons);
    clock_ms=21001;vpad_poll(clock_ms);assert(!g_slots[1].pad_state.buttons);
    /* Remove/reuse and failed-slot replacement must not inherit 20 seconds. */
    vpad_remove(1);assert(!g_slots[1].stale_timeout_ms && g_slots[1].status==VP_FREE);
    assert(vpad_add(1,CONN_USB_WIRED,"DS4"));assert(g_slots[1].stale_timeout_ms==500);
    vpad_remove(1);
    assert(vpad_add_with_timeout(1,CONN_USB_WIRED,"G7",20000));
    g_slots[1].status=VP_FAILED;
    assert(vpad_add(1,CONN_NETWORK_STREAM,"LAN"));assert(g_slots[1].stale_timeout_ms==500);
    vpad_remove(1);
    assert(vpad_add_with_timeout(1,CONN_USB_WIRED,"G7",20000));ready(1);
    vpad_update(1,&st);vpad_recycle_slot_locked(1);
    assert(g_slots[1].stale_timeout_ms==20000); /* Same physical device. */
    assert(!g_slots[1].last_update_time && !g_slots[1].pad_state.buttons);
    assert(g_slots[1].status==VP_QUEUED);
    vpad_remove(1);
    assert(vpad_add_with_timeout(1,CONN_USB_WIRED,"invalid",0));
    assert(g_slots[1].stale_timeout_ms==500);
    vpad_remove(0);vpad_remove(1);
    assert(!g_slots[0].stale_timeout_ms && !g_slots[1].stale_timeout_ms);
    /* Deferred removal cannot accept a replacement before the old slot ends. */
    assert(vpad_add_with_timeout(2,CONN_USB_WIRED,"old",20000));
    g_slots[2].status=VP_PENDING;vpad_remove(2);
    assert(g_slots[2].remove_when_found && !vpad_add(2,CONN_USB_WIRED,"new"));
    g_slots[2].status=VP_READY;vpad_remove(2);
    assert(vpad_add(2,CONN_USB_WIRED,"new") && g_slots[2].stale_timeout_ms==500);
    vpad_remove(2);
    puts("Per-slot default/extended boundaries, isolation, reuse, disconnect and recycle passed.");
}
