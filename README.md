# Self Program Module - Minimal Bootloader

This is the reference minimal (flash) bootloader for the P80C550-SPM expansion board. It allows reading and writing the flash device over the internal UART via xmodem.

## Usage

At present, the minimal bootloader is a very simple program that waits for 1 second for any character to be received by the 8051's internal uart. It currently assumes that the memory device is an SST39SF010/SST39SF020/SST39SF040 parallel flash.

- 'P' is received: erase 64 KiB code page of flash and accepts up to 64 KiB of data via xmodem
- 'U' is received: upload 64 KiB code page of flash via xmodem
- 'D' is received: upload all 512 KiB of flash via xmodem
- 'B' is received: boot to flash device
- any other byte is received: print 'N'

## Example Makefile Rule

```
$(EXEC).bin: $(EXEC)
	objcopy -I ihex $(EXEC) -O binary $(EXEC).bin

boot: $(EXEC).bin
	stty -F /dev/ttyUSB0 57600 cs8 -cstopb -parenb -ixon -crtscts
	echo -n 'PP' >/dev/ttyUSB0
	sx $(EXEC).bin >/dev/ttyUSB0 </dev/ttyUSB0
	echo -n 'B' >/dev/ttyUSB0
```

## Writing to a ROM
```
make bootloader.ihx
minipro -p AT28C256 -f ihex -w bootloader.ihx
```
