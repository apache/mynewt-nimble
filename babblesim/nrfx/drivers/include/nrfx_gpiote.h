/*
 * Copyright (c) 2015 - 2020, Nordic Semiconductor ASA
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef NRFX_GPIOTE_H__
#define NRFX_GPIOTE_H__

#include <nrfx.h>
#include <hal/nrf_gpiote.h>
#include <hal/nrf_gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup nrfx_gpiote GPIOTE driver
 * @{
 * @ingroup nrf_gpiote
 * @brief   GPIO Task Event (GPIOTE) peripheral driver.
 */

/** @brief Input pin configuration. */
typedef struct
{
    nrf_gpiote_polarity_t sense;               /**< Transition that triggers the interrupt. */
    nrf_gpio_pin_pull_t   pull;                /**< Pulling mode. */
    bool                  is_watcher      : 1; /**< True when the input pin is tracking an output pin. */
    bool                  hi_accuracy     : 1; /**< True when high accuracy (IN_EVENT) is used. */
    bool                  skip_gpio_setup : 1; /**< Do not change GPIO configuration */
} nrfx_gpiote_in_config_t;

/**
 * @brief Macro for configuring a pin to use a GPIO IN or PORT EVENT to detect low-to-high transition.
 * @details Set hi_accu to true to use IN_EVENT.
 */
#define NRFX_GPIOTE_CONFIG_IN_SENSE_LOTOHI(hi_accu) \
{                                                   \
    .sense = NRF_GPIOTE_POLARITY_LOTOHI,            \
    .pull = NRF_GPIO_PIN_NOPULL,                    \
    .is_watcher = false,                            \
    .hi_accuracy = hi_accu,                         \
    .skip_gpio_setup = false,                       \
}

/**
 * @brief Macro for configuring a pin to use a GPIO IN or PORT EVENT to detect high-to-low transition.
 * @details Set hi_accu to true to use IN_EVENT.
 */
#define NRFX_GPIOTE_CONFIG_IN_SENSE_HITOLO(hi_accu) \
{                                                   \
    .sense = NRF_GPIOTE_POLARITY_HITOLO,            \
    .pull = NRF_GPIO_PIN_NOPULL,                    \
    .is_watcher = false,                            \
    .hi_accuracy = hi_accu,                         \
    .skip_gpio_setup = false,                       \
}

/**
 * @brief Macro for configuring a pin to use a GPIO IN or PORT EVENT to detect any change on the pin.
 * @details Set hi_accu to true to use IN_EVENT.
 */
#define NRFX_GPIOTE_CONFIG_IN_SENSE_TOGGLE(hi_accu) \
{                                                   \
    .sense = NRF_GPIOTE_POLARITY_TOGGLE,            \
    .pull = NRF_GPIO_PIN_NOPULL,                    \
    .is_watcher = false,                            \
    .hi_accuracy = hi_accu,                         \
    .skip_gpio_setup = false,                       \
}

/**
 * @brief Macro for configuring a pin to use a GPIO IN or PORT EVENT to detect low-to-high transition.
 * @details Set hi_accu to true to use IN_EVENT.
 * @note This macro prepares configuration that skips the GPIO setup.
 */
#define NRFX_GPIOTE_RAW_CONFIG_IN_SENSE_LOTOHI(hi_accu) \
{                                                       \
    .sense = NRF_GPIOTE_POLARITY_LOTOHI,                \
    .pull = NRF_GPIO_PIN_NOPULL,                        \
    .is_watcher = false,                                \
    .hi_accuracy = hi_accu,                             \
    .skip_gpio_setup = true,                            \
}

/**
 * @brief Macro for configuring a pin to use a GPIO IN or PORT EVENT to detect high-to-low transition.
 * @details Set hi_accu to true to use IN_EVENT.
 * @note This macro prepares configuration that skips the GPIO setup.
 */
