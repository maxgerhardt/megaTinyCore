/* UART.h - Hardware serial library, main header.
 * This library is free software released under LGPL 2.1.
 * See License.md for more information.
 * This file is part of megaTinyCore.
 *
 * Copyright (c) 2006 Nicholas Zambetti, Modified by
 * 11/23/2006 David A. Mellis, 9/20/2010 Mark Sproul,
 * 8/24/2012 Alarus, 12/3/2013 Matthijs Kooijman
 * Others (unknown) 2013-2017, 2017-2021 Spence Konde
 * and 2021 MX682X
 *
 * Modified 28 September 2010 by Mark Sproul
 * Modified 14 August 2012 by Alarus
 * Modified 3 December 2013 by Matthijs Kooijman
 * Modified by SOMEONE around 2016-2017; hardware seriel didn't port itself to the megaAVR 0-series.
 * Modified 2017-2021 by Spence Konde for megaTinyCore and DxCore.
 * Modified late 2021 by Spence Konde and MX682X for DxCore
 * 12/26/21: Correct bug introduced in my ASM macros. -Spence
 * 12/30/21: Clean up tests for conditional compilation
             tests for defined(USE_ASM_*) removed UART.h tests that test both defined and the value of
             USE_ASM_* macros. We check if they're defined and define them if they're not defined already
             so whenever UART.h has been loaded, those three macros are defined as either 1, or wharever
             value the user overode them with, likely 0. Also high byte of UART address always 0x08, so replace
             2-clock ldd with 1 clock ldi. - Spence
*/

#pragma once

#include <inttypes.h>
#include "api/HardwareSerial.h"
#include "pins_arduino.h"
#include "UART_constants.h"
#include "UART_check_pins.h"

// No UART_swap.h on megaTinyCore - there isn't enough to put in separate file.

/* Define constants and variables for buffering incoming serial data.  We're
 * using a ring buffer, in which head is the index of the location to which
 * to write the next incoming character and tail is the index of the
 * location from which to read.
 * NOTE: a "power of 2" buffer size is required. The compiler misses chances
 * to optimize power-of-2 buffers sizes, so disallowing them saves flash.
 * I see no compelling reason to permit non-power-of-2 ring buffer length.
 *      -Spence
 *
 * WARNING: When buffer sizes are increased to > 256, the buffer index
 * variables are automatically increased in size, but the extra
 * atomicity guards needed for that are not implemented. This will
 * often work, but occasionally a race condition can occur that makes
 * Serial behave erratically. See https://github.com/arduino/Arduino/issues/2405
 * This is definitely fixed for TX. I am not confident that it is for RX.
 *
 * Flash versus RAM table
 * |       |  modern tinyAVR series parts   | Other modern parts   |
 * | Flash | 0-series | 1-series | 2-series | mega | All Dx |  EA  |
 * |-------|----------|----------|----------|------|--------|------|
 * |  2048 |      128 |      128 |       -  |   -  |     -  |   -  |
 * |  4096 |      256 |      256 |      512 |   -  |     -  |   -  |
 * |  8192 |      512 |      512 |     1024 | 1024 |     -  | 1024 |
 * | 16384 |     1024 |     2048 |     2048 | 2048 |   2048 | 2048 |
 * | 32768 |       -  |     2048 |     3072 | 4096 |   4096 | 4096 |
 * | 49152 |       -  |       -  |       -  | 6120 |     -  |   -  |
 * | 65536 |       -  |       -  |       -  |   -  |   8192 | 6120 |
 * |  128k |       -  |       -  |       -  |   -  |  16384 |   -  |
 * This ratio is remarkably consistent. No AVR part was ever made with
 * less than 8:1 flash:ram, nor more than 16:1, since first ATmegas!
 * The sole exception? The ATmega2560/2561 has only 4k RAM.
 * (to be fair, you are allowed to use external RAM - also I think
 * a unique feature)
 */
#if !defined(USE_ASM_TXC)
  #define USE_ASM_TXC 1    // This *appears* to work? It's the easy one. saves 6b for 1 USART, 50 for 2.
#endif

#if !defined(USE_ASM_RXC)
  #define USE_ASM_RXC 1    // This now works. Saves only 4b for 1 usart but 102 for 2.
