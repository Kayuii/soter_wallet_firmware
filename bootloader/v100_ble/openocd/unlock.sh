#!/bin/bash

#xxx.hex or xxx.bin

#stm32f103c8t6
FLASH_ADDR=0x0 
FLASH_SIZE=0x100000 #128KB

CURRENT_DIR=$(cd $(dirname $0); pwd)

# -f interface/cmsis-dap.cfg -c "transport select swd" -f target/nrf52.cfg

CONFIG=target/nrf52.cfg
INTERFACE=interface/cmsis-dap.cfg

openocd -f ${INTERFACE} -f ${CONFIG} -c \
"   init;
    halt;
    reset halt;
    help ;
    exit;
"