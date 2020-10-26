#!/bin/bash

# sudo openocd -f ../tcl/target/stm32f0x_cmsisdap.cfg
openocd -f interface/cmsis-dap.cfg -c "transport select swd" -f target/nrf52.cfg