#endif

#if !defined(USE_ASM_DRE)
  #define USE_ASM_DRE 1      // This is the hard one...Depends on BOTH buffers, and has that other method of calling it. saves 34b for 1 USART 102 for 2
#endif
// savings:
// 44 total for 0/1,
// 301 for 2-series, which may be nearly 9% of the total flash!
// The USE_ASM_* options can be disabled by defining them as 0 either in variant pins_arduino.h
// The buffer sizes can be overridden in by defining SERIAL_TX_BUFFER either in variant file (as defines in pins_arduino.h) or boards.txt as (By passing them as extra flags).
// note that buffer sizes must be powers of 2 omly.
#if !defined(SERIAL_TX_BUFFER_SIZE)
  #if   (INTERNAL_SRAM_SIZE  < 1024)  // 128/256b/512b RAM
    #define SERIAL_TX_BUFFER_SIZE 16
  #elif (INTERNAL_SRAM_SIZE < 2048)   // 1k RAM
    #define SERIAL_TX_BUFFER_SIZE 32
  #else
    #define SERIAL_TX_BUFFER_SIZE 64  // 2k/3k RAM
  #endif
#endif
#if !defined(SERIAL_RX_BUFFER_SIZE)
  #if   (INTERNAL_SRAM_SIZE <  512)  // 128/256b RAM
    #define SERIAL_RX_BUFFER_SIZE 16
    // current tx buffer position = SerialClass + txtail + 37
  #elif (INTERNAL_SRAM_SIZE < 1024)  // 512b RAM
    #define SERIAL_RX_BUFFER_SIZE 32
    // current tx buffer position = SerialClass + txtail + 53
  #else
    #define SERIAL_RX_BUFFER_SIZE 64  // 1k+ RAM
    // current tx buffer position = SerialClass + txtail + 85
    // rx buffer position always = SerialClass + rxhead + 21
  #endif
#endif
/* Use INTERNAL_SRAM_SIZE instead of RAMEND - RAMSTART, which is vulnerable to
 * a fencepost error. */
#if (SERIAL_TX_BUFFER_SIZE > 256)
  typedef uint16_t tx_buffer_index_t;
#else
  typedef uint8_t  tx_buffer_index_t;
#endif
#if (SERIAL_RX_BUFFER_SIZE > 256)
  typedef uint16_t rx_buffer_index_t;
#else
  typedef uint8_t  rx_buffer_index_t;
#endif
// As noted above, forcing the sizes to be a power of two saves a small
// amount of flash, and there's no compelling reason to NOT have them be
// a power of two. If this is a problem, since you're already modifying
// core, change the lines in UART.cpp where it does & (SERIAL_xX_BUFFERLSIZE-1)
// and replace them with % SERIAL_xX_BUFFER_SIZE; where xX is TX or RX.
// There are two of each, and the old ending of the line is even commented
// out at the end of the line.
#if (SERIAL_TX_BUFFER_SIZE & (SERIAL_TX_BUFFER_SIZE - 1))
  #error "ERROR: TX buffer size must be a power of two."
#endif
#if (SERIAL_RX_BUFFER_SIZE & (SERIAL_RX_BUFFER_SIZE - 1))
  #error "ERROR: RX buffer size must be a power of two."
#endif

#if USE_ASM_RXC == 1 && !(SERIAL_RX_BUFFER_SIZE == 256 || SERIAL_RX_BUFFER_SIZE == 128 || SERIAL_RX_BUFFER_SIZE == 64 || SERIAL_RX_BUFFER_SIZE == 32 || SERIAL_RX_BUFFER_SIZE == 16)
  #error "Assembly RX Complete (RXC) ISR is only supported when RX buffer size are 256, 128, 64, 32 or 16 bytes"
#endif

#if USE_ASM_DRE == 1 && !((SERIAL_RX_BUFFER_SIZE == 256 || SERIAL_RX_BUFFER_SIZE == 128 || SERIAL_RX_BUFFER_SIZE == 64 || SERIAL_RX_BUFFER_SIZE == 32 || SERIAL_RX_BUFFER_SIZE == 16) && \
                          (SERIAL_TX_BUFFER_SIZE == 256 || SERIAL_TX_BUFFER_SIZE == 128 || SERIAL_TX_BUFFER_SIZE == 64 || SERIAL_TX_BUFFER_SIZE == 32 || SERIAL_TX_BUFFER_SIZE == 16))
  #error "Assembly Data Register Empty (DRE) ISR is only supported when both TX and RX buffer sizes are 256, 128, 64, 32 or 16 bytes"
