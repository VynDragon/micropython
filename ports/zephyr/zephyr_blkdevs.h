/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2024 MASSDRIVER EI
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <zephyr/storage/flash_map.h>
#include <zephyr/devicetree.h>

#include "py/runtime.h"

#if MICROPY_VFS

#define ZEPHYR_DISK_ENTRY_NODEID_DISKNAME(n) DT_STRING_TOKEN(n, disk_name)
#define ZEPHYR_DISK_ENTRY(n) { MP_ROM_QSTR(CONCAT(MP_QSTR_,ZEPHYR_DISK_ENTRY_NODEID_DISKNAME(n))), MP_ROM_QSTR(CONCAT(MP_QSTR_,ZEPHYR_DISK_ENTRY_NODEID_DISKNAME(n))) },

#define ZEPHYR_FLASHPARTITION_P_ENTRY_NODEID_LABEL(n) DT_STRING_TOKEN(n, label)
#define ZEPHYR_FLASHPARTITION_P_ENTRY(n) { MP_ROM_QSTR(CONCAT(MP_QSTR_,ZEPHYR_FLASHPARTITION_P_ENTRY_NODEID_LABEL(n))), MP_OBJ_NEW_SMALL_INT(DT_FIXED_PARTITION_ID(n)) },
#define ZEPHYR_FLASHPARTITION_ENTRY(n) DT_FOREACH_CHILD(n, ZEPHYR_FLASHPARTITION_P_ENTRY)
static const mp_rom_map_elem_t zephyr_diskdevs_globals_table[] = {
    DT_FOREACH_STATUS_OKAY(zephyr_flash_disk, ZEPHYR_DISK_ENTRY)
    DT_FOREACH_STATUS_OKAY(zephyr_ram_disk, ZEPHYR_DISK_ENTRY)
};
static MP_DEFINE_CONST_DICT(zephyr_diskdevs_globals, zephyr_diskdevs_globals_table);

static const mp_rom_map_elem_t zephyr_flashdevs_globals_table[] = {
    DT_FOREACH_STATUS_OKAY(fixed_partitions, ZEPHYR_FLASHPARTITION_ENTRY)
};
static MP_DEFINE_CONST_DICT(zephyr_flashdevs_globals, zephyr_flashdevs_globals_table);

static const mp_rom_map_elem_t zephyr_blkdevs_globals_table[] = {
#ifdef CONFIG_DISK_ACCESS
    { MP_ROM_QSTR(MP_QSTR_DiskIDs), MP_ROM_PTR(&zephyr_diskdevs_globals) },
#endif
#ifdef CONFIG_FLASH_MAP
    { MP_ROM_QSTR(MP_QSTR_FlashIDs), MP_ROM_PTR(&zephyr_flashdevs_globals) },
#endif
#if defined(CONFIG_FLASH_MAP) && FIXED_PARTITION_EXISTS(storage_partition)
    { MP_ROM_QSTR(MP_QSTR_Flash), MP_OBJ_NEW_SMALL_INT(FIXED_PARTITION_ID(storage_partition)) },
#elif defined(CONFIG_FLASH_MAP) && FIXED_PARTITION_EXISTS(storage)
    { MP_ROM_QSTR(MP_QSTR_Flash), MP_OBJ_NEW_SMALL_INT(FIXED_PARTITION_ID(storage)) },
#endif
#ifdef CONFIG_DISK_DRIVER_SDMMC
    { MP_ROM_QSTR(MP_QSTR_SD), MP_ROM_QSTR(CONFIG_SDMMC_VOLUME_NAME) },
#elif CONFIG_DISK_DRIVER_MMC
    { MP_ROM_QSTR(MP_QSTR_SD), MP_ROM_QSTR(CONFIG_MMC_VOLUME_NAME) },
#endif
};
static MP_DEFINE_CONST_DICT(zephyr_blkdevs_globals, zephyr_blkdevs_globals_table);

#endif // MICROPY_VFS
