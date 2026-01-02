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

void console_init(void)
{
    HAL_UART_Receive_DMA(
        &huart2,
        uart_rx_dma_buf,
        UART_RX_DMA_BUF_SIZE
    );

    __HAL_UART_ENABLE_IT(&huart2, UART_IT_IDLE);
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

        if (c == '\r' || c == '\n')
        {
            if (line_len > 0)
            {
                line_buf[line_len] = '\0';
                HAL_UART_Transmit(
                    &huart2,
                    (uint8_t *)line_buf,
                    line_len,
                    HAL_MAX_DELAY
                );
                HAL_UART_Transmit(
                    &huart2,
                    (uint8_t *)"\r\n",
                    2,
                    HAL_MAX_DELAY
                );
                line_len = 0;
            }
        }
        else if (line_len < LINE_BUF_SIZE - 1)
        {
            line_buf[line_len++] = c;
        }
        else
        {
            line_len = 0;
        }
    }
}