#endif


#define syncBegin(port, baud, config, syncopts) ({\
  if ((config & 0xC0) == 0x40)                    \
    {pinConfigure(port.getPin(2), syncopts);      \
    port.begin(baud >> 3, config);                \
  }})

#define mspiBegin(port, baud, config, invert) ({  \
  if ((config & 0xC0) == 0xC0) {                  \
    pinConfigure(port.getPin(2), invert);         \
    port.begin(baud >> 3, config);                \
  }})



// tinyAVR 0/1-series has 2 bits devoted to RS485, supporting normal (00), RS485 with XDIR driven to control
// an external line driver (01), and some other mysterious mode (10) the function of which is unclear. There is
// evidence that this poorly documented feature is also present in other hardware, and was only removed on paper.
#if defined(USART_RS4850_bm) && !defined(USART_RS485_bm)
  #define USART_RS485_bm USART_RS4850_bm
#endif
#if defined(__AVR_ATtinyxy2__) // 8-pin parts use a different set of pin mappings.
const uint8_t _usart_pins[][4] = {{PIN_PA6, PIN_PA7, PIN_PA3, PIN_PA0},{PIN_PA1, PIN_PA2, NOT_A_PIN, NOT_A_PIN}};
#elif !defined(__AVR_ATtinyx26__) && !defined(__AVR_ATtinyx27__) // Everything that's not a 2-series with >= 20 pins has the standard mappings.
const uint8_t _usart_pins[][4] = {{PIN_PB2, PIN_PB3, PIN_PB1, PIN_PB0},{PIN_PA1, PIN_PA2, PIN_PA3, PIN_PA4}};
#elif defined(__AVR_ATtinyx26__) || defined(__AVR_ATtinyx27__) // 2-series with 20 or 24 pins have the alt pins for USART1.
const uint8_t _usart_pins[][4] = {
  {PIN_PB2, PIN_PB3, PIN_PB1, PIN_PB0},
  {PIN_PA1, PIN_PA2, PIN_PA3, PIN_PA4},
  {PIN_PC0, PIN_PC1, PIN_PC2, PIN_PC3}
};
#else
  #error "This can't happen - it doesn't have 8, 14, 20 or 24 pins, or it has 14 pins but no serial port - defines aren't being picked up correctly."
