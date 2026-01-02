#include "console.h"
#include "usart.h"
#include "dma.h"
#include <string.h>

#define UART_RX_DMA_BUF_SIZE 128
#define LINE_BUF_SIZE 64

static uint8_t uart_rx_dma_buf[UART_RX_DMA_BUF_SIZE];
static uint16_t dma_last_pos = 0;
volatile uint8_t rx_pending = 0;

static char line_buf[LINE_BUF_SIZE];
static uint8_t line_len = 0;

static void console_process_bytes(uint8_t *data, uint16_t len);

static void console_prompt(void)
{
    HAL_UART_Transmit(
        &huart2,
        (uint8_t *)"> ",
        2,
        HAL_MAX_DELAY
    );
}

static void console_write(const char *s)
{
    HAL_UART_Transmit(
        &huart2,
        (uint8_t *)s,
        strlen(s),
        HAL_MAX_DELAY
    );
}

static void console_handle_command(const char *cmd)
{
	static const char help_msg[] =
	    "help, led off, led slow, led fast\r\n";

    if (strcmp(cmd, "help") == 0)
    {
        console_write("help, led off, led slow, led fast\r\n");
    }

    console_prompt();
}

void console_init(void)
{
    HAL_UART_Receive_DMA(
        &huart2,
        uart_rx_dma_buf,
        UART_RX_DMA_BUF_SIZE
    );

    __HAL_UART_ENABLE_IT(&huart2, UART_IT_IDLE);

    console_prompt();
}

void task_console(void)
{
    if (!rx_pending)
        return;

    rx_pending = 0;

    uint16_t dma_pos =
        UART_RX_DMA_BUF_SIZE - __HAL_DMA_GET_COUNTER(huart2.hdmarx);

    if (dma_pos != dma_last_pos)
    {
        if (dma_pos > dma_last_pos)
        {
            console_process_bytes(
                &uart_rx_dma_buf[dma_last_pos],
                dma_pos - dma_last_pos
            );
        }
        else
        {
            console_process_bytes(
                &uart_rx_dma_buf[dma_last_pos],
                UART_RX_DMA_BUF_SIZE - dma_last_pos
            );
            if (dma_pos > 0)
            {
                console_process_bytes(
                    &uart_rx_dma_buf[0],
                    dma_pos
                );
            }
        }
        dma_last_pos = dma_pos;
    }
}

static void console_process_bytes(uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
    {
        char c = data[i];

        /* ENTER */
        if (c == '\r' || c == '\n')
        {
            HAL_UART_Transmit(
                &huart2,
                (uint8_t *)"\r\n",
                2,
                HAL_MAX_DELAY
            );

            if (line_len > 0)
            {
                line_buf[line_len] = '\0';
                console_handle_command(line_buf);
                line_len = 0;
            }
        }

        /* BACKSPACE */
        else if (c == 0x08 || c == 0x7F)
        {
            if (line_len > 0)
            {
                line_len--;

                /* erase character on terminal */
                HAL_UART_Transmit(
                    &huart2,
                    (uint8_t *)"\b \b",
                    3,
                    HAL_MAX_DELAY
                );
            }
        }

        /* PRINTABLE CHARACTER */
        else if (c >= 0x20 && c <= 0x7E)
        {
            if (line_len < LINE_BUF_SIZE - 1)
            {
                line_buf[line_len++] = c;

                /* echo */
                HAL_UART_Transmit(
                    &huart2,
                    (uint8_t *)&c,
                    1,
                    HAL_MAX_DELAY
                );
            }
            else
            {
                /* optional: bell or ignore */
            }
        }
    }
}

