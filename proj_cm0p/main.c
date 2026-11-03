/***************************************************************************//**
* \file main.c
* \version 1.0
*
* \brief    Implements listening application running on Cortex-M0+ core on
* the FX20 device. SCB1-UART is used for logging.
*
* See \ref README.md ["README.md"]
*
*
*******************************************************************************
* \copyright
* (c) (2026), Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.
*
* SPDX-License-Identifier: Apache-2.0
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#include "cy_pdl.h"
#include "cybsp.h"
#include <string.h>
#include "cy_debug.h"
#include "../app_version.h"
#include "cycfg.h"
#include "cy_fx_common.h"
#include "ipc_communication.h"

/* Select SCB interface used for UART based logging. */
#define LOGGING_SCB             (SCB1)
#define LOGGING_SCB_IDX         (1)
#define DEBUG_LEVEL             (3u)

/* Debug log related initilization */
/* RAM buffer used to hold debug log data. */
#define LOGBUF_SIZE (1024u)
uint8_t logBuff[LOGBUF_SIZE];

cy_stc_debug_config_t dbgCfg = {
    .pBuffer         = logBuff,
    .traceLvl        = DEBUG_LEVEL,
    .bufSize         = LOGBUF_SIZE,
    .dbgIntfce       = CY_DEBUG_INTFCE_UART_SCB1,
    .printNow        = true
};

/* Context structure for the debug logging UART module. */
static cy_stc_scb_uart_context_t glDbgUartCtxt;

/* IPC command code received from Cortex-M4 */
static volatile uint8_t msg_cmd = 0;

/* IPC structure to be sent to Cortex-M4 */
static ipc_msg_t ipc_msg = {
    .client_id  = IPC_CM0_TO_CM4_CLIENT_ID,
    .cpu_status = 0,
    .intr_mask  = USER_IPC_PIPE_INTR_MASK,
    .cmd        = IPC_CMD_STATUS,
    .value      = 0
};

/*******************************************************************************
* Function Prototypes
********************************************************************************/

/**
 * \name Cy_PrintVersionInfo
 * \brief Function to print version information to UART console.
 * \param type Type of version string.
 * \param version Version number including major, minor, patch and build number.
 * \retval None
 */
void Cy_PrintVersionInfo(const char *type, uint32_t version)
{
    char tString[32];
    uint16_t vBuild;
    uint8_t vMajor, vMinor, vPatch;
    uint8_t typeLen = strlen(type);

    vMajor = (version >> 28U);
    vMinor = ((version >> 24U) & 0x0FU);
    vPatch = ((version >> 16U) & 0xFFU);
    vBuild = (uint16_t)(version & 0xFFFFUL);

    memcpy(tString, type, typeLen);
    tString[typeLen++] = '0' + (vMajor / 10);
    tString[typeLen++] = '0' + (vMajor % 10);
    tString[typeLen++] = '.';
    tString[typeLen++] = '0' + (vMinor / 10);
    tString[typeLen++] = '0' + (vMinor % 10);
    tString[typeLen++] = '.';
    tString[typeLen++] = '0' + (vPatch / 10);
    tString[typeLen++] = '0' + (vPatch % 10);
    tString[typeLen++] = '.';
    tString[typeLen++] = '0' + (vBuild / 1000);
    tString[typeLen++] = '0' + ((vBuild % 1000) / 100);
    tString[typeLen++] = '0' + ((vBuild % 100) / 10);
    tString[typeLen++] = '0' + (vBuild % 10);
    tString[typeLen++] = '\r';
    tString[typeLen++] = '\n';
    tString[typeLen] = 0;

    DBG_APP_INFO("%s", tString);
}

/**
 * \name mcwdt_handler
 * \brief Interrupt handler for the MCWDT counter 0 interrupt.
 */
static void mcwdt_handler (void)
{
    uint32_t mcwdtIsrMask;

    /* Get the Watchdog Interrupt Status */
    mcwdtIsrMask = Cy_MCWDT_GetInterruptStatus(DelayTimer_HW);

    if ((mcwdtIsrMask & CY_MCWDT_CTR0) != 0) {
        /* Clear Watchdog Interrupt */
        Cy_MCWDT_ClearInterrupt(DelayTimer_HW, CY_MCWDT_CTR0);

        /* Set the message command to be processed in the main loop */
        msg_cmd = IPC_CMD_STATUS;
    }
}

/**
 * \name StartCm4
 * \brief Function to start the Cortex-M4 core.
 */
static void StartCm4 (void)
{
    /* Start the CM4 with the correct application base address. */
#if BLOAD_ENABLE
    Cy_SysEnableCM4(0x10040000UL);
#else
    Cy_SysEnableCM4(0x10038000UL);
#endif /* BLOAD_ENABLE */
}

/**
 * \name cm0p_msg_callback
 * \brief Callback that handles IPC messages received from Cortex-M4.
 *
 * \param msg Message received.
 */
static void
cm0p_msg_callback (uint32_t *msg)
{
    ipc_msg_t *ipc_recv_msg;

    if (msg != NULL)
    {
        /* Cast the message received to the IPC structure */
        ipc_recv_msg = (ipc_msg_t *) msg;

        /* Extract the command to be processed in the main loop */
        msg_cmd = ipc_recv_msg->cmd;
    }
}

/**
 * \name main
 * \brief Entry to the application.
 * \retval Does not return.
 */
