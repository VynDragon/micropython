include(boards/common.cmake)
set(CHIP bl602)
set(BOARD bl602dk)
set(CMAKE_C_COMPILER riscv32-unknown-elf-gcc)
set(CMAKE_CXX_COMPILER riscv32-unknown-elf-g++)
set(MICROPY_CROSS_FLAGS -march=riscv32)