#endif
/*
#if defined(__AVR_ATtinyxy2__)
const uint8_t _usart_pins[][7] = {{PIN_PA6, PIN_PA7, PIN_PA0, PIN_PA3},{PIN_PA1, PIN_PA2, NOT_A_PIN, NOT_A_PIN}};
#elif !defined(__AVR_ATtinyx26__) && !defined(__AVR_ATtinyx27__) && defined(MEGATINYCORE_SERIES)
const uint8_t _usart_pins[][7] = {{4, 8, 2, 1, 0x22, 0x23, 0x20},{2, 4, 8, 16, 0x01, 0x02, 0x04}};
#elif defined(__AVR_ATtinyx26__) || defined(__AVR_ATtinyx27__)
const uint8_t _usart_pins[][7] = {
  {4, 8, 2, 1, 0x22, 0x23, 0x20},
  {2, 4, 8, 16, 0x01, 0x02, 0x04},
  {1, 2, 4, 8, 0x40, 0x41, 0x43}
};
*/
#define SERIAL_PIN_SETS 2
// WHAT THE HELL IS WRONG WITH THIS?!
// When this definition is used, row 0 is {PIN_PB3,PIN_PB2,PIN_PB1,PIN_PB0 in this file and row 2 is 0, 0, 0, 0! WTF}
// TX, RX, XCK, XDIR
/*
const uint8_t _usart_pins[][4] = {
  #if defined(HWSERIAL0_MUX)
    #if (defined(PIN_HWSERIAL0_TX) && defined(PIN_HWSERIAL0_RX) && defined(PIN_HWSERIAL0_XCK) && defined(PIN_HWSERIAL0_XIR) && PIN_HWSERIAL0_TX != NOT_A_PIN && PIN_HWSERIAL0_RX != NOT_A_PIN)
      {PIN_HWSERIAL0_TX, PIN_HWSERIAL0_RX, PIN_HWSERIAL0_XCK, PIN_HWSERIAL0_XDIR},
    #else
      {NOT_A_PIN, NOT_A_PIN, NOT_A_PIN, NOT_A_PIN},
    #endif
  #endif
  #if defined(HWSERIAL0_MUX_PINSWAP_1)
    #if (defined(PIN_HWSERIAL0_TX_PINSWAP_1) && defined(PIN_HWSERIAL0_RX_PINSWAP_1) && defined(PIN_HWSERIAL0_XCK_PINSWAP_1) && defined(PIN_HWSERIAL0_XIR_PINSWAP_1) && PIN_HWSERIAL0_TX_PINSWAP_1 != NOT_A_PIN && PIN_HWSERIAL0_RX_PINSWAP_1 != NOT_A_PIN)
      {PIN_HWSERIAL0_TX_PINSWAP_1, PIN_HWSERIAL0_RX_PINSWAP_1, PIN_HWSERIAL0_XCK_PINSWAP_1, PIN_HWSERIAL0_XDIR_PINSWAP_1},
    #else
      {NOT_A_PIN, NOT_A_PIN, NOT_A_PIN, NOT_A_PIN},
    #endif
  #endif
  #if defined(USART1) // On 0/1-series, with only one USART, we don't even need the third row.
    #if defined(HWSERIAL1_MUX_PINSWAP_1)
      #if (defined(PIN_HWSERIAL1_TX_PINSWAP_1) && defined(PIN_HWSERIAL1_RX_PINSWAP_1) && defined(PIN_HWSERIAL1_XCK_PINSWAP_1) && defined(PIN_HWSERIAL1_XIR_PINSWAP_1) && PIN_HWSERIAL1_TX_PINSWAP_1 != NOT_A_PIN && PIN_HWSERIAL1_RX_PINSWAP_1 != NOT_A_PIN)
        {PIN_HWSERIAL1_TX_PINSWAP_1, PIN_HWSERIAL1_RX_PINSWAP_1, PIN_HWSERIAL1_XCK_PINSWAP_1, PIN_HWSERIAL1_XDIR_PINSWAP_1},
      #else
        {NOT_A_PIN, NOT_A_PIN, NOT_A_PIN, NOT_A_PIN},
      #endif
    #endif
  #endif
};
*/



/* DANGER DANGER DANGER */
/* CHANGING THE MEMBER VARIABLES BETWEEN HERE AND THE OTHER SCARY COMMENT WILL COMPLETELY BREAK SERIAL
 * WHEN USE_ASM_DRE and/or USE_ASM_RXC is used! */
/* DANGER DANGER DANGER */
class UartClass : public HardwareSerial {
  protected:
    volatile USART_t *const _hwserial_module;
    const uint8_t _module_number;
    uint8_t _pin_set;

    uint8_t _state; /* 0b000000hw */
    // h = half duplex with open drain - disable RX while TX.
    // w = written (like old _written)

    volatile rx_buffer_index_t _rx_buffer_head;
    volatile rx_buffer_index_t _rx_buffer_tail;
    volatile tx_buffer_index_t _tx_buffer_head;
    volatile tx_buffer_index_t _tx_buffer_tail;

    // Don't put any members after these buffers, since only the first
    // 32 bytes of this struct can be accessed quickly using the ldd
    // instruction.
    volatile unsigned char _rx_buffer[SERIAL_RX_BUFFER_SIZE];
    volatile unsigned char _tx_buffer[SERIAL_TX_BUFFER_SIZE];
/* DANGER DANGER DANGER */
/* ANY CHANGES BETWEEN OTHER SCARY COMMENT AND THIS ONE WILL BREAK SERIAL when USE_ASM_DRE or USE_ASM_RXC is used! */
/* DANGER DANGER DANGER */

