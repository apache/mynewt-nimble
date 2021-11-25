/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef __MCU_MCU_H_
#define __MCU_MCU_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Defines for naming GPIOs. NOTE: the nordic chip docs use numeric labels for
 * ports. Port A corresponds to Port 0, B to 1, etc. The nrf52832 has only one
 * port and thus uses pins 0 - 31. The nrf52840 has two ports but Port 1 only
 * has 16 pins.
 */
#define MCU_GPIO_PORTA(pin)	((0 * 16) + (pin))
#define MCU_GPIO_PORTB(pin)	((1 * 16) + (pin))

#if NRF52

#define MCU_SYSVIEW_INTERRUPTS \
    "I#1=Reset,I#2=MNI,I#3=HardFault,I#4=MemoryMgmt,I#5=BusFault,I#6=UsageFault," \
    "I#11=SVCall,I#12=DebugMonitor,I#14=PendSV,I#15=SysTick," \
    "I#16=POWER_CLOCK,I#17=RADIO,I#18=UARTE0_UART0,I#19=SPIx0_TWIx0," \
    "I#20=SPIx1_TWIx1,I#21=NFCT,I#22=GPIOTE,I#23=SAADC," \
    "I#24=TIMER0,I#25=TIMER1,I#26=TIMER2,I#27=RTC0,I#28=TEMP,I#29=RNG,I#30=ECB," \
    "I#31=CCM_AAR,I#32=WDT,I#33=RTC1,I#34=QDEC,I#35=COMP_LPCOMP,I#36=SWI0_EGU0," \
    "I#37=SWI1_EGU1,I#38=SWI2_EGU2,I#39=SWI3_EGU3,I#40=SWI4_EGU4,I#41=SWI5_EGU5," \
    "I#42=TIMER3,I#43=TIMER4,I#44=PWM0,I#45=PDM,I#48=MWU,I#49=PWM1,I#50=PWM2," \
    "I#51=SPIx2,I#52=RTC2,I#53=I2S,I#54=FPU"

#elif NRF52840_XXAA

#define MCU_SYSVIEW_INTERRUPTS \
    "I#1=Reset,I#2=MNI,I#3=HardFault,I#4=MemoryMgmt,I#5=BusFault,I#6=UsageFault," \
    "I#11=SVCall,I#12=DebugMonitor,I#14=PendSV,I#15=SysTick," \
    "I#16=POWER_CLOCK,I#17=RADIO,I#18=UARTE0_UART0,I#19=SPIx0_TWIx0," \
    "I#20=SPIx1_TWIx1,I#21=NFCT,I#22=GPIOTE,I#23=SAADC," \
    "I#24=TIMER0,I#25=TIMER1,I#26=TIMER2,I#27=RTC0,I#28=TEMP,I#29=RNG,I#30=ECB," \
    "I#31=CCM_AAR,I#32=WDT,I#33=RTC1,I#34=QDEC,I#35=COMP_LPCOMP,I#36=SWI0_EGU0," \
    "I#37=SWI1_EGU1,I#38=SWI2_EGU2,I#39=SWI3_EGU3,I#40=SWI4_EGU4,I#41=SWI5_EGU5," \
    "I#42=TIMER3,I#43=TIMER4,I#44=PWM0,I#45=PDM,I#48=MWU,I#49=PWM1,I#50=PWM2," \
    "I#51=SPIx2,I#52=RTC2,I#53=I2S,I#54=FPU,I#55=USBD," \
    "I#56=UARTE1,I#57=QSPI,I#58=CRYPTOCELL,I#61=PWM3,I#63=SPIM3"

#endif

#ifdef __cplusplus
}
#endif

#endif /* __MCU_MCU_H_ */
