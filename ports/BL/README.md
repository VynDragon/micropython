
git clone https://github.com/bouffalolab/bouffalo_sdk.git

get xuantie t-chain toolchain

build toolchain for riscv (rv32imafcb for BL602)

export BL_SDK_BASE=/bouffalo_sdk
export PATH=toolchain/bin:$PATH
mdkir build && cd build
cmake -D CROSS_COMPILE=/opt/riscv/bin/riscv32-unknown-elf- ..
