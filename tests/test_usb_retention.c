#include <assert.h>
#include <stdio.h>
#include "../src/usb_controllers.h"
int main(void){for(int t=CTRL_UNKNOWN;t<=CTRL_GAMESIR_G7SE;t++)
assert(usb_controller_stale_timeout_ms((usb_controller_type_t)t)==(t==CTRL_GAMESIR_G7PRO?20000:500));
puts("Only G7 Pro selects extended USB retention.");}