int main(void)
{
    uint32_t random_number;
    cy_en_ipc_pipe_status_t ipc_status;
    cy_en_sysint_status_t sysint_status;
    cy_en_mcwdt_status_t mcwdt_status;
    cy_en_crypto_status_t crypt_status;
    cy_stc_sysint_t intrCfg;

    /* Initialize the PDL driver library and set the clock variables. */
    /* Note: All FX devices, share a common configuration structure. */
    Cy_PDL_Init(&cy_deviceIpBlockCfgFX3G2);

    /* Initialize the device and board peripherals */
    cybsp_init();

    /* Unlock and then disable the watchdog. */
    Cy_WDT_Unlock();
    Cy_WDT_Disable();

    /* Init the IPC communication on Cortex-M0+ */
    setup_ipc_communication_cm0();

    /* Disconnect USBHS interrupt from NvicMux input */
    Cy_SysInt_DisconnectInterruptSource(NvicMux0_IRQn, usbhsdev_interrupt_u2d_active_o_IRQn);

    /* Enable interrupts. */
    __enable_irq();

    /* Initialize SCB for debug log output. */
    Cy_SCB_UART_Init(Cm0LogScb_HW, &Cm0LogScb_config, &glDbgUartCtxt);
    Cy_SCB_UART_Enable(Cm0LogScb_HW);

    /*
     * Initialize the logger module. We are using a blocking print option which will
     * output the messages immediately without buffering.
     */
    Cy_Debug_LogInit(&dbgCfg);
    Cy_SysLib_Delay(500);

    /* Print start-up messages. */
    Cy_Debug_AddToLog(1, "********** FX20: Cortex-M0+ Application ********** \r\n");
    Cy_PrintVersionInfo("APP_VERSION: ", APP_VERSION_NUM);

    /* Register callback to handle messages from Cortex-M4 */
    ipc_status = Cy_IPC_Pipe_RegisterCallback(USER_IPC_PIPE_EP_ADDR, cm0p_msg_callback, IPC_CM4_TO_CM0_CLIENT_ID);
    DBG_APP_INFO("IPC register callback status: %d\r\n", ipc_status);

    /* Start the CM4 core. */
    StartCm4();

    while (1)
    {
        (void)Cy_SysPm_DeepSleep(CY_SYSPM_WAIT_FOR_INTERRUPT);

        /* Process IPC commands */
        switch (msg_cmd)
        {
            case IPC_CMD_INIT:
                DBG_APP_INFO("Init command received\r\n");

                /* Initialize the MCWDT Interrupt */
                intrCfg.cm0pSrc = DelayTimer_IRQ;
                intrCfg.intrSrc = NvicMux1_IRQn;
                intrCfg.intrPriority = 3;

                sysint_status = Cy_SysInt_Init(&intrCfg, mcwdt_handler);
                if (sysint_status != CY_SYSINT_SUCCESS) {
                    DBG_APP_ERR("MCWDT interrupt init failed\r\n");
                    break;
                }

                NVIC_ClearPendingIRQ((IRQn_Type) intrCfg.intrSrc);
                NVIC_EnableIRQ((IRQn_Type) intrCfg.intrSrc);

                /* Init the MCWDT */
                mcwdt_status = Cy_MCWDT_Init(DelayTimer_HW, &DelayTimer_config);
                if (mcwdt_status != CY_MCWDT_SUCCESS) {
                    DBG_APP_ERR("MCWDT init failed\r\n");
                    break;
                }
                Cy_MCWDT_SetInterruptMask(DelayTimer_HW, CY_MCWDT_CTR0);

                /* Keep the Crypto core enabled. */
                crypt_status = Cy_Crypto_Core_Enable(CRYPTO);
                if (crypt_status != CY_CRYPTO_SUCCESS) {
                    DBG_APP_ERR("CRYPTO block init failed\r\n");
                    break;
                }
                break;

            case IPC_CMD_START:
                DBG_APP_INFO("Start command received\r\n");

                /* Start the MCWDT timer. */
                Cy_MCWDT_Enable(DelayTimer_HW, CY_MCWDT_CTR0, 0u);
                break;

            case IPC_CMD_STOP:
                DBG_APP_INFO("Stop command received\r\n");

                /* Stop the MCWDT timer. */
                Cy_MCWDT_Disable(DelayTimer_HW, CY_MCWDT_CTR0, 0u);
                break;

            case IPC_CMD_STATUS:
                DBG_APP_INFO("Status update command received\r\n");

                /* Generate a random number */
                crypt_status = Cy_Crypto_Core_Trng(CRYPTO, 0x04C11DB7UL, 0x04C11DB7UL, 32UL, &random_number);
                if (crypt_status != CY_CRYPTO_SUCCESS) {
                    DBG_APP_INFO("Random number generation failed\r\n");
                    random_number = 0;
                }

                DBG_APP_INFO("Random number=%x\r\n", random_number);

                /* Send the random number to CM4 to be printed */
                ipc_msg.value = random_number;
                ipc_status = Cy_IPC_Pipe_SendMessage(USER_IPC_PIPE_EP_ADDR_CM4,
                        USER_IPC_PIPE_EP_ADDR_CM0,
                        (uint32_t *) &ipc_msg, NULL);
                if (ipc_status != CY_IPC_PIPE_SUCCESS) {
                    DBG_APP_ERR("IPC message send failed\r\n");
                }
                break;

            default:
                break;
        }

        /* Clear command */
        msg_cmd = 0;
    }

    return 0;
}

/**
 * \name Cy_OnResetUser(void)
 * \brief Init function which is executed before the load regions in RAM are updated.
 * \details The High BandWidth subsystem needs to be enable here to allow variables
 *          placed in the High BandWidth SRAM to be updated.
 * \retval None
 */
void Cy_OnResetUser(void)
{
    Cy_UsbFx_OnResetInit();
}

/* [] END OF FILE */