#define NRFX_GPIOTE_RAW_CONFIG_IN_SENSE_HITOLO(hi_accu) \
{                                                       \
    .sense = NRF_GPIOTE_POLARITY_HITOLO,                \
    .pull = NRF_GPIO_PIN_NOPULL,                        \
    .is_watcher = false,                                \
    .hi_accuracy = hi_accu,                             \
    .skip_gpio_setup = true,                            \
}

/**
 * @brief Macro for configuring a pin to use a GPIO IN or PORT EVENT to detect any change on the pin.
 * @details Set hi_accu to true to use IN_EVENT.
 * @note This macro prepares configuration that skips the GPIO setup.
 */
#define NRFX_GPIOTE_RAW_CONFIG_IN_SENSE_TOGGLE(hi_accu) \
{                                                       \
    .sense = NRF_GPIOTE_POLARITY_TOGGLE,                \
    .pull = NRF_GPIO_PIN_NOPULL,                        \
    .is_watcher = false,                                \
    .hi_accuracy = hi_accu,                             \
    .skip_gpio_setup = true,                            \
}


/** @brief Output pin configuration. */
typedef struct
{
    nrf_gpiote_polarity_t action;     /**< Configuration of the pin task. */
    nrf_gpiote_outinit_t  init_state; /**< Initial state of the output pin. */
    bool                  task_pin;   /**< True if the pin is controlled by a GPIOTE task. */
} nrfx_gpiote_out_config_t;

/** @brief Macro for configuring a pin to use as output. GPIOTE is not used for the pin. */
#define NRFX_GPIOTE_CONFIG_OUT_SIMPLE(init_high)                                                \
    {                                                                                           \
        .action     = NRF_GPIOTE_POLARITY_LOTOHI,                                               \
        .init_state = init_high ? NRF_GPIOTE_INITIAL_VALUE_HIGH : NRF_GPIOTE_INITIAL_VALUE_LOW, \
        .task_pin   = false,                                                                    \
    }

/**
 * @brief Macro for configuring a pin to use the GPIO OUT TASK to change the state from high to low.
 * @details The task will clear the pin. Therefore, the pin is set initially.
 */
#define NRFX_GPIOTE_CONFIG_OUT_TASK_LOW              \
    {                                                \
        .action     = NRF_GPIOTE_POLARITY_HITOLO,    \
        .init_state = NRF_GPIOTE_INITIAL_VALUE_HIGH, \
        .task_pin   = true,                          \
    }

/**
 * @brief Macro for configuring a pin to use the GPIO OUT TASK to change the state from low to high.
 * @details The task will set the pin. Therefore, the pin is cleared initially.
 */
#define NRFX_GPIOTE_CONFIG_OUT_TASK_HIGH            \
    {                                               \
        .action     = NRF_GPIOTE_POLARITY_LOTOHI,   \
        .init_state = NRF_GPIOTE_INITIAL_VALUE_LOW, \
        .task_pin   = true,                         \
    }

/**
 * @brief Macro for configuring a pin to use the GPIO OUT TASK to toggle the pin state.
 * @details The initial pin state must be provided.
 */
#define NRFX_GPIOTE_CONFIG_OUT_TASK_TOGGLE(init_high)                                           \
    {                                                                                           \
        .action     = NRF_GPIOTE_POLARITY_TOGGLE,                                               \
        .init_state = init_high ? NRF_GPIOTE_INITIAL_VALUE_HIGH : NRF_GPIOTE_INITIAL_VALUE_LOW, \
        .task_pin   = true,                                                                     \
    }

#if !defined (NRFX_GPIOTE_CHANNELS_USED) || defined(__NRFX_DOXYGEN__)
/** @brief Bitmask that defines GPIOTE channels that are reserved for use outside of the nrfx library. */
#define NRFX_GPIOTE_CHANNELS_USED 0
#endif

