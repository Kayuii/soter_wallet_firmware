#!/bin/bash

openocd -f interface/cmsis-dap.cfg -c "transport select swd" -f target/nrf52.cfg  \
-c init -c "reset init" -c halt -c "nrf52 mass_erase" \
-c "program s140_nrf52_6.1.0_softdevice.hex verify" -c "program nrf52840_xxaa_s140.hex verify" \
-c reset -c exit