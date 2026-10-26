/* Delegate initialization and the infinite run loop to the firmware module so main owns no state. */
#include "firmware.h"

int main(void) {
    microflow_firmware_run();
    return 0;
}
