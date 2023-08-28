//#include <stdio.h>
//#include <string.h>
//#include <stdarg.h>


#include "FreeRTOS.h"
#include "task.h"

#include "py/builtin.h"
#include "py/compile.h"
#include "py/gc.h"
#include "py/mperrno.h"
#include "py/stackctrl.h"
#include "py/runtime.h"
#include "py/repl.h"

#include "shared/readline/readline.h"
#include "shared/runtime/pyexec.h"

#if __SIZEOF_POINTER__ == 8
#include "core_rv64.h"
#else
#include "core_rv32.h"
#endif

#include "board.h"


extern void log_start(void);

extern uint32_t __HeapBase;
extern uint32_t __HeapLimit;

// MicroPython runs as a task under FreeRTOS
#define MP_TASK_PRIORITY        (ESP_TASK_PRIO_MIN + 1)
#define MP_TASK_STACK_SIZE      __HeapLimit

// Set the margin for detecting stack overflow, depending on the CPU architecture.
#define MP_TASK_STACK_LIMIT_MARGIN (1024)


// Initial Python heap size. This starts small but adds new heap areas on
// demand due to settings MICROPY_GC_SPLIT_HEAP & MICROPY_GC_SPLIT_HEAP_AUTO
#define MP_TASK_HEAP_SIZE (16 * 1024)


int vprintf_null(const char *format, va_list ap) {
    // do nothing: this is used as a log target during raw repl mode
    return 0;
}

void mp_task(void *pvParameter) {
    #if MICROPY_PY_THREAD
    mp_thread_init(pxTaskGetStackStart(NULL), MP_TASK_STACK_SIZE / sizeof(uintptr_t));
    #endif

    void *mp_task_heap = malloc(MP_TASK_HEAP_SIZE);

soft_reset:
    // initialise the stack pointer for the main thread
    mp_stack_set_top((void *)__HeapBase);
    mp_stack_set_limit(MP_TASK_STACK_SIZE - MP_TASK_STACK_LIMIT_MARGIN);
    gc_init(mp_task_heap, mp_task_heap + MP_TASK_HEAP_SIZE);
    mp_init();
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR__slash_lib));
    readline_init0();

    // run boot-up scripts
    pyexec_file_if_exists("boot.py");
    if (pyexec_mode_kind == PYEXEC_MODE_FRIENDLY_REPL) {
        int ret = pyexec_file_if_exists("main.py");
        if (ret & PYEXEC_FORCED_EXIT) {
            goto soft_reset_exit;
        }
    }

    for (;;) {
        if (pyexec_mode_kind == PYEXEC_MODE_RAW_REPL) {
            if (pyexec_raw_repl() != 0) {
                break;
            }
        } else {
            if (pyexec_friendly_repl() != 0) {
                break;
            }
        }
    }

soft_reset_exit:

    //machine_timer_deinit_all();

    #if MICROPY_PY_THREAD
    mp_thread_deinit();
    #endif

    gc_sweep_all();

    mp_hal_stdout_tx_str("MPY: soft reboot\r\n");

    // deinitialise peripherals
    //machine_pwm_deinit_all();
    // TODO: machine_rmt_deinit_all();
    //machine_pins_deinit();
    //machine_deinit();
    #if MICROPY_PY_SOCKET_EVENTS
    socket_events_deinit();
    #endif

    mp_deinit();
    fflush(stdout);
    goto soft_reset;
}

int main(void) {
    board_init();
#if defined(CONFIG_FREERTOS) && CONFIG_FREERTOS
    xTaskCreate(mp_task, "mp_task", MP_TASK_STACK_SIZE / sizeof(StackType_t), NULL, 1, NULL);
    vTaskStartScheduler();
#else
// TODO
#endif
}

void nlr_jump_fail(void *val) {
    printf("NLR jump failed, val=%p\n", val);
    csi_system_reset();
}
