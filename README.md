# step_5_uart_console
Building on step 4, provide a UART console to control the LED patterns alonside the User Button

### Using these versions:
* `STM32CubeIDE` : `1.18.1`
* `STM32CubeMX` : `6.14.1`

### What is this?
I want a console that:
* Uses UART RX via Direct Memory Access (DMA) (no blocking reads)
* Accepts human-typed commands over a serial terminal
* Works alongside other tasks (LEDs, timers, etc.)
* Is robust against partial lines, overruns, and idle gaps

### High-level architecture
**Key idea:**
DMA fills a circular buffer → IRQ tells us "new data arrived" → you parse it
```
[ UART ] → [ DMA circular buffer ] → [ IRQ/IDLE event ]
                                     ↓
                               software RX buffer
                                     ↓
                              line / command parser
```
**DMA does NOT give you lines or commands**
It gives us bytes. Everything else is firmware responsibility

### What will change?
I'm adding:
* USART2 + DMA RX
* A console module
  * `console.c`
  * `console.h`
* One new scheduled task
  * `task_console()`

## Step 1: Enable USUART2
Open the .ioc in STM32CubeIDE
1. **Connectivity** → **USART2**, then under "**Mode**":
    * Mode: _Asynchronous_
    * Baud rate: `115200`
    * Word length: 8 bits
    * Parity: None
    * Stop bits: 1
2. Under "**Configuration**" and "**DMA Settings**":
    * Add **USART2_RX**
    * Mode: **Circular**
    * Data width: Byte
    * Priority: Low (fine)
3. **NVIC Settings**
    * Enable **USART2 global interrupt**
    * Enable **DMA RX interrupt**
4. Click **Generate Code**

This will create:
* `Core/Src/usart.c`
* `Core/Inc/usart.h`
* A global:
  ```c
  UART_HandleTypeDef huart2;
  ```

Until this exists, **nothing UART-related can compile**

## STEP 2: Create console.h
Create `Core/Inc/console.h`:
```c
#ifndef CONSOLE_H
#define CONSOLE_H

void console_init(void);
void task_console(void);

#endif
```

## STEP 3: Create console.c (this is where UART lives)
Create `Core/Src/console.c`:
```c
#include "console.h"
#include "usart.h"
#include "dma.h"
#include <string.h>
```

Now put **ALL** of this here (not in `main.c`):
```c
#define UART_RX_DMA_BUF_SIZE 128
#define LINE_BUF_SIZE 64

static uint8_t uart_rx_dma_buf[UART_RX_DMA_BUF_SIZE];
static uint16_t dma_last_pos = 0;

static char line_buf[LINE_BUF_SIZE];
static uint8_t line_len = 0;
```

`console_init()`
```c
void console_init(void)
{
    HAL_UART_Receive_DMA(
        &huart2,
        uart_rx_dma_buf,
        UART_RX_DMA_BUF_SIZE
    );

    __HAL_UART_ENABLE_IT(&huart2, UART_IT_IDLE);
}
```

`task_console()`
```c
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
```

**RX byte processing**
```c
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
```
## STEP 4: Hook the USART2 IRQ
In `stm32f4xx_it.c` update the stub function to include this user code:
```c
void USART2_IRQHandler(void)
{
    if (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_IDLE))
    {
        __HAL_UART_CLEAR_IDLEFLAG(&huart2);
        rx_pending = 1;
    }
    HAL_UART_IRQHandler(&huart2);
}
```

* `rx_pending` must be `extern`:
  * Add to `console.c`...
    ```c
    volatile uint8_t rx_pending;
    ```
  * and in stm32f4xx_it.c:
    ```c
    extern volatile uint8_t rx_pending;
    ```

## Step 5: Call `console_init()` from `main()`
In `main.c`, after peripheral init:
```c
MX_GPIO_Init();
MX_TIM2_Init();
MX_USART2_UART_Init();   // <-- CubeMX generated
console_init();
```
## Exactly what to do on Windows PC
**1. Plug in the board via USB (ST-LINK USB)**
* No extra cables; just the normal Nucleo USB connector
   
**2. Find the COM port**
  * Open **Device Manager**
  * Expand **Ports (COM & LPT)**
  * Look for something like:
    ```
    STMicroelectronics STLink Virtual COM Port (COMx)
    ```
  * Note the `COMx` number

**3. Open a PuTTY terminal**
  * ****PuTTY settings**:
    * Connection type: **Serial**
    * Serial line: `COMx`
    * Speed (baud): **115200**
    * Data bits: 8
    * Parity: None
    * Stop bits: 1
    * Flow control: None
  * Then click **Open**

## What you should see right now

With the current code, behavior is:
* You type characters → **nothing appears immediately** (no echo yet)
* You press **Enter**
* The board echoes back the full line you typed

Example:
```
hello
hello
```
That’s intentional — we haven’t added character echo yet.

## Next steps...
* Once this is building, it will just echo text lines in the connected console
* Next...
  * Immediate character echo and Backspace handling
  * Adding a Prompt (`> `)
  * Commands to control LED modes
    * Add command parsing
    * Control `led.mode` from console
    * Add backspace support
    * Add DMA TX
