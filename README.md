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

## Step 2: Create console.h
Create `Core/Inc/console.h`:
```c
#ifndef CONSOLE_H
#define CONSOLE_H

void console_init(void);
void task_console(void);

#endif
```

## Step 3: Create console.c (this is where UART lives)
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
## Step 4: Hook the USART2 IRQ
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
  * Step 6.1 : Immediate character echo and Backspace handling
  * Step 6.2 : Adding a Prompt (`> `)
  * Step 6.3 : Commands to control LED modes
    * Add command parsing
      * Command table `(struct { name, fn })`
      * Used to control `led.mode` from console
    * Command history (↑ ↓)
    * Tab completion
    * Add DMA TX
    * Redirect `printf()`

## Step 6.1: 
Add **immediate echo** and **proper backspace handling**, without breaking:
* DMA RX
* Your scheduler
* Low-power `__WFI()`

### Target behaviour
After this change:
* Each typed character appears immediately
* Backspace:
  * Removes the character from the line buffer
  * Erases it on the terminal ("\b \b")
* Enter:
  * Ends the line
  * Processes the command
* Buffer overflow handled safely

**This change lives only in `console.c`**
* No changes to:
  * main.c
  * Scheduler
  * DMA setup
  * IRQs
 
### Recognise control characters
The terminal will send:

| Key       | Value            |
| --------- | ---------------- |
| Backspace | `0x08` or `0x7F` |
| Enter     | `'\r'` or `'\n'` |

We need to handle both

### Update console_process_bytes()
Replace the existing function with this:
```c
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
```

## Step 6.2: Add a prompt
After handling a command, print a prompt:
```c
static void console_prompt(void)
{
    HAL_UART_Transmit(
        &huart2,
        (uint8_t *)"> ",
        2,
        HAL_MAX_DELAY
    );
}
```

Call it:
* Once at end of `console_init()`
* After each command completes

This makes the console feel a little more professional

## Step 6.3: Bind console commands to the existing LED modes
1. Introduce a **command table** (data-driven, not if/else soup)
2. Bind commands to **the existing LED modes** (no new LED logic)
3. Keep parsing / execution cleanly separated
4. Leave this set-up as a future reference for easy expansion later

### Define the command concept
Each command needs:
* a name (`"led"`)
* a handler function
* a short help string

**Add this to `console.c`:**
```c
typedef void (*console_cmd_fn_t)(int argc, char *argv[]);

typedef struct
{
    const char        *name;
    console_cmd_fn_t   fn;
    const char        *help;
} console_cmd_t;
```

### Expose LED control cleanly (important)
Right now, the LED logic lives in `main.c` and that’s good — we won’t break it.

What we need is **one small interface** the console can call.

In `main.h`, add:
```c
typedef enum
{
    LED_MODE_OFF = 0,
    LED_MODE_SLOW,
    LED_MODE_FAST
} led_mode_t;

void led_set_mode(led_mode_t mode);
led_mode_t led_get_mode(void);
```

In `main.c`, add implementations (near your other LED code):
```c
void led_set_mode(led_mode_t mode)
{
    led.mode = mode;
}

led_mode_t led_get_mode(void)
{
    return led.mode;
}
```

* Now the console never touches GPIOs
* It only expresses _intent_
* This is exactly how bigger systems are structured

### Implement the LED command
In `console.c`:
```c
static void cmd_led(int argc, char *argv[])
{
    if (argc < 2)
    {
        console_write("usage: led off|slow|fast\r\n");
        return;
    }

    if (strcmp(argv[1], "off") == 0)
    {
        led_set_mode(LED_MODE_OFF);
    }
    else if (strcmp(argv[1], "slow") == 0)
    {
        led_set_mode(LED_MODE_SLOW);
    }
    else if (strcmp(argv[1], "fast") == 0)
    {
        led_set_mode(LED_MODE_FAST);
    }
    else
    {
        console_write("invalid mode\r\n");
        return;
    }

    console_write("ok\r\n");
}
```

This binds **directly** to the existing LED behaviour: no duplication; no hacks

### Implement help
Add the following:
```c
static void cmd_help(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    for (size_t i = 0; i < CMD_COUNT; i++)
    {
        console_write(cmd_table[i].name);
        console_write(" - ");
        console_write(cmd_table[i].help);
        console_write("\r\n");
    }
}
```

### Build the command table
At file scope in `console.c`:
```c
static const console_cmd_t cmd_table[] =
{
    { "help", cmd_help, "show this help" },
    { "led",  cmd_led,  "led off|slow|fast" },
};

#define CMD_COUNT (sizeof(cmd_table) / sizeof(cmd_table[0]))
```

This is the heart of the design.

To add a new command later:

* write a function
* add one row

No parser changes. No spaghetti

### Dispatch commands from your line handler
Update `console_handle_command()` and call when **Enter** is pressed:
```c
static void console_execute(char *line)
{
    char *argv[8];
    int argc = 0;

    char *token = strtok(line, " ");
    while (token && argc < 8)
    {
        argv[argc++] = token;
        token = strtok(NULL, " ");
    }

    if (argc == 0)
        return;

    for (size_t i = 0; i < CMD_COUNT; i++)
    {
        if (strcmp(argv[0], cmd_table[i].name) == 0)
        {
            cmd_table[i].fn(argc, argv);
            return;
        }
    }

    console_write("unknown command\r\n");
    console_prompt();
}
```

### What we now have
We've just built:
* A non-blocking DMA UART console
* Immediate echo + backspace
* A table-driven command interpreter
* Proper module boundaries
* Console commands bound to real system behaviour

This is no longer "LED blink training"; this is the skeleton of a profesional-level firmware project