  public:
    inline             UartClass(volatile USART_t *hwserial_module, uint8_t module_number, uint8_t default_pinset);
    bool                    pins(uint8_t tx, uint8_t rx);
    bool                    swap(uint8_t mux_level = 1);
    void                   begin(uint32_t baud) {begin(baud, SERIAL_8N1);}
    void                   begin(uint32_t baud, uint16_t options);
    void                     end();
    // Basic printHex() forms for 8, 16, and 32-bit values
    void                printHex(const     uint8_t              b);
    void                printHex(const    uint16_t  w, bool s = 0);
    void                printHex(const    uint32_t  l, bool s = 0);
    // printHex(signed) and printHexln() - trivial implementation;
    void                printHex(const      int8_t  b)              {printHex((uint8_t )   b);           }
    void                printHex(const        char  b)              {printHex((uint8_t )   b);           }
    void              printHexln(const      int8_t  b)              {printHex((uint8_t )   b); println();}
    void              printHexln(const        char  b)              {printHex((uint8_t )   b); println();}
    void              printHexln(const     uint8_t  b)              {printHex(             b); println();}
    void              printHexln(const    uint16_t  w, bool s = 0)  {printHex(          w, s); println();}
    void              printHexln(const    uint32_t  l, bool s = 0)  {printHex(          l, s); println();}
    void              printHexln(const     int16_t  w, bool s = 0)  {printHex((uint16_t)w, s); println();}
    void              printHexln(const     int32_t  l, bool s = 0)  {printHex((uint16_t)l, s); println();}
    // The pointer-versions for mass printing uint8_t and uint16_t arrays.
    uint8_t *           printHex(          uint8_t* p, uint8_t len, char sep = 0            );
    uint16_t *          printHex(         uint16_t* p, uint8_t len, char sep = 0, bool s = 0);
    volatile uint8_t *  printHex(volatile  uint8_t* p, uint8_t len, char sep = 0            );
    volatile uint16_t * printHex(volatile uint16_t* p, uint8_t len, char sep = 0, bool s = 0);

    virtual int availableForWrite(void);
    virtual int available(void);
    virtual      int peek(void);
    virtual      int read(void);
    virtual    void flush(void);
    virtual  size_t write(uint8_t ch);
    inline   size_t write(unsigned long n)  {return write((uint8_t)n);}
    inline   size_t write(long n)           {return write((uint8_t)n);}
    inline   size_t write(unsigned int n)   {return write((uint8_t)n);}
    inline   size_t write(int n)            {return write((uint8_t)n);}
    using Print::write; // pull in write(str) and write(buf, size) from Print
    explicit operator bool() {
      return true;
    }
    uint8_t getPin(uint8_t pin);

    // Interrupt handlers - Not intended to be called externally
    #if !(USE_ASM_RXC == 1 && (SERIAL_RX_BUFFER_SIZE == 256 || SERIAL_RX_BUFFER_SIZE == 128 || SERIAL_RX_BUFFER_SIZE == 64 || SERIAL_RX_BUFFER_SIZE == 32 || SERIAL_RX_BUFFER_SIZE == 16))
      static void _rx_complete_irq(UartClass& uartClass);
    #endif
    #if !(USE_ASM_DRE == 1 && (SERIAL_RX_BUFFER_SIZE == 256 || SERIAL_RX_BUFFER_SIZE == 128 || SERIAL_RX_BUFFER_SIZE == 64 || SERIAL_RX_BUFFER_SIZE == 32 || SERIAL_RX_BUFFER_SIZE == 16) && \
                              (SERIAL_TX_BUFFER_SIZE == 256 || SERIAL_TX_BUFFER_SIZE == 128 || SERIAL_TX_BUFFER_SIZE == 64 || SERIAL_TX_BUFFER_SIZE == 32 || SERIAL_TX_BUFFER_SIZE == 16))
      static void _tx_data_empty_irq(UartClass& uartClass);
    #endif

  private:
    void _poll_tx_data_empty(void);
    static void        _set_pins(uint8_t port_num, uint8_t mux_setting, uint8_t enmask);
    static uint8_t _pins_to_swap(uint8_t port_num, uint8_t tx_pin, uint8_t rx_pin);
};

#if defined(USART0)
  extern UartClass Serial;
#endif
#if defined(USART1)
  extern UartClass Serial1;
#endif
