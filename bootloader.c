/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "p80c550.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XMODEM_SOH 0x01
#define XMODEM_EOT 0x04
#define XMODEM_ACK 0x06
#define XMODEM_NAK 0x15
#define XMODEM_ETB 0x17
#define XMODEM_CAN 0x18
#define XMODEM_HEY_LISTEN 0x43

#define TX_BUFFER_SIZE 128
#define RX_BUFFER_SIZE 128
#define TX_BUFFER_MASK (TX_BUFFER_SIZE-1)
#define RX_BUFFER_MASK (RX_BUFFER_SIZE-1)

// definition of the control register
union EVN_Control_Register {
    struct {
        uint8_t paged_memory_window : 6;
        uint8_t offboard_memory_enable : 1;
        // Read: !PFO (low power: 0, normal: 1)
        // Write: PFO_MASK (unmask pfo interrupt: 0, mask: 0)
        uint8_t pfo : 1;
    };
    uint8_t value;
};

// EVN and CPLD registers
__xdata __at(0x8000) volatile union EVN_Control_Register control;
__xdata __at(0xA000) uint8_t expanded_memory[8192];

// serial buffers
static char __xdata tx_buf[TX_BUFFER_SIZE];
static char __xdata rx_buf[RX_BUFFER_SIZE];
static unsigned char tx_write_ptr = 0;
static unsigned char tx_read_ptr = 0;
static unsigned char rx_write_ptr = 0;
static unsigned char rx_read_ptr = 0;

static volatile uint16_t centiseconds = 0;

void uart_isr (void) __interrupt(SI0_VECTOR) __using(0) {
    // byte _was_ received
    if (RI) {
        RI = 0;
        rx_buf[rx_write_ptr++ & RX_BUFFER_MASK] = SBUF;
    }

    // byte _was_ sent
    if (TI) {
        // ack byte
        TI = 0;
        tx_read_ptr++;

        // send more data if we have more data
        if (tx_read_ptr != tx_write_ptr) {
            SBUF = tx_buf[tx_read_ptr & TX_BUFFER_MASK];
        }
    }
}

void timer0_isr (void) __interrupt(TF0_VECTOR) __using(0) {
    centiseconds++;

    // overflow 100 times per second
    const uint16_t reload = 56320;
    TH0 = (reload >> 8) & 0x00ff;
    TL0 = reload & 0x00ff;
}

inline unsigned char uart_tx_pending(void) {
    unsigned char pending;
    if (tx_read_ptr <= tx_write_ptr) {
        pending = tx_write_ptr - tx_read_ptr;
    } else {
        pending = (255 - tx_read_ptr) + tx_write_ptr + 1;
    }
    return pending;
}

inline unsigned char uart_rx_pending(void) {
    unsigned char pending;
    if (rx_read_ptr <= rx_write_ptr) {
        pending = rx_write_ptr - rx_read_ptr;
    } else {
        pending = (255 - rx_read_ptr) + rx_write_ptr + 1;
    }
    return pending;
}

void putbyte(uint8_t c) {
    ES = 0;
    if (tx_read_ptr == tx_write_ptr) {
        // send byte now if the tx buffer is empty
        tx_write_ptr++;
        SBUF = c;
    } else {
        // block until buffer has space available
        while (uart_tx_pending() == TX_BUFFER_SIZE);
        tx_buf[tx_write_ptr++ & TX_BUFFER_MASK] = c; 
    }
    ES = 1;
}

uint8_t getbyte() {
    // wait for a byte
    while (rx_read_ptr == rx_write_ptr);
    return rx_buf[rx_read_ptr++ & RX_BUFFER_MASK];    
}