#if (GPIOTE_CH_NUM == 4) || defined(__NRFX_DOXYGEN__)
/** @brief Bitfield representing all GPIOTE channels available to the application. */
#define NRFX_GPIOTE_APP_CHANNELS_MASK ((uint32_t)0x0000000F & ~(NRFX_GPIOTE_CHANNELS_USED))
#elif (GPIOTE_CH_NUM == 8)
#define NRFX_GPIOTE_APP_CHANNELS_MASK ((uint32_t)0x000000FF & ~(NRFX_GPIOTE_CHANNELS_USED))
#else
#error Unsupported number of GPIOTE channels.
#endif

/** @brief Pin. */
typedef uint32_t nrfx_gpiote_pin_t;

/**
 * @brief Pin event handler prototype.
 *
 * @param[in] pin    Pin that triggered this event.
 * @param[in] action Action that led to triggering this event.
 */
typedef void (*nrfx_gpiote_evt_handler_t)(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action);

/**
 * @brief Function for initializing the GPIOTE module.
 *
 * @details Only static configuration is supported to prevent the shared
 * resource being customized by the initiator.
 *
 * @param[in] interrupt_priority Interrupt priority.
 *
 * @retval NRFX_SUCCESS             Initialization was successful.
 * @retval NRFX_ERROR_INVALID_STATE The driver was already initialized.
 */
nrfx_err_t nrfx_gpiote_init(uint8_t interrupt_priority);

/**
 * @brief Function for checking if the GPIOTE module is initialized.
 *
 * @details The GPIOTE module is a shared module. Therefore, check if
 * the module is already initialized and skip initialization if it is.
 *
 * @retval true  The module is already initialized.
 * @retval false The module is not initialized.
 */
bool nrfx_gpiote_is_init(void);

/** @brief Function for uninitializing the GPIOTE module. */
void nrfx_gpiote_uninit(void);

/**
 * @brief Function for allocating a GPIOTE channel.
 * @details This function allocates the first unused GPIOTE channel from
 *          pool defined in @ref NRFX_GPIOTE_APP_CHANNELS_MASK.
 *
 * @param[out] p_channel Pointer to the GPIOTE channel that has been allocated.
 *
 * @retval NRFX_SUCCESS      The channel was successfuly allocated.
 * @retval NRFX_ERROR_NO_MEM There is no available channel to be used.
 */
nrfx_err_t nrfx_gpiote_channel_alloc(uint8_t * p_channel);

/**
 * @brief Function for freeing a GPIOTE channel.
 * @details This function frees a GPIOTE channel that was allocated using
 *          @ref nrfx_gpiote_channel_alloc.
 *
 * @param[in] channel GPIOTE channel to be freed.
 *
 * @retval NRFX_SUCCESS             The channel was successfully freed.
 * @retval NRFX_ERROR_INVALID_PARAM The channel is not user-configurable.
 */
nrfx_err_t nrfx_gpiote_channel_free(uint8_t channel);

/**
 * @brief Function for initializing a GPIOTE output pin.
 * @details The output pin can be controlled by the CPU or by PPI. The initial
 * configuration specifies which mode is used. If PPI mode is used, the driver
 * attempts to allocate one of the available GPIOTE channels. If no channel is
 * available, an error is returned.
 *
 * @param[in] pin      Pin.
 * @param[in] p_config Initial configuration.
 *
 * @retval NRFX_SUCCESS      Initialization was successful.
 * @retval NRFX_ERROR_BUSY   The pin is already used.
 * @retval NRFX_ERROR_NO_MEM No GPIOTE channel is available.
 */
nrfx_err_t nrfx_gpiote_out_init(nrfx_gpiote_pin_t                pin,
                                nrfx_gpiote_out_config_t const * p_config);

/**
 * @brief Function for initializing a GPIOTE output pin with preallocated channel.
 * @details The output pin can be controlled by PPI.
 *
 * @param[in] pin      Pin.
 * @param[in] p_config Initial configuration.
 * @param[in] channel  GPIOTE channel allocated with @ref nrfx_gpiote_channel_alloc.
 *
 * @retval NRFX_SUCCESS             Initialization was successful.
 * @retval NRFX_ERROR_BUSY          The pin is already used.
 * @retval NRFX_ERROR_INVALID_PARAM Pin is configured to not be controlled by
                                    the GPIOTE task and cannot be used with
                                    preallocated channel.
                                    Use @ref nrfx_gpiote_out_init instead.
 */
