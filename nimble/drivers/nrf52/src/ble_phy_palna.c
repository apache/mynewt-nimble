#include <stdbool.h>
#include "syscfg/syscfg.h"
#include "hal/hal_gpio.h"
#include "nrfx/nrfx.h"
#include "nrfx/hal/nrf_gpiote.h"
#include "controller/ble_phy_palna.h"

#if (MYNEWT_VAL(BLE_PHY_PALNA_PA_ENABLE_PIN) || MYNEWT_VAL(BLE_PHY_PALNA_LNA_ENABLE_PIN))

typedef enum
{
    MODE_SHUTDOWN,
    MODE_TX,
    MODE_RX,
} pa_modes_t;

#define PALNA_PPI_CHANNEL_MASK ((1 << BLE_PHY_PALNA_PPI_CHANNEL_RADIO_READY) | \
                                (1 << BLE_PHY_PALNA_PPI_CHANNEL_RADIO_DISABLED))

static void set_mode(pa_modes_t mode);

void
ble_phy_palna_init(void)
{
    /* disable pa, lna */
    #if MYNEWT_VAL(BLE_PHY_PALNA_PA_ENABLE_PIN)
    hal_gpio_init_out(MYNEWT_VAL(BLE_PHY_PALNA_PA_ENABLE_PIN), !MYNEWT_VAL(BLE_PHY_PALNA_PA_ENABLE_PIN_ACTIVE_LOW));
    #endif

    #if MYNEWT_VAL(BLE_PHY_PALNA_LNA_ENABLE_PIN)
    hal_gpio_init_out(MYNEWT_VAL(BLE_PHY_PALNA_LNA_ENABLE_PIN), !MYNEWT_VAL(BLE_PHY_PALNA_LNA_ENABLE_PIN_ACTIVE_LOW));
    #endif

    /* Setup a PPI Channel for Radio Ready Event to enable PA-LNA */
    NRF_PPI->CH[BLE_PHY_PALNA_PPI_CHANNEL_RADIO_READY].EEP = (uint32_t)&NRF_RADIO->EVENTS_READY;
    NRF_PPI->CH[BLE_PHY_PALNA_PPI_CHANNEL_RADIO_READY].TEP = (uint32_t)&NRF_GPIOTE->TASKS_SET[0];

    /* Setup PPI channel for Radio Disabled Event to disable PA-LNA */
    NRF_PPI->CH[BLE_PHY_PALNA_PPI_CHANNEL_RADIO_DISABLED].EEP = (uint32_t)&NRF_RADIO->EVENTS_DISABLED;
    NRF_PPI->CH[BLE_PHY_PALNA_PPI_CHANNEL_RADIO_DISABLED].TEP = (uint32_t)&NRF_GPIOTE->TASKS_CLR[0];

    /* init gpiote */
    nrf_gpiote_te_default(BLE_PHY_PALNA_GPIOTE_CHANNEL);

    /* off */
    ble_phy_palna_idle();
}

void
ble_phy_palna_tx_prepare(void)
{
    set_mode(MODE_TX);
}

void
ble_phy_palna_rx_prepare(void)
{
    set_mode(MODE_RX);
}

void
ble_phy_palna_idle(void)
{
    set_mode(MODE_SHUTDOWN);
}

static void
set_mode(pa_modes_t mode)
{
    NRF_PPI->CHENCLR = PALNA_PPI_CHANNEL_MASK;               /* disable PPI channels */
    NRF_GPIOTE->TASKS_CLR[BLE_PHY_PALNA_GPIOTE_CHANNEL] = 1; /* ensure current GPIOTE pin is low */

    switch (mode) {
    case MODE_SHUTDOWN:
        nrf_gpiote_te_default(BLE_PHY_PALNA_GPIOTE_CHANNEL);
        break;

    case MODE_TX:
        #if MYNEWT_VAL(BLE_PHY_PALNA_PA_ENABLE_PIN)
        nrf_gpiote_task_configure(
                BLE_PHY_PALNA_GPIOTE_CHANNEL,
                MYNEWT_VAL(BLE_PHY_PALNA_PA_ENABLE_PIN),
                NRF_GPIOTE_POLARITY_TOGGLE,
            (MYNEWT_VAL(BLE_PHY_PALNA_PA_ENABLE_PIN_ACTIVE_LOW) ? NRF_GPIOTE_INITIAL_VALUE_HIGH :
             NRF_GPIOTE_INITIAL_VALUE_LOW)
            );

        nrf_gpiote_task_enable(BLE_PHY_PALNA_GPIOTE_CHANNEL);
        NRF_PPI->CHENSET = PALNA_PPI_CHANNEL_MASK;                      /* re-enable PPI channels 1 and 2 */
        #endif

    case MODE_RX:
        #if MYNEWT_VAL(BLE_PHY_PALNA_LNA_ENABLE_PIN)
        nrf_gpiote_task_configure(
                BLE_PHY_PALNA_GPIOTE_CHANNEL,
                 MYNEWT_VAL(BLE_PHY_PALNA_LNA_ENABLE_PIN),
                NRF_GPIOTE_POLARITY_TOGGLE,
            (MYNEWT_VAL(BLE_PHY_PALNA_LNA_ENABLE_PIN_ACTIVE_LOW) ? NRF_GPIOTE_INITIAL_VALUE_HIGH :
             NRF_GPIOTE_INITIAL_VALUE_LOW)
            );
        nrf_gpiote_task_enable(BLE_PHY_PALNA_GPIOTE_CHANNEL);
        NRF_PPI->CHENSET = PALNA_PPI_CHANNEL_MASK;                      /* re-enable PPI channels 1 and 2 */
        #endif
        break;
    }
}
#endif