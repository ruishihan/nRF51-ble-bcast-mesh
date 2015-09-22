/***********************************************************************************
Copyright (c) Nordic Semiconductor ASA
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright notice, this
  list of conditions and the following disclaimer in the documentation and/or
  other materials provided with the distribution.

  3. Neither the name of Nordic Semiconductor ASA nor the names of other
  contributors to this software may be used to endorse or promote products
  derived from this software without specific prior written permission.

  4. This software must only be used in a processor manufactured by Nordic
  Semiconductor ASA, or in a processor manufactured by a third party that
  is used in combination with a processor manufactured by Nordic Semiconductor.


THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
************************************************************************************/

#include "rbc_mesh.h"
#include "cmd_if.h"
#include "timeslot_handler.h"

#include "softdevice_handler.h"
#include "app_error.h"
#include "nrf_gpio.h"
#include "SEGGER_RTT.h"
#include "boards.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#define MESH_ACCESS_ADDR        (0xA541A68F)
#define MESH_INTERVAL_MIN_MS    (100)
#define MESH_CHANNEL            (38)
#define MESH_HANDLE_COUNT       (20)

#define INVALID_HANDLE          (0xFF)

static uint8_t m_handle = INVALID_HANDLE;
static uint8_t m_data[MAX_VALUE_LENGTH];

extern void UART0_IRQHandler(void);

static void print_usage(void)
{
    _LOG("To configure: transmit the handle number this device responds to, \r\n"
    "or 0 to respond to all handles. MAX: %d\r\n", MESH_HANDLE_COUNT);
}

/** 
* @brief Handle an incoming command, and act accordingly.
*/
static void cmd_rx(uint8_t* cmd, uint32_t len)
{
    if (len <= 1)
        return;
    m_handle = atoi((char*) cmd);
    if (m_handle > MESH_HANDLE_COUNT)
    {
        _LOG("OUT OF BOUNDS!\r\n");
        print_usage();
    }
    else if (m_handle == 0)
    {
        m_data[6]++;
        for (uint32_t i = 0; i < MESH_HANDLE_COUNT; ++i)
        {
            rbc_mesh_value_set(i + 1, m_data, MAX_VALUE_LENGTH);
        }
        _LOG("Responding to all\r\n");
    }
    else
    {
        m_data[6]++;
        rbc_mesh_value_set(m_handle, m_data, MAX_VALUE_LENGTH);
        _LOG("Responding to handle %d\r\n", (int) m_handle);
    }
}
/**
* @brief General error handler.
*/
static void error_loop(void)
{
    while (1)
    {
        UART0_IRQHandler();
    }
}

/**
* @brief Softdevice crash handler, never returns
* 
* @param[in] pc Program counter at which the assert failed
* @param[in] line_num Line where the error check failed 
* @param[in] p_file_name File where the error check failed
*/
void sd_assert_handler(uint32_t pc, uint16_t line_num, const uint8_t* p_file_name)
{
    _LOG("SD ERROR: %s:L%d\r\n", (const char*) p_file_name, (int) line_num);
    error_loop();
}

/**
* @brief App error handle callback. Called whenever an APP_ERROR_CHECK() fails.
*   Never returns.
* 
* @param[in] error_code The error code sent to APP_ERROR_CHECK()
* @param[in] line_num Line where the error check failed 
* @param[in] p_file_name File where the error check failed
*/
void app_error_handler(uint32_t error_code, uint32_t line_num, const uint8_t * p_file_name)
{
    _LOG("APP ERROR: %s:L%d - E:%X\r\n", p_file_name, (int) line_num, (int) error_code);
    error_loop();
}

void HardFault_Handler(void)
{
    _LOG("HARDFAULT\r\n");
    error_loop();
}

/**
* @brief Softdevice event handler 
*/
uint32_t sd_evt_handler(void)
{
    rbc_mesh_sd_irq_handler();
    return NRF_SUCCESS;
}

/**
* @brief RBC_MESH framework event handler. Defined in rbc_mesh.h. Handles
*   events coming from the mesh. Propagates the event to the host via UART or RTT.
*
* @param[in] evt RBC event propagated from framework
*/
void rbc_mesh_event_handler(rbc_mesh_event_t* evt)
{ 
    static const char cmd[] = {'U', 'C', 'N', 'I', 'T'};
    switch (evt->event_type)
    {
        case RBC_MESH_EVENT_TYPE_CONFLICTING_VAL:
        case RBC_MESH_EVENT_TYPE_UPDATE_VAL:
        case RBC_MESH_EVENT_TYPE_NEW_VAL:  
            if (evt->value_handle == m_handle || m_handle == 0)
            {
                nrf_gpio_pin_toggle(LED_START);
                m_data[6]++;
                rbc_mesh_value_set(evt->value_handle, m_data, MAX_VALUE_LENGTH);
                if (m_handle == 0)
                    _LOG("%c[%d] \r\n", cmd[evt->event_type], evt->value_handle);
            }
            else
            {
                rbc_mesh_value_disable(evt->value_handle);
            }
            break;
        case RBC_MESH_EVENT_TYPE_INITIALIZED: break;
        case RBC_MESH_EVENT_TYPE_TX: break;
    }
}

/** @brief main function */
int main(void)
{   
    /* Enable Softdevice (including sd_ble before framework */
    SOFTDEVICE_HANDLER_INIT(NRF_CLOCK_LFCLKSRC_XTAL_75_PPM, sd_evt_handler);
    
    /* Init the rbc_mesh */
    rbc_mesh_init_params_t init_params;

    init_params.access_addr = MESH_ACCESS_ADDR;
    init_params.interval_min_ms = MESH_INTERVAL_MIN_MS;
    init_params.channel = MESH_CHANNEL;
    init_params.handle_count = MESH_HANDLE_COUNT;
    init_params.packet_format = RBC_MESH_PACKET_FORMAT_ORIGINAL;
    init_params.radio_mode = RBC_MESH_RADIO_MODE_BLE_1MBIT;
   
    uint32_t error_code = rbc_mesh_init(init_params);
    APP_ERROR_CHECK(error_code);
    
    ble_gap_addr_t addr;
    sd_ble_gap_address_get(&addr);
    memcpy(m_data, addr.addr, 6);
    
    nrf_gpio_range_cfg_output(0, 32);
    for (uint32_t i = LED_START; i <= LED_STOP; ++i)
    {
        nrf_gpio_pin_set(i);
    }
    
    cmd_init(cmd_rx);

    _LOG("START\r\n");
    print_usage();
    
    while (true)
    {
        sd_app_evt_wait();
    }
}
