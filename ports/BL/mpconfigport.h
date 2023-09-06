#include <stdint.h>

// Python internal features.
#define MICROPY_ENABLE_GC                       (1)
#define MICROPY_HELPER_REPL                     (1)
#define MICROPY_ERROR_REPORTING                 (MICROPY_ERROR_REPORTING_TERSE)
#define MICROPY_FLOAT_IMPL                      (MICROPY_FLOAT_IMPL_FLOAT)
#define MICROPY_ENABLE_EXTERNAL_IMPORT          (1)
#define MICROPY_ENABLE_FINALISER                (1)
#define MICROPY_VFS                             (1)
#define MICROPY_VFS_FAT                         (0)
#define MICROPY_VFS_LFS2                        (1)
#define MICROPY_READER_VFS                      (1)

// Fine control over Python builtins, classes, modules, etc.
#define MICROPY_USE_INTERNAL_PRINTF             (0)

// Type definitions for the specific machine.

typedef intptr_t mp_int_t; // must be pointer size
typedef uintptr_t mp_uint_t; // must be pointer size
typedef long mp_off_t;

// We need to provide a declaration/definition of alloca().
#include <alloca.h>

// Define the port's name and hardware.
#ifndef MICROPY_HW_BOARD_NAME
#define MICROPY_HW_BOARD_NAME "Bouffalo"
#endif
#ifndef MICROPY_HW_MCU_NAME
#define MICROPY_HW_MCU_NAME   "Bouffalo"
#endif

#define MP_STATE_PORT MP_STATE_VM