nrfx_err_t nrfx_gpiote_out_prealloc_init(nrfx_gpiote_pin_t                pin,
                                         nrfx_gpiote_out_config_t const * p_config,
                                         uint8_t                          channel);

/**
 * @brief Function for uninitializing a GPIOTE output pin.
 * @details The driver frees the GPIOTE channel if the output pin was using one.
 *
 * @param[in] pin Pin.
 */
void nrfx_gpiote_out_uninit(nrfx_gpiote_pin_t pin);

/**
 * @brief Function for setting a GPIOTE output pin.
 *
 * @param[in] pin Pin.
 */
void nrfx_gpiote_out_set(nrfx_gpiote_pin_t pin);

/**
 * @brief Function for clearing a GPIOTE output pin.
 *
 * @param[in] pin Pin.
 */
void nrfx_gpiote_out_clear(nrfx_gpiote_pin_t pin);

/**
 * @brief Function for toggling a GPIOTE output pin.
 *
 * @param[in] pin Pin.
 */
void nrfx_gpiote_out_toggle(nrfx_gpiote_pin_t pin);

/**
 * @brief Function for enabling a GPIOTE output pin task.
 *
 * @param[in] pin Pin.
 */
void nrfx_gpiote_out_task_enable(nrfx_gpiote_pin_t pin);

/**
 * @brief Function for disabling a GPIOTE output pin task.
 *
 * @param[in] pin Pin.
 */
void nrfx_gpiote_out_task_disable(nrfx_gpiote_pin_t pin);

/**
 * @brief Function for getting the OUT task for the specified output pin.
 *
 * @details The returned task identifier can be used within @ref nrf_gpiote_hal,
 *          for example, to configure a DPPI channel.
 *
 * @param[in] pin Pin.
 *
 * @return OUT task associated with the specified output pin.
 */
nrf_gpiote_task_t nrfx_gpiote_out_task_get(nrfx_gpiote_pin_t pin);

/**
 * @brief Function for getting the address of the OUT task for the specified output pin.
 *
 * @param[in] pin Pin.
 *
 * @return Address of OUT task.
 */
uint32_t nrfx_gpiote_out_task_addr_get(nrfx_gpiote_pin_t pin);

#if defined(GPIOTE_FEATURE_SET_PRESENT) || defined(__NRFX_DOXYGEN__)
/**
 * @brief Function for getting the SET task for the specified output pin.
 *
 * @details The returned task identifier can be used within @ref nrf_gpiote_hal,
 *          for example, to configure a DPPI channel.
 *
 * @param[in] pin Pin.
 *
 * @return SET task associated with the specified output pin.
 */
nrf_gpiote_task_t nrfx_gpiote_set_task_get(nrfx_gpiote_pin_t pin);

/**
 * @brief Function for getting the address of the SET task for the specified output pin.
 *
 * @param[in] pin Pin.
 *
 * @return Address of SET task.
 */
uint32_t nrfx_gpiote_set_task_addr_get(nrfx_gpiote_pin_t pin);
#endif // defined(GPIOTE_FEATURE_SET_PRESENT) || defined(__NRFX_DOXYGEN__)

#if defined(GPIOTE_FEATURE_CLR_PRESENT) || defined(__NRFX_DOXYGEN__)
/**
 * @brief Function for getting the CLR task for the specified output pin.
 *
 * @details The returned task identifier can be used within @ref nrf_gpiote_hal,
 *          for example, to configure a DPPI channel.
 *
 * @param[in] pin Pin.
 *
 * @return CLR task associated with the specified output pin.
 */
