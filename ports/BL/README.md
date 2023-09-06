
git clone https://github.com/bouffalolab/bouffalo_sdk.git

get xuantie t-chain toolchain

get toolchain for riscv (https://github.com/bouffalolab/toolchain_gcc_t-head_linux)

export BL_SDK_BASE=/bouffalo_sdk
export PATH=toolchain/bin:$PATH
mdkir build && cd build
cmake -D CROSS_COMPILE=toolchain/bin/riscv64-unknown-elf- ..
make
make combine < - might not look like much but will prevent boot if not done
cd ..
${BL_SDK_BASE}/tools/bflb_tools/bouffalo_flash_cube/BLFlashCommand-ubuntu --interface=uart --baudrate=115200 --port=/dev/ttyUSB0 --chipname=bl602 --config=flash_prog_cfg.ini