// erase the 64 KiB segment of the flash the program resides in
inline void flash_erase() {
    const uint16_t sector_erase_cycle1_address = 0x5555;
    const uint16_t sector_erase_cycle2_address = 0x2AAA;
    const uint16_t sector_erase_cycle3_address = 0x5555;
    const uint16_t sector_erase_cycle4_address = 0x5555;
    const uint16_t sector_erase_cycle5_address = 0x2AAA;
    const uint8_t sector_erase_cycle1_data = 0xAA;
    const uint8_t sector_erase_cycle2_data = 0x55;
    const uint8_t sector_erase_cycle3_data = 0x80;
    const uint8_t sector_erase_cycle4_data = 0xAA;
    const uint8_t sector_erase_cycle5_data = 0x55;
    const uint8_t sector_erase_cycle6_data = 0x30;

    // erase 16x 4 KiB sectors
    uint16_t sector = 0;
    do {
        control.paged_memory_window = sector_erase_cycle1_address >> 13;
        expanded_memory[sector_erase_cycle1_address & 0x1FFF] = sector_erase_cycle1_data;
        control.paged_memory_window = sector_erase_cycle2_address >> 13;
        expanded_memory[sector_erase_cycle2_address & 0x1FFF] = sector_erase_cycle2_data;
        control.paged_memory_window = sector_erase_cycle3_address >> 13;
        expanded_memory[sector_erase_cycle3_address & 0x1FFF] = sector_erase_cycle3_data;
        control.paged_memory_window = sector_erase_cycle4_address >> 13;
        expanded_memory[sector_erase_cycle4_address & 0x1FFF] = sector_erase_cycle4_data;
        control.paged_memory_window = sector_erase_cycle5_address >> 13;
        expanded_memory[sector_erase_cycle5_address & 0x1FFF] = sector_erase_cycle5_data;
        control.paged_memory_window = sector >> 13;
        expanded_memory[sector & 0x1FFF] = sector_erase_cycle6_data;

        // wait for completion
        uint8_t a = 0;
        uint8_t b;
        while ((b = expanded_memory[sector & 0x1FFF] & 0x40) ^ a) {
            a = b;
        }
    } while ((sector += 0x1000));
}

inline void flash_write(uint16_t address, uint8_t data) {
    // put chip in program mode
    const uint16_t program_cycle1_address = 0x5555;
    const uint16_t program_cycle2_address = 0x2AAA;
    const uint16_t program_cycle3_address = 0x5555;
    const uint8_t program_cycle1_data = 0xAA;
    const uint8_t program_cycle2_data = 0x55;
    const uint8_t program_cycle3_data = 0xA0;

    control.paged_memory_window = program_cycle1_address >> 13;
    expanded_memory[program_cycle1_address & 0x1FFF] = program_cycle1_data;
    control.paged_memory_window = program_cycle2_address >> 13;
    expanded_memory[program_cycle2_address & 0x1FFF] = program_cycle2_data;
    control.paged_memory_window = program_cycle3_address >> 13;
    expanded_memory[program_cycle3_address & 0x1FFF] = program_cycle3_data;

    // write byte
    control.paged_memory_window = address >> 13;
    expanded_memory[address & 0x1FFF] = data;

    // wait for completion
    uint8_t a = 0;
    uint8_t b;
    while ((b = expanded_memory[address & 0x1FFF] & 0x40) ^ a) {
        a = b;
    }
}

inline void action_flash_program_xmodem() {
    __xdata uint8_t packet[128];
    uint16_t address = 0;
    uint16_t length = 0xFFFF;

    // erase the flash
    flash_erase();

    // wait for something
    uint16_t curtime = centiseconds;
    do {
        putbyte(XMODEM_HEY_LISTEN);
        while (!uart_rx_pending() && ((centiseconds - curtime) < 100));
        curtime = centiseconds;
    } while (!uart_rx_pending());

    // receive packet(s)
    while(1) {
        // process header
        uint8_t header = getbyte();
        if (header == XMODEM_ETB) {
            break;
        } else if (header == XMODEM_EOT) {
            break;
        } else if (header != XMODEM_SOH) {
            return;
        }

        // receive packet number
        uint8_t packet_number = getbyte();
        uint8_t n_packet_number = getbyte();

        // receive packet data
        uint16_t crc = 0;
        uint8_t i;
        for (i = 0; i < sizeof packet; i++) {
            packet[i] = getbyte();

            // add to crc computation
            crc = crc ^ (uint16_t) packet[i] << 8;
            uint8_t j = 8;
            do {
                if (crc & 0x8000)
                    crc = crc << 1 ^ 0x1021;
                else
                    crc = crc << 1;
            } while (--j);
        }

        // receive packet crc
        uint16_t remote_crc;
        remote_crc = getbyte();
        remote_crc <<= 8;
        remote_crc |= getbyte();

        // validate packet crc
        if (crc != remote_crc) {
            putbyte(XMODEM_NAK);
            continue;
        }

        // program flash
        for (i = 0; i < sizeof packet; i++) {
            if (length) {
                length--;
                flash_write(address++, packet[i]);
            }
        }
        putbyte(XMODEM_ACK);
    }
    putbyte(XMODEM_ACK);
}

