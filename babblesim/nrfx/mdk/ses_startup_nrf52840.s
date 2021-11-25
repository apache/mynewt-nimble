/***********************************************************************************
 *                           SEGGER Microcontroller GmbH                           *
 *                               The Embedded Experts                              *
 ***********************************************************************************
 *                                                                                 *
 *                   (c) 2014 - 2018 SEGGER Microcontroller GmbH                   *
 *                                                                                 *
 *                  www.segger.com     Support: support@segger.com                 *
 *                                                                                 *
 ***********************************************************************************
 *                                                                                 *
 *        All rights reserved.                                                     *
 *                                                                                 *
 *        Redistribution and use in source and binary forms, with or               *
 *        without modification, are permitted provided that the following          *
 *        conditions are met:                                                      *
 *                                                                                 *
 *        - Redistributions of source code must retain the above copyright         *
 *          notice, this list of conditions and the following disclaimer.          *
 *                                                                                 *
 *        - Neither the name of SEGGER Microcontroller GmbH                        *
 *          nor the names of its contributors may be used to endorse or            *
 *          promote products derived from this software without specific           *
 *          prior written permission.                                              *
 *                                                                                 *
 *        THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND                   *
 *        CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,              *
 *        INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF                 *
 *        MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE                 *
 *        DISCLAIMED.                                                              *
 *        IN NO EVENT SHALL SEGGER Microcontroller GmbH BE LIABLE FOR              *
 *        ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR                 *
 *        CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT        *
 *        OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;          *
 *        OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF            *
 *        LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT                *
 *        (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE        *
 *        USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH         *
 *        DAMAGE.                                                                  *
 *                                                                                 *
 ***********************************************************************************/

/************************************************************************************
 *                         Preprocessor Definitions                                 *
 *                         ------------------------                                 *
 * VECTORS_IN_RAM                                                                   *
 *                                                                                  *
 *   If defined, an area of RAM will large enough to store the vector table         *
 *   will be reserved.                                                              *
 *                                                                                  *
 ************************************************************************************/

  .syntax unified
  .code 16

  .section .init, "ax"
  .align 0


/************************************************************************************
 * Macros                                                                           *
 ************************************************************************************/

// Directly place a vector (word) in the vector table
.macro VECTOR Name=
        .section .vectors, "ax"
        .code 16
        .word \Name
.endm

// Declare an exception handler with a weak definition
.macro EXC_HANDLER Name=
        // Insert vector in vector table
        .section .vectors, "ax"
        .word \Name
        // Insert dummy handler in init section
        .section .init.\Name, "ax"
        .thumb_func
        .weak \Name
        .balign 2
\Name:
        1: b 1b   // Endless loop
.endm

// Declare an interrupt handler with a weak definition
.macro ISR_HANDLER Name=
        // Insert vector in vector table
        .section .vectors, "ax"
        .word \Name
        // Insert dummy handler in init section
#if defined(__OPTIMIZATION_SMALL)
        .section .init, "ax"
        .weak \Name
        .thumb_set \Name,Dummy_Handler
#else
        .section .init.\Name, "ax"
        .thumb_func
        .weak \Name
        .balign 2
\Name:
        1: b 1b   // Endless loop
#endif
.endm

// Place a reserved vector in vector table
.macro ISR_RESERVED
        .section .vectors, "ax"
        .word 0
.endm

// Place a reserved vector in vector table
.macro ISR_RESERVED_DUMMY
        .section .vectors, "ax"
        .word Dummy_Handler
.endm

/************************************************************************************
 * Reset Handler Extensions                                                         *
 ************************************************************************************/

  .extern Reset_Handler
  .global nRFInitialize
  .extern afterInitialize

  .thumb_func
nRFInitialize:
  bx lr
 
 
/************************************************************************************
 * Vector Table                                                                     *
 ************************************************************************************/

  .section .vectors, "ax"
  .align 0
  .global _vectors
  .extern __stack_end__