nrf_gpiote_task_t nrfx_gpiote_clr_task_get(nrfx_gpiote_pin_t pin);

/**
 * @brief Function for getting the address of the SET task for the specified output pin.
 *
 * @param[in] pin Pin.
 *
 * @return Address of CLR task.
 */
uint32_t nrfx_gpiote_clr_task_addr_get(nrfx_gpiote_pin_t pin);
#endif // defined(GPIOTE_FEATURE_CLR_PRESENT) || defined(__NRFX_DOXYGEN__)

/**
 * @brief Function for initializing a GPIOTE input pin.
 * @details The input pin can act in two ways:
 * - lower accuracy but low power (high frequency clock not needed)
 * - higher accuracy (high frequency clock required)
 *
 * The initial configuration specifies which mode is used.
 * If high-accuracy mode is used, the driver attempts to allocate one
 * of the available GPIOTE channels. If no channel is
 * available, an error is returned.
 * In low accuracy mode SENSE feature is used. In this case, only one active pin
 * can be detected at a time. It can be worked around by setting all of the used
 * low accuracy pins to toggle mode.
 * For more information about SENSE functionality, refer to Product Specification.
 *
 * @param[in] pin         Pin.
 * @param[in] p_config    Initial configuration.
 * @param[in] evt_handler User function to be called when the configured transition occurs.
 *
 * @retval NRFX_SUCCESS      Initialization was successful.
 * @retval NRFX_ERROR_BUSY   The pin is already used.
 * @retval NRFX_ERROR_NO_MEM No GPIOTE channel is available.
 */
nrfx_err_t nrfx_gpiote_in_init(nrfx_gpiote_pin_t               pin,
                               nrfx_gpiote_in_config_t const * p_config,
                               nrfx_gpiote_evt_handler_t       evt_handler);

/**
 * @brief Function for initializing a GPIOTE input pin with preallocated channel.
 * @details The input pin can act in higher accuracy (high frequency clock required)
 * mode.
 *
 * @param[in] pin         Pin.
 * @param[in] p_config    Initial configuration.
 * @param[in] channel     GPIOTE channel allocated with @ref nrfx_gpiote_channel_alloc.
 * @param[in] evt_handler User function to be called when the configured transition occurs.
 *
 * @retval NRFX_SUCCESS             Initialization was successful.
 * @retval NRFX_ERROR_BUSY          The pin is already used.
 * @retval NRFX_ERROR_INVALID_PARAM Pin is configured to not be controlled by
                                    the GPIOTE task and cannot be used with
                                    preallocated channel.
                                    Use @ref nrfx_gpiote_in_init instead.
 */
nrfx_err_t nrfx_gpiote_in_prealloc_init(nrfx_gpiote_pin_t               pin,
                                        nrfx_gpiote_in_config_t const * p_config,
                                        uint8_t                         channel,
                                        nrfx_gpiote_evt_handler_t       evt_handler);
/**
 * @brief Function for uninitializing a GPIOTE input pin.
 * @details The driver frees the GPIOTE channel if the input pin was using one.
 *
 * @param[in] pin Pin.
 */
void nrfx_gpiote_in_uninit(nrfx_gpiote_pin_t pin);

/**
 * @brief Function for enabling sensing of a GPIOTE input pin.
 *
 * @details If the input pin is configured as high-accuracy pin, the function
 * enables an IN_EVENT. Otherwise, the function enables the GPIO sense mechanism.
 * The PORT event is shared between multiple pins, therefore the interrupt is always enabled.
 *
 * @param[in] pin        Pin.
 * @param[in] int_enable True to enable the interrupt. Always valid for a high-accuracy pin.
 */
void nrfx_gpiote_in_event_enable(nrfx_gpiote_pin_t pin, bool int_enable);

/**
 * @brief Function for disabling a GPIOTE input pin.
 *
 * @param[in] pin Pin.
 */
void nrfx_gpiote_in_event_disable(nrfx_gpiote_pin_t pin);

