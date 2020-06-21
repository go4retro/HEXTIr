#ifndef DEBUG_H
#define DEBUG_H

#if defined CONFIG_UART_DEBUG || defined CONFIG_UART_DEBUG_SW
void debug_putc(uint8_t data);
void debug_puts(const char *text);
void debug_puts_P(const char *text);
void debug_puthex(uint8_t hex);
void debug_putcrlf(void);
void debug_trace(void *ptr, uint16_t start, uint16_t len);
void debug_init(void);
#else
#  define debug_putc(x)           do {} while(0)
#  define debug_puthex(x)         do {} while(0)
#  define debug_trace(x,y,z)      do {} while(0)
#  define debug_puts(x)           do {} while(0)
#  define debug_puts_P(x)         do {} while(0)
#  define debug_putcrlf()         do {} while(0)
#endif
#endif