_vectors:
  VECTOR        __stack_end__
  VECTOR        Reset_Handler
  EXC_HANDLER   NMI_Handler
  EXC_HANDLER   HardFault_Handler
  EXC_HANDLER   MemoryManagement_Handler
  EXC_HANDLER   BusFault_Handler
  EXC_HANDLER   UsageFault_Handler
  ISR_RESERVED                           /*Reserved */
  ISR_RESERVED                           /*Reserved */
  ISR_RESERVED                           /*Reserved */
  ISR_RESERVED                           /*Reserved */
  EXC_HANDLER   SVC_Handler
  EXC_HANDLER   DebugMon_Handler
  ISR_RESERVED                           /*Reserved */
  EXC_HANDLER   PendSV_Handler
  EXC_HANDLER   SysTick_Handler

/* External Interrupts */
  ISR_HANDLER   POWER_CLOCK_IRQHandler
  ISR_HANDLER   RADIO_IRQHandler
  ISR_HANDLER   UARTE0_UART0_IRQHandler
  ISR_HANDLER   SPIM0_SPIS0_TWIM0_TWIS0_SPI0_TWI0_IRQHandler
  ISR_HANDLER   SPIM1_SPIS1_TWIM1_TWIS1_SPI1_TWI1_IRQHandler
  ISR_HANDLER   NFCT_IRQHandler
  ISR_HANDLER   GPIOTE_IRQHandler
  ISR_HANDLER   SAADC_IRQHandler
  ISR_HANDLER   TIMER0_IRQHandler
  ISR_HANDLER   TIMER1_IRQHandler
  ISR_HANDLER   TIMER2_IRQHandler
  ISR_HANDLER   RTC0_IRQHandler
  ISR_HANDLER   TEMP_IRQHandler
  ISR_HANDLER   RNG_IRQHandler
  ISR_HANDLER   ECB_IRQHandler
  ISR_HANDLER   CCM_AAR_IRQHandler
  ISR_HANDLER   WDT_IRQHandler
  ISR_HANDLER   RTC1_IRQHandler
  ISR_HANDLER   QDEC_IRQHandler
  ISR_HANDLER   COMP_LPCOMP_IRQHandler
  ISR_HANDLER   SWI0_EGU0_IRQHandler
  ISR_HANDLER   SWI1_EGU1_IRQHandler
  ISR_HANDLER   SWI2_EGU2_IRQHandler
  ISR_HANDLER   SWI3_EGU3_IRQHandler
  ISR_HANDLER   SWI4_EGU4_IRQHandler
  ISR_HANDLER   SWI5_EGU5_IRQHandler
  ISR_HANDLER   TIMER3_IRQHandler
  ISR_HANDLER   TIMER4_IRQHandler
  ISR_HANDLER   PWM0_IRQHandler
  ISR_HANDLER   PDM_IRQHandler
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_HANDLER   MWU_IRQHandler
  ISR_HANDLER   PWM1_IRQHandler
  ISR_HANDLER   PWM2_IRQHandler
  ISR_HANDLER   SPIM2_SPIS2_SPI2_IRQHandler
  ISR_HANDLER   RTC2_IRQHandler
  ISR_HANDLER   I2S_IRQHandler
  ISR_HANDLER   FPU_IRQHandler
  ISR_HANDLER   USBD_IRQHandler
  ISR_HANDLER   UARTE1_IRQHandler
  ISR_HANDLER   QSPI_IRQHandler
  ISR_HANDLER   CRYPTOCELL_IRQHandler
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_HANDLER   PWM3_IRQHandler
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_HANDLER   SPIM3_IRQHandler
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
  ISR_RESERVED_DUMMY                           /*Reserved */
_vectors_end:

#ifdef VECTORS_IN_RAM
  .section .vectors_ram, "ax"
  .align 0
  .global _vectors_ram

_vectors_ram:
  .space _vectors_end - _vectors, 0
#endif

/*********************************************************************
*
*  Dummy handler to be used for reserved interrupt vectors
*  and weak implementation of interrupts.
*
*/
        .section .init.Dummy_Handler, "ax"
        .thumb_func
        .weak Dummy_Handler
        .balign 2
Dummy_Handler:
        1: b 1b   // Endless loop