/**
 * @brief Function for checking if a GPIOTE input pin is set.
 *
 * @param[in] pin Pin.
 *
 * @retval true  The input pin is set.
 * @retval false The input pin is not set.
 */
bool nrfx_gpiote_in_is_set(nrfx_gpiote_pin_t pin);

/**
 * @brief Function for getting the GPIOTE event for the specified input pin.
 *
 * @details The returned event identifier can be used within @ref nrf_gpiote_hal,
 *          for example, to configure a DPPI channel.
 *          If the pin is configured to use low-accuracy mode, the PORT event
 *          is returned.
 *
 * @param[in] pin Pin.
 *
 * @return Event associated with the specified input pin.
 */
nrf_gpiote_event_t nrfx_gpiote_in_event_get(nrfx_gpiote_pin_t pin);

/**
 * @brief Function for getting the address of a GPIOTE input pin event.
 * @details If the pin is configured to use low-accuracy mode, the address of the PORT event is returned.
 *
 * @param[in] pin Pin.
 *
 * @return Address of the specified input pin event.
 */
uint32_t nrfx_gpiote_in_event_addr_get(nrfx_gpiote_pin_t pin);

/**
 * @brief Function for forcing a specific state on the pin configured as task.
 *
 * @param[in] pin   Pin.
 * @param[in] state Pin state.
 */
void nrfx_gpiote_out_task_force(nrfx_gpiote_pin_t pin, uint8_t state);

/**
 * @brief Function for triggering the task OUT manually.
 *
 * @param[in] pin Pin.
 */
void nrfx_gpiote_out_task_trigger(nrfx_gpiote_pin_t pin);

#if defined(GPIOTE_FEATURE_SET_PRESENT) || defined(__NRFX_DOXYGEN__)
/**
 * @brief Function for triggering the task SET manually.
 *
 * @param[in] pin Pin.
 */
void nrfx_gpiote_set_task_trigger(nrfx_gpiote_pin_t pin);
#endif // defined(GPIOTE_FEATURE_SET_PRESENT) || defined(__NRFX_DOXYGEN__)

#if defined(GPIOTE_FEATURE_CLR_PRESENT) || defined(__NRFX_DOXYGEN__)
/**
 * @brief Function for triggering the task CLR manually.
 *
 * @param[in] pin Pin.
 */
void nrfx_gpiote_clr_task_trigger(nrfx_gpiote_pin_t pin);
#endif // defined(GPIOTE_FEATURE_CLR_PRESENT) || defined(__NRFX_DOXYGEN__)

#if NRF_GPIOTE_HAS_LATENCY
/**
 * @brief Function for setting the latency setting.
 *
 * @note Available for event mode with rising or falling edge detection on the pin.
 *       Toggle task mode can only be used with low latency setting.
 *
 * @param[in] latency Latency setting to be set.
 */
NRFX_STATIC_INLINE void nrfx_gpiote_latency_set(nrf_gpiote_latency_t latency);

/**
 * @brief Function for retrieving the latency setting.
 *
 * @return Latency setting.
 */
NRFX_STATIC_INLINE nrf_gpiote_latency_t nrfx_gpiote_latency_get(void);
#endif

#ifndef NRFX_DECLARE_ONLY

#if NRF_GPIOTE_HAS_LATENCY
NRFX_STATIC_INLINE void nrfx_gpiote_latency_set(nrf_gpiote_latency_t latency)
{
    nrf_gpiote_latency_set(NRF_GPIOTE, latency);
}

NRFX_STATIC_INLINE nrf_gpiote_latency_t nrfx_gpiote_latency_get(void)
{
    return nrf_gpiote_latency_get(NRF_GPIOTE);
}
#endif // NRF_GPIOTE_HAS_LATENCY

#endif // NRFX_DECLARE_ONLY

/** @} */

void nrfx_gpiote_irq_handler(void);


#ifdef __cplusplus
}
#endif

#endif // NRFX_GPIOTE_H__
