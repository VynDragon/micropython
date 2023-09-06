inotifywait  -e create,moved_to,attrib --include '/ttyUSB0$' -qq /dev
sleep 0.1
${BL_SDK_BASE}/tools/bflb_tools/bouffalo_flash_cube/BLFlashCommand-ubuntu --interface=uart --baudrate=115200 --port=/dev/ttyUSB0 --chipname=bl602 --config=flash_prog_cfg.ini