// upload flash over xmodem
void action_flash_dump_xmodem(uint8_t full) {
    // synchronize with receive
    while (getbyte() != XMODEM_HEY_LISTEN);

    // send all 64 KiB of code section
    uint32_t address = 0;
    uint32_t address_end = full ? 0x80000 : 0x10000;
    do {
        // send header
        uint8_t packet_number = (address >> 7) + 1;
        putbyte(XMODEM_SOH);
        putbyte(packet_number);
        putbyte(255 - packet_number);

        // send data
        control.paged_memory_window = address >> 13;
        uint16_t crc = 0;
        do {
            uint8_t data = expanded_memory[address & 0x1FFF];
            putbyte(data);

            // add to crc computation
            crc = crc ^ (uint16_t) data << 8;
            uint8_t i = 8;
            do {
                if (crc & 0x8000)
                    crc = crc << 1 ^ 0x1021;
                else
                    crc = crc << 1;
            } while (--i);
        } while (++address & 0x7F);

        // send checksum
        putbyte(crc >> 8);
        putbyte(crc & 0xff);

        // await ack
        uint8_t response = getbyte();
        if (response != XMODEM_ACK) {
            address -= 128;
        }
    } while (address < address_end);

    // end transmission
    putbyte(XMODEM_EOT);
    getbyte();
    putbyte(XMODEM_ETB);
    getbyte();
}

// exit bootloader and change to the program memory
void boot() {
    // disable interrupts
    EA = 0;

    // disable peripheral interrupts
    ET0 = 0;
    ES = 0;

    // disable peripherals
    TCON = 0;
    TMOD = 0;
    SCON = 0;
    PCON = PCON & ~SMOD;

    // enable offboard memory.
    // after 3 code reads, offboard code mem is enabled
    // --- DO NOT CHANGE ---
    control.offboard_memory_enable = 1;
    __asm ljmp 0 __endasm;
    // ---------------------

    // in case we overshoot, add some single byte instructions
    __asm nop __endasm;
    __asm nop __endasm;
    __asm nop __endasm;
    __asm nop __endasm;
}

void main(void) {
    // first thing we have to do is wait for the CPLD to start
    // keep writing a 1 to a register and see when the bit goes high
    while (!(control.value & 1)) {
        control.value = 0x81;
    }

    // setup timer 1 as baudrate generator
    TMOD = 0x21; // timer 1, mode 2 (8-bit autoreload), timer 0, mode 1 (16-bit timer)
    TH1 = 255;   // 28.8k baud @ 11.0592 MHz clock (baud = fosc / (12 * 32 * (256 - TH1))
    SCON = 0x50; // variable baud rate, single processor, receiver enabled.
    PCON = PCON | SMOD;    // double the baud rate (to 57.6k baud)
    ES = 1;      // enable serial interrupts
    TR1 = 1;     // start timer 1

    // setup timer 0 to overflow every 9216 machine cycles (100 times per second)
    TH0 = 0xdc;
    TL0 = 0x00;
    ET0 = 1;
    TR0 = 1;

    // setup EVN control register (disable pfo interrupt, memory window = 0, onboard code memory)
    control.value = 0x80;

    // enable interrupts
    EA = 1;

    // Wait 1 second for any data on the UART (discard it)
    while (!uart_rx_pending()) {
        if (centiseconds > 99) {
            boot();
        }
    }
    getbyte();

    // Ack on the console
    while (1) {
        // get command token
        switch (getbyte()) {
            case 'P':
                action_flash_program_xmodem();
                break;
            case 'U':
                action_flash_dump_xmodem(0);
                break;
            case 'D':
                action_flash_dump_xmodem(1);
            case 'B':
                boot();
                break;
            default:
                putbyte('N');
                break;
        }
    }
}
