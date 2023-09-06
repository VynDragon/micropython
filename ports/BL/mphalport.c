#include <unistd.h>
#include <string.h>

#include "mpconfigboard.h"
#include "py/mpconfig.h"
#include "py/builtin.h"
#include "bflb_uart.h"

// Receive single character, blocking until one is available.
int mp_hal_stdin_rx_chr(void) {
    unsigned char c = 0;
    struct bflb_device_s *uart0 = bflb_device_get_by_name("uart0");
    if (uart0 != NULL)
    {
        c = bflb_uart_getchar(uart0);
    }
    return c;
}


// Send the string of given length.
void mp_hal_stdout_tx_strn(const char *str, mp_uint_t len) {
    struct bflb_device_s *uart0 = bflb_device_get_by_name("uart0");
    if (uart0 != NULL)
    {
        bflb_uart_put(uart0, (uint8_t *)str, len);
    }
    return;
}
