/*
 * Copyright 2019 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "board.h"
#include "app.h"
#include "fsl_debug_console.h"
#include "fsl_ctimer.h"
#include "peripherals.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
/* Tiempo muerto tras aceptar una pulsacion, en ms (antirrebote) */
#define DEBOUNCE_MS 50U

/* 3 LEDs on/off -> 2^3 = 8 combinaciones. bit2=R, bit1=G, bit0=B */
#define COLOR_COUNT 8U

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
void color_change(void);
void color_loop_speed_change(void);

/*******************************************************************************
 * Variables
 ******************************************************************************/
/* Compartidas entre ISR y main -> obligatorio volatile */
volatile uint32_t g_msTicks         = 0U;
volatile bool     g_swModePressed   = false; /* SW2 / SW_MODE  (P1_7, GPIO1) */
volatile bool     g_swSpeedPressed  = false; /* SW3 / SW_SPEED (P0_6, GPIO0) */

uint8_t  n                 = 0U;
uint32_t timedalay         = 500000U; /* us entre colores en modo automatico */
bool     flag_change_color = false;
uint8_t  dutyCicle           = 10U; /* duty cycle del PWM, en % */

/*******************************************************************************
 * Code
 ******************************************************************************/
/* Base de tiempos de 1 ms, usada por el antirrebote */
void SysTick_Handler(void)
{
    g_msTicks++;
}

/* =========================================================
 * SW2 / SW_MODE  -> P1_7 -> GPIO1_IRQn
 * BOARD_SW2_IRQ_HANDLER expande a GPIO1_IRQHandler (board.h)
 * ========================================================= */
void BOARD_SW2_IRQ_HANDLER(void)
{
    static uint32_t lastMs = 0U;
    uint32_t        flags  = GPIO_PortGetInterruptFlags(BOARD_SW2_GPIO);
    
    DbgConsole_Printf("SW2 IRQ: flags = 0x%08X\r\n", (unsigned)flags);  
    if ((flags & (1UL << BOARD_SW2_GPIO_PIN)) != 0UL)
    {
        /* Limpiar el flag SIEMPRE, aunque el rebote se descarte despues */
        GPIO_PortClearInterruptFlags(BOARD_SW2_GPIO, 1UL << BOARD_SW2_GPIO_PIN);

        if ((uint32_t)(g_msTicks - lastMs) >= DEBOUNCE_MS)
        {
            lastMs          = g_msTicks;
            g_swModePressed = true;
        }
    }

    SDK_ISR_EXIT_BARRIER;
}

/* =========================================================
 * SW3 / SW_SPEED -> P0_6 -> GPIO0_IRQn
 * BOARD_SW3_IRQ_HANDLER expande a GPIO0_IRQHandler (board.h)
 * ========================================================= */
void BOARD_SW3_IRQ_HANDLER(void)
{
    static uint32_t lastMs = 0U;
    uint32_t        flags  = GPIO_PortGetInterruptFlags(BOARD_SW3_GPIO);

    DbgConsole_Printf("SW3 IRQ: flags = 0x%08X\r\n", (unsigned)flags);  
    if ((flags & (1UL << BOARD_SW3_GPIO_PIN)) != 0UL)
    {
        GPIO_PortClearInterruptFlags(BOARD_SW3_GPIO, 1UL << BOARD_SW3_GPIO_PIN);

        if ((uint32_t)(g_msTicks - lastMs) >= DEBOUNCE_MS)
        {
            lastMs           = g_msTicks;
            g_swSpeedPressed = true;
        }
    }

    SDK_ISR_EXIT_BARRIER;
}

/*!
 * @brief Main function
 */
int main(void)
{
    /* BOARD_InitHardware() ya inicializa la consola (BOARD_InitDebugConsole) */
    BOARD_InitHardware();

    /* SysTick a 1 ms: base de tiempos del antirrebote.
     * Sustituye al reload de 1 s que genera el config tool. */
    (void)SysTick_Config(SystemCoreClock / 1000U);

    /* Habilitar en el NVIC las dos lineas de interrupcion */
    EnableIRQ(BOARD_SW2_IRQ); /* GPIO1_IRQn */
    EnableIRQ(BOARD_SW3_IRQ); /* GPIO0_IRQn */

    /* Configuracion de perifericos */

    /*Configurar CTIMER0 como PWM0 en el canal 0 en J2 P2_12 */
    if (CTIMER_SetupPwm(CTIMER0,
                        kCTIMER_Match_3,               /* canal de periodo */
                        kCTIMER_Match_0,               /* salida en P2_12 (CT0_MAT0) */
                        50U,                           /* duty 50 % */
                        1000U,                         /* 1 kHz */
                        CLOCK_GetCTimerClkFreq(0U),    /* 12 MHz */
                        false) != kStatus_Success)
    {
        DbgConsole_Printf("Error: CTIMER_SetupPwm fallo\r\n");
    }

    DbgConsole_Printf("\r\n=== rgb_control: consola lista ===\r\n");
    DbgConsole_Printf("SW2: cambia el color del LED\r\n");
    DbgConsole_Printf("SW3: cambia la velocidad de parpadeo\r\n");

    /* Arranque de periferico */
    /* Arranque del timer 0 */
    CTIMER_StartTimer(CTIMER0_PERIPHERAL);

    /* Bucle principal */
    while (1)
    {
        DbgConsole_Printf("color = %u, periodo = %u us\r\n", (unsigned)n, (unsigned)timedalay);
        if (g_swModePressed)
        {
            DbgConsole_Printf("SW2 pulsado: color = %u\r\n", (unsigned)n);
            g_swModePressed = false;

            color_change();
            flag_change_color = false; /* para el modo automatico */
            if (CTIMER_UpdatePwmDutycycle(CTIMER0, kCTIMER_Match_3, kCTIMER_Match_0, dutyCicle) != kStatus_Success)
            {
                DbgConsole_Printf("Error: CTIMER_UpdatePwmDutycycle fallo\r\n");
            }
        }

        if (g_swSpeedPressed)
        {
            DbgConsole_Printf("SW3 pulsado: periodo = %u us\r\n", (unsigned)timedalay);
            g_swSpeedPressed = false;

            /* 80 % de la velocidad anterior, en aritmetica entera */
            timedalay = (timedalay * 4U) / 5U;
            if (timedalay < 5000U)
            {
                timedalay = 500000U; /* reinicia a 500 ms */
            }

            flag_change_color = true; /* arranca el modo automatico */
            DbgConsole_Printf("SW3 pulsado: periodo = %u us\r\n", (unsigned)timedalay);
        }

        if (flag_change_color)
        {
            color_loop_speed_change();
        }
    }
}

/*Color change function*/
/*@brief Changes the color of the LED */
void color_change(void)
{
    GPIO_PinWrite(BOARD_LED_RED_GPIO, BOARD_LED_RED_GPIO_PIN, (n >> 2) & 1U);
    GPIO_PinWrite(BOARD_LED_GREEN_GPIO, BOARD_LED_GREEN_GPIO_PIN, (n >> 1) & 1U);
    GPIO_PinWrite(BOARD_LED_BLUE_GPIO, BOARD_LED_BLUE_GPIO_PIN, n & 1U);
    n = (uint8_t)((n + 1U) % COLOR_COUNT);
}

/*color loop speed change function*/
void color_loop_speed_change(void)
{
    color_change();
    /* Ojo: el reloj real es SystemCoreClock (12 MHz), no el maximo del chip */
    SDK_DelayAtLeastUs(timedalay, SystemCoreClock);
}
