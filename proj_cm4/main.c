/***************************************************************************//**
* \file main.c
* \version 1.0
*
* Main source file of the CM4 project corresponding to the FX20 Dual-Core
* Application.
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

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "cy_pdl.h"
#include <string.h>
#include "cy_usb_common.h"
#include "cy_usbss_cal_drv.h"
#include "cy_usb_echo_device.h"
#include "cy_usb_usbd.h"
#include "cy_usb_app.h"
#include "cy_debug.h"
#include "cybsp.h"
#include "cy_usbd_version.h"
#include "cy_hbdma_version.h"
#include "../app_version.h"
#include "cy_usb_app_common.h"
#include "cy_fx_common.h"
#include "ipc_communication.h"

/*================================================================================================*/
/*================================================================================================*/

#if BLOAD_ENABLE

/* Section reserved to store the application signature. Size is equal to flash row size. */
__attribute__ ((section(".cy_app_signature"), used)) const uint8_t cyFx2g3AppSignature[512] = {0};

#if defined (__ARMCC_VERSION)
/*******************************************************************************
* Function Name: Cy_AppVerify_MemorySymbols
****************************************************************************//**
*
* The intention of the function is to declare boundaries of the memories for the
* MDK compilers. For the rest of the supported compilers, this is done using
* linker configuration files. The following symbols used by the cymcuelftool.
*
*******************************************************************************/
void Cy_AppVerify_MemorySymbols (void)
{
    __asm (
        ".global __cy_app_verify_start\n"
        ".global __cy_app_verify_length\n"
        ".global __cy_app_signature_addr\n"
        ".equ __cy_app_verify_start, 0x10008000\n"
        ".equ __cy_app_verify_length, 0x00077E00\n"
        ".equ __cy_app_signature_addr, 0x1007FE00\n"
    );
}
#endif /* defined (__ARMCC_VERSION) */

#endif /* BLOAD_ENABLE */

/*================================================================================================*/
/*================================================================================================*/

/* Select SCB interface used for UART based logging. */
#define LOGGING_SCB             (SCB1)
#define LOGGING_SCB_IDX         (1)
#define DEBUG_LEVEL             (3u)

#if DEBUG_INFRA_EN
/* Debug log related initilization */
#define LOGBUF_SIZE (1024u)
uint8_t logBuff[LOGBUF_SIZE];
#if USBFS_LOGS_ENABLE
    cy_stc_debug_config_t dbgCfg = {logBuff, DEBUG_LEVEL, LOGBUF_SIZE, CY_DEBUG_INTFCE_USBFS_CDC, true};
#else
    cy_stc_debug_config_t dbgCfg = {logBuff, DEBUG_LEVEL, LOGBUF_SIZE, CY_DEBUG_INTFCE_UART_SCB1, true};
#endif /* USBFS_LOGS_ENABLE */
TaskHandle_t printLogTaskHandle;
#endif /* DEBUG_INFRA_EN */


/* GPIO to be used as REMOTE-WAKE trigger: PMODE or P13.0 */
#define REMOTEWAKE_RQT_PORT     (P13_0_PORT)
#define REMOTEWAKE_RQT_PIN      (P13_0_PIN)

cy_stc_usbss_cal_ctxt_t ssCalCtxt;
cy_stc_usb_cal_ctxt_t   hsCalCtxt;

/* Global variables associated with High BandWidth DMA setup. */
cy_stc_hbdma_context_t HBW_DrvCtxt;             /* High BandWidth DMA driver context. */
cy_stc_hbdma_dscr_list_t HBW_DscrList;          /* High BandWidth DMA descriptor free list. */
cy_stc_hbdma_buf_mgr_t HBW_BufMgr;              /* High BandWidth DMA buffer manager. */
cy_stc_hbdma_mgr_context_t HBW_MgrCtxt;         /* High BandWidth DMA manager context. */

/* CPU DMA register pointers. */
DMAC_Type *pCpuDmacBase;
DW_Type *pCpuDw0Base, *pCpuDw1Base;

cy_stc_usb_usbd_ctxt_t  usbdCtxt;
cy_stc_usb_app_ctxt_t   appCtxt;


/* USB HS related descriptors */
extern const uint8_t CyFxUSB20DeviceDscr[];
extern const uint8_t CyFxUSBFSConfigDscr[];
extern const uint8_t CyFxUSBHSConfigDscr[];

/* USBSS related descriptors */
extern const uint8_t CyFxUSB30DeviceDscr[];
extern const uint8_t CyFxUSBBOSDscr[];
extern const uint8_t CyFxUSBSSConfigDscr[];

/* Common descriptors shared across speed. */
extern const uint8_t CyFxLangString[];
extern const uint8_t CyFxMfgString[];
extern const uint8_t CyFxProdString[];

extern const uint8_t CyFxDevQualDscr[];
extern const uint8_t CyFxBOSDscr[];

#if FREERTOS_ENABLE
extern void xPortPendSVHandler( void );
extern void xPortSysTickHandler( void );
extern void vPortSVCHandler( void );

/*******************************************************************************
* Function Name: Cy_SysTickIntrWrapper
****************************************************************************//**
*
* Wrapper function to handle the systick interrupt.
*
* \param void
*
* \return void
*
*******************************************************************************/

void Cy_SysTickIntrWrapper (void)
{
    Cy_USBD_TickIncrement(&usbdCtxt);
    xPortSysTickHandler();
}

/*******************************************************************************
* Function Name: vPortSetupTimerInterrupt
****************************************************************************//**
* Initialise the systick timer to generate tick events for the FreeRTOS scheduler
*
* \param  void
*
* \return void
*
*******************************************************************************/

void vPortSetupTimerInterrupt( void )
{
    /* Register the exception vectors. */
    Cy_SysInt_SetVector(PendSV_IRQn, xPortPendSVHandler);
    Cy_SysInt_SetVector(SVCall_IRQn, vPortSVCHandler);
    Cy_SysInt_SetVector(SysTick_IRQn, Cy_SysTickIntrWrapper);

    /* Start the SysTick timer with a period of 1 ms. */
    Cy_SysTick_SetClockSource (CY_SYSTICK_CLOCK_SOURCE_CLK_CPU);
    Cy_SysTick_SetReload(Cy_SysClk_ClkFastGetFrequency() / 1000U);
    Cy_SysTick_Clear ();
    Cy_SysTick_Enable ();
}

#if DEBUG_INFRA_EN
/*******************************************************************************
* Function Name: Cy_PrintTaskHandler
****************************************************************************//**
*
* Task to handle display of debug log messages
*
* \param pTaskParam
*  Parameter passed to the task
*
* \return void
*
*******************************************************************************/

void Cy_PrintTaskHandler(void *pTaskParam)
{
    while(1)
    {
        /* Print any pending logs to the output console. */
        Cy_Debug_PrintLog();

        /* Put the thread to sleep for 5 ms */
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
#endif /* DEBUG_INFRA_EN */
#endif /* FREERTOS_ENABLE */


/*****************************************************************************
 * Function Name: Cy_USB_HS_ISR
 ******************************************************************************
 * Summary:
 *  Handler for USB-HS Interrupts.
 *
 * Parameters:
 *  None
 *
 * Return:
 *  None
 *****************************************************************************/
void Cy_USB_HS_ISR(void)
{
    if (Cy_USBHS_Cal_IntrHandler(&hsCalCtxt))
    {
        portYIELD_FROM_ISR(true);
    }
}

/*****************************************************************************
 * Function Name: Cy_USB_SS_ISR
 ******************************************************************************
 * Summary:
 *  Handler for USB-HS Interrupts.
 *
 * Parameters:
 *  None
 *
 * Return:
 *  None
 *****************************************************************************/
void Cy_USB_SS_ISR(void)
{
    /* Call the USB32DEV interrupt handler. */
    Cy_USBSS_Cal_IntrHandler(&ssCalCtxt);
    
#if FREERTOS_ENABLE
    portYIELD_FROM_ISR(true);
#endif /* FREERTOS_ENABLE */
}

void Cy_USB_IngressDma_ISR(void)
{
    /* Call the HBDMA interrupt handler with the appropriate adapter ID. */
    Cy_HBDma_HandleInterrupts(&HBW_DrvCtxt, CY_HBDMA_ADAP_USB_IN);
#if FREERTOS_ENABLE
    portYIELD_FROM_ISR(true);
#endif /* FREERTOS_ENABLE */
}

void Cy_USB_EgressDma_ISR(void)
{
    /* Call the HBDMA interrupt handler with the appropriate adapter ID. */
    Cy_HBDma_HandleInterrupts(&HBW_DrvCtxt, CY_HBDMA_ADAP_USB_EG);
#if FREERTOS_ENABLE
    portYIELD_FROM_ISR(true);
#endif /* FREERTOS_ENABLE */
}

/*****************************************************************************
 * Function Name: Cy_PrintVersionInfo
 ******************************************************************************
 * Summary:
 *  Function to print version information to UART console.
 *
 * Parameters:
 *  type: Type of version string.
 *  version: Version number including major, minor, patch and build number.
 *
 * Return:
 *  None
 *****************************************************************************/
void Cy_PrintVersionInfo (const char *type, uint32_t version)
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
    tString[typeLen]   = 0;

    Cy_Debug_AddToLog(1,"%s", tString);
}

/*****************************************************************************
 * Function Name: VbusDetGpio_ISR
 *****************************************************************************
 * Summary
 *  Interrupt handler for the Vbus detect GPIO transition detection.
 *
 * Parameters:
 *  None
 *
 * Return:
 *  void
 ****************************************************************************/
static void VbusDetGpio_ISR(void)
{
    cy_stc_usb_app_ctxt_t *pAppCtxt = &appCtxt;
    cy_stc_usbd_app_msg_t xMsg;
#if FREERTOS_ENABLE
    BaseType_t xHigherPriorityTaskWoken;
#endif /* FREERTOS_ENABLE */
    uint32_t gpio_state = Cy_GPIO_Read(VBUS_DETECT_GPIO_PORT, VBUS_DETECT_GPIO_PIN);

    /* Clear the GPIO interrupt. */
    Cy_GPIO_ClearInterrupt(VBUS_DETECT_GPIO_PORT, VBUS_DETECT_GPIO_PIN);

    if (gpio_state != 0) {
        if (pAppCtxt->vbusPresent == false) {
            DBG_APP_INFO("VBUS presence detected\r\n");
            pAppCtxt->vbusPresent = true;

            xMsg.type = CY_USB_VBUS_DETECT_PRESENT;
#if FREERTOS_ENABLE
            xQueueSendFromISR(pAppCtxt->xQueue, &xMsg, &xHigherPriorityTaskWoken);
#else
            Cy_USB_EchoDeviceTaskHandler(pAppCtxt, &xMsg);
#endif /* FREERTOS_ENABLE */
        } else {
            DBG_APP_INFO("Spurious GPIO INT - 1\r\n");
        }
    } else {
        if (pAppCtxt->vbusPresent != false) {
            DBG_APP_INFO("VBUS absence detected\r\n");
            pAppCtxt->vbusPresent = false;

            xMsg.type = CY_USB_VBUS_DETECT_ABSENT;
#if FREERTOS_ENABLE
            xQueueSendFromISR(pAppCtxt->xQueue, &xMsg, &xHigherPriorityTaskWoken);
#else
            Cy_USB_EchoDeviceTaskHandler(pAppCtxt, &xMsg);
#endif /* FREERTOS_ENABLE */
        } else {
            DBG_APP_INFO("Spurious GPIO INT - 0\r\n");
        }
    }
}

/*****************************************************************************
 * Function Name: Cy_USB_RemoteWakeGpio_ISR
 *****************************************************************************
 * Summary
 *  Interrupt handler for the remote wake-up trigger GPIO.
 *
 * Parameters:
 *  None
 *
 * Return:
 *  void
 ****************************************************************************/
static void Cy_USB_RemoteWakeGpio_ISR(void)
{
    uint32_t gpio_state = Cy_GPIO_Read(REMOTEWAKE_RQT_PORT, REMOTEWAKE_RQT_PIN);

    /* Clear the GPIO interrupt. */
    Cy_GPIO_ClearInterrupt(REMOTEWAKE_RQT_PORT, REMOTEWAKE_RQT_PIN);

    if (gpio_state != 0) {
    DBG_APP_INFO("Remote wake request detected\r\n");
    /* Duration of signal will be taken care by HS_CAL */
    Cy_USBD_SignalRemoteWakeup(&usbdCtxt, true);
    }
}
/*******************************************************************************
 * Function name: Cy_Fx3g2_InitPeripheralClocks
 ****************************************************************************//**
 *
 * Function used to enable clocks to different peripherals on the FX10/FX20 device.
 *
 * \param adcClkEnable
 * Whether to enable clock to the ADC in the USBSS block.
 *
 * \param usbfsClkEnable
 * Whether to enable bus reset detect clock input to the USBFS block.
 *
 *******************************************************************************/
void Cy_Fx3g2_InitPeripheralClocks (
        bool adcClkEnable,
        bool usbfsClkEnable)
{
    if (adcClkEnable) {
        /* Divide PERI clock at 75 MHz by 75 to get 1 MHz clock using 16-bit divider #1. */
        Cy_SysClk_PeriphSetDivider(CY_SYSCLK_DIV_16_BIT, 1, 74);
        Cy_SysClk_PeriphEnableDivider(CY_SYSCLK_DIV_16_BIT, 1);
        Cy_SysLib_DelayUs(10U);
        Cy_SysClk_PeriphAssignDivider(PCLK_LVDS2USB32SS_CLOCK_SAR, CY_SYSCLK_DIV_16_BIT, 1);
    }

    if (usbfsClkEnable) {
        /* Divide PERI clock at 75 MHz by 750 to get 100 KHz clock using 16-bit divider #2. */
        Cy_SysClk_PeriphSetDivider(CY_SYSCLK_DIV_16_BIT, 2, 749);
        Cy_SysClk_PeriphEnableDivider(CY_SYSCLK_DIV_16_BIT, 2);
        Cy_SysLib_DelayUs(10U);
        Cy_SysClk_PeriphAssignDivider(PCLK_USB_CLOCK_DEV_BRS, CY_SYSCLK_DIV_16_BIT, 2);
    }
}
/*****************************************************************************
 * Function Name: Cy_USB_USBSSInit
 *****************************************************************************
 * Summary
 *  Initialize USBSS and USBHS block and attempt device enumeration.
 *
 * Parameters:
 *  None
 *
 * Return:
 *  void
 ****************************************************************************/
void Cy_USB_USBSSInit (void)
{
    cy_stc_gpio_pin_config_t pinCfg;
    cy_stc_sysint_t intrCfg;

    memset ((void *)&pinCfg, 0, sizeof(pinCfg));

    /* Configure VBus detect GPIO. */
    pinCfg.driveMode = CY_GPIO_DM_HIGHZ;
    pinCfg.hsiom     = HSIOM_SEL_GPIO;
    pinCfg.intEdge   = CY_GPIO_INTR_BOTH;
    pinCfg.intMask   = 0x01UL;
    Cy_GPIO_Pin_Init(VBUS_DETECT_GPIO_PORT, VBUS_DETECT_GPIO_PIN, &pinCfg);

    /* Register edge detect interrupt for Vbus detect GPIO. */
    intrCfg.intrSrc = VBUS_DETECT_GPIO_INTR;
    intrCfg.intrPriority = 7;
    Cy_SysInt_Init(&intrCfg, VbusDetGpio_ISR);
    NVIC_EnableIRQ(intrCfg.intrSrc);

    /* Configure GPIO pin as interrupt source to trigger remote wakeup. */
    pinCfg.driveMode = CY_GPIO_DM_HIGHZ;
    pinCfg.hsiom     = HSIOM_SEL_GPIO;
    pinCfg.intEdge   = CY_GPIO_INTR_RISING;
    pinCfg.intMask   = 0x01UL;
    Cy_GPIO_Pin_Init(REMOTEWAKE_RQT_PORT, REMOTEWAKE_RQT_PIN, &pinCfg);

    /* Register edge detect interrupt for Vbus detect GPIO. */
    intrCfg.intrSrc = ioss_interrupts_gpio_dpslp_13_IRQn;
    intrCfg.intrPriority = 7;
    Cy_SysInt_Init(&intrCfg, Cy_USB_RemoteWakeGpio_ISR);
    NVIC_EnableIRQ(intrCfg.intrSrc);

    /* Register the ISR and enable the interrupt. */
    intrCfg.intrSrc      = lvds2usb32ss_usb32_int_o_IRQn;
    intrCfg.intrPriority = 4;
    Cy_SysInt_Init(&intrCfg, &Cy_USB_SS_ISR);
    NVIC_EnableIRQ(intrCfg.intrSrc);

    intrCfg.intrSrc      = lvds2usb32ss_usb32_wakeup_int_o_IRQn;
    intrCfg.intrPriority = 5;
    Cy_SysInt_Init(&intrCfg, &Cy_USB_SS_ISR);
    NVIC_EnableIRQ(intrCfg.intrSrc);

    /* ISRs for the USB Ingress and Egress DMA adapters. */
    intrCfg.intrSrc      = lvds2usb32ss_usb32_ingrs_dma_int_o_IRQn;
    intrCfg.intrPriority = 3;
    Cy_SysInt_Init(&intrCfg, &Cy_USB_IngressDma_ISR);
    NVIC_EnableIRQ(intrCfg.intrSrc);

    intrCfg.intrSrc      = lvds2usb32ss_usb32_egrs_dma_int_o_IRQn;
    intrCfg.intrPriority = 3;
    Cy_SysInt_Init(&intrCfg, &Cy_USB_EgressDma_ISR);
    NVIC_EnableIRQ(intrCfg.intrSrc);

    /* Register the ISR for USBHS and enable the interrupt. */
    intrCfg.intrSrc      = usbhsdev_interrupt_u2d_active_o_IRQn;
    intrCfg.intrPriority = 4;
    Cy_SysInt_Init(&intrCfg, Cy_USB_HS_ISR);
    NVIC_EnableIRQ(intrCfg.intrSrc);

    intrCfg.intrSrc = usbhsdev_interrupt_u2d_dpslp_o_IRQn;
    intrCfg.intrPriority = 4;
    Cy_SysInt_Init(&intrCfg, Cy_USB_HS_ISR);
    NVIC_EnableIRQ(usbhsdev_interrupt_u2d_dpslp_o_IRQn);
}

/*****************************************************************************
 * Function Name: OutEpDataWire_ISR
 ******************************************************************************
 * Summary:
 *  Handler for DMA transfer completion on OUT endpoint.
 *
 * Parameters:
 *  None
 *
 * Return:
 *  None
 *****************************************************************************/
void
OutEpDataWire_ISR (void)
{
    /* Pass the interrupt to the DMA manager. */
    Cy_HBDma_Mgr_HandleDW0Interrupt(&HBW_MgrCtxt);
    portYIELD_FROM_ISR(true);
}

/*****************************************************************************
 * Function Name: InEpDataWire_ISR
 ******************************************************************************
 * Summary:
 *  Handler for DMA transfer completion on IN endpoint.
 *
 * Parameters:
 *  None
 *
 * Return:
 *  None
 *****************************************************************************/
void InEpDataWire_ISR (void)
{
    /* Pass the interrupt to the DMA manager. */
    Cy_HBDma_Mgr_HandleDW1Interrupt(&HBW_MgrCtxt);
    portYIELD_FROM_ISR(true);
}

/*
 * Notes on DMA Buffer RAM Usage:
 * 1. The initial part of the buffer RAM is reserved for the descriptors used by the DMA manager.
 *    The space used for this is reserved using the gHbDmaDescriptorSpace array which is placed
 *    in a section named ".hbDmaDescriptor". This array should have a minimum size of 8192 (8 KB)
 *    and has a default size allocation of 16384 bytes (16 KB). No other data should be placed
 *    in this section.
 *
 * 2. The descriptor region is followed by RW data structures which are placed in the ".descSection".
 *    Only data members placed in this section will be initialized during the firmware load
 *    process.
 *
 * 3. The ".descSection" is followed by the ".hbBufSection" which will hold data structures
 *    which do not need to be explicitly initialized (equivalent of ".bss" section).
 *
 * 4. This is followed by the ".hbDmaBufferHeap" section which will be used to allocate all
 *    the DMA buffers from. The gHbDmaBufferHeap array represents the memory region which will
 *    be given to the DMA buffer manager to allocate buffers from and can be sized based on the
 *    available memory. No other data or variables should be placed in this section.
 *
 * Any pre-initialized data which is to be placed in the High BandWidth Buffer RAM should be
 * added to the ".descSection". Any non-initialized data which is to be placed in the High
 * BandWidth Buffer RAM should be added to the ".hbBufSection".
 */

/* Region of 16 KB reserved for High BandWidth DMA descriptors. */
static __attribute__ ((section(".hbDmaDescriptor"), used)) uint32_t gHbDmaDescriptorSpace[16384 / 4];

/* Region of 448 KB reserved for DMA buffer heap. */
static __attribute__ ((section(".hbDmaBufferHeap"), used)) uint32_t gHbDmaBufferHeap[448 * 1024 / 4];

/*****************************************************************************
 * Function Name: Cy_InitHbDma
 ******************************************************************************
 * Summary:
 *  Initialization of HBW DMA.
 *
 * Parameters:
 *  None
 *
 * Return:
 *  None
 *****************************************************************************/
bool Cy_InitHbDma(void)
{
    cy_en_hbdma_status_t      drvstat;
    cy_en_hbdma_mgr_status_t  mgrstat;

    /* Initialize the HBW DMA driver layer. */
    drvstat = Cy_HBDma_Init(LVDSSS_LVDS, USB32DEV, &HBW_DrvCtxt, 0, 0);
    if (drvstat != CY_HBDMA_SUCCESS) {
        return false;
    }

    /* Verify that gHbDmaDescriptorSpace is located at the base of the DMA buffer SRAM. */
    if ((uint32_t)gHbDmaDescriptorSpace != CY_HBW_SRAM_BASE_ADDR) {
        DBG_APP_ERR("High BandWidth DMA descriptors not placed at the correct address\r\n");
        return false;
    }

    /* Setup a HBW DMA descriptor list using the space reserved in gHbDmaDescriptorSpace. */
    mgrstat = Cy_HBDma_DscrList_Create(&HBW_DscrList, sizeof(gHbDmaDescriptorSpace) / 16);
    if (mgrstat != CY_HBDMA_MGR_SUCCESS) {
        return false;
    }

    /* Initialize the DMA buffer manager to use the gHbDmaBufferHeap region. */
    mgrstat = Cy_HBDma_BufMgr_Create(&HBW_BufMgr, (uint32_t *)gHbDmaBufferHeap, sizeof(gHbDmaBufferHeap));
    if (mgrstat != CY_HBDMA_MGR_SUCCESS) {
       return false;
    }

    /* Initialize the HBW DMA channel manager. */
    mgrstat = Cy_HBDma_Mgr_Init(&HBW_MgrCtxt, &HBW_DrvCtxt, &HBW_DscrList, &HBW_BufMgr);
    if (mgrstat != CY_HBDMA_MGR_SUCCESS) {
        return false;
    }

    /* Register the USB stack context with the HBW DMA manager. */
    Cy_HBDma_Mgr_RegisterUsbContext(&HBW_MgrCtxt, &usbdCtxt);

    return true;
}

/*****************************************************************************
* Function Name: Cy_USBSS_DeInit
******************************************************************************
* Summary:
*  Temporary function to reset the USB block and GTX PHY to ensure device
*  disconnects from the host.
*
* Parameters:
*  cy_stc_usbss_cal_ctxt_t *pCalCtxt: Pointer to USB-SS CAL context.

* Return:
*  None
*****************************************************************************/
void
Cy_USBSS_DeInit (cy_stc_usbss_cal_ctxt_t *pCalCtxt)
{

    USB32DEV_Type *base = pCalCtxt->regBase;
    USB32DEV_MAIN_Type  *USB32DEV_MAIN = &base->USB32DEV_MAIN;

    /* Disable the clock for USB3.2 function */
    USB32DEV_MAIN->CTRL &= ~USB32DEV_MAIN_CTRL_CLK_EN_Msk;

    /* Disable PHYSS */
    base->USB32DEV_PHYSS.USB40PHY[1].USB40PHY_TOP.TOP_CTRL_0 &=
                    ~(USB32DEV_PHYSS_USB40PHY_TOP_CTRL_0_REG_PWR_GOOD_CORE_RX_Msk |
                     USB32DEV_PHYSS_USB40PHY_TOP_CTRL_0_REG_PWR_GOOD_CORE_PLL_Msk |
                     USB32DEV_PHYSS_USB40PHY_TOP_CTRL_0_REG_VBUS_Msk |
                     USB32DEV_PHYSS_USB40PHY_TOP_CTRL_0_REG_PHYSS_EN_Msk |
                     USB32DEV_PHYSS_USB40PHY_TOP_CTRL_0_PCLK_EN_Msk);

    base->USB32DEV_PHYSS.USB40PHY[0].USB40PHY_TOP.TOP_CTRL_0 &=
        ~(USB32DEV_PHYSS_USB40PHY_TOP_CTRL_0_REG_PWR_GOOD_CORE_RX_Msk |
                USB32DEV_PHYSS_USB40PHY_TOP_CTRL_0_REG_PWR_GOOD_CORE_PLL_Msk |
                USB32DEV_PHYSS_USB40PHY_TOP_CTRL_0_REG_VBUS_Msk |
                USB32DEV_PHYSS_USB40PHY_TOP_CTRL_0_REG_PHYSS_EN_Msk |
                USB32DEV_PHYSS_USB40PHY_TOP_CTRL_0_PCLK_EN_Msk);

    /* Disable the SuperSpeed Device function */
    USB32DEV_MAIN->CTRL &= ~USB32DEV_MAIN_CTRL_SSDEV_ENABLE_Msk;
}

/*****************************************************************************
* Function Name: Cy_USB_EnableUsbBlock
******************************************************************************
* Summary:
*  Function to enable the USB32DEV IP block before enabling a new USB
*  connection.
*
* Parameters:
*  None

* Return:
*  None
*****************************************************************************/
void Cy_USB_EnableUsbBlock (void)
{
    /* Enable the USB DMA adapters and respective interrupts. */
    Cy_HBDma_Init(NULL, USB32DEV, &HBW_DrvCtxt, 0, 0);

    NVIC_EnableIRQ(lvds2usb32ss_usb32_ingrs_dma_int_o_IRQn);
    NVIC_EnableIRQ(lvds2usb32ss_usb32_egrs_dma_int_o_IRQn);

    /* Make sure to enable USB32DEV IP first. */
    USB32DEV->USB32DEV_MAIN.CTRL |= USB32DEV_MAIN_CTRL_IP_ENABLED_Msk;
}

/*****************************************************************************
* Function Name: Cy_USB_DisableUsbBlock
******************************************************************************
* Summary:
*  Function to disable the USB32DEV IP block after terminating current
*  connection.
*
* Parameters:
*  None

* Return:
*  None
*****************************************************************************/
static void Cy_USB_DisableUsbBlock (void)
{
    /* Disable the USB32DEV IP. */
    USB32DEV->USB32DEV_MAIN.CTRL &= ~USB32DEV_MAIN_CTRL_IP_ENABLED_Msk;

    /* Disable HBDMA adapter interrupts and the adapter itself. */
    NVIC_DisableIRQ(lvds2usb32ss_usb32_ingrs_dma_int_o_IRQn);
    NVIC_DisableIRQ(lvds2usb32ss_usb32_egrs_dma_int_o_IRQn);
    Cy_HBDma_DeInit(&HBW_DrvCtxt);
    DBG_APP_INFO("ADP DISABLE\r\n");
}


/*****************************************************************************
* Function Name: UsbConnectionEnable
******************************************************************************
* Summary:
*  Function used to enable USB 3.x connection.
*
* Parameters:
*  cy_stc_usb_app_ctxt_t *pAppCtxt: Application context structure.

* Return:
*  void
*****************************************************************************/
bool UsbConnectionEnable (cy_stc_usb_app_ctxt_t *pAppCtxt)
{
    DBG_APP_INFO("UsbConnectionEnable >>\r\n");

    Cy_USBD_ConnectDevice(pAppCtxt->pUsbdCtxt, USB_CONN_TYPE);

    pAppCtxt->usbConnectDone = true;
    DBG_APP_INFO("UsbConnectionEnable <<\r\n");
    return true;
}

/*****************************************************************************
* Function Name: UsbConnectionDisable
******************************************************************************
* Summary:
*  Function which disables the USB 3.x connection.
*
* Parameters:
*  cy_stc_usb_app_ctxt_t *pAppCtxt: Application context structure pointer.

* Return:
*  void
*****************************************************************************/
void UsbConnectionDisable (cy_stc_usb_app_ctxt_t *pAppCtxt)
{
    Cy_USB_AppHbDmaDestroyEndpDmaSetAll(pAppCtxt);
    Cy_USBD_DisconnectDevice(pAppCtxt->pUsbdCtxt);
    Cy_USBSS_DeInit(pAppCtxt->pUsbdCtxt->pSsCalCtxt);
    pAppCtxt->usbConnectDone = false;
    pAppCtxt->devState = CY_USB_DEVICE_STATE_DISABLE;
    Cy_USB_DisableUsbBlock();
}


/*****************************************************************************
* Function Name: lightDisable
******************************************************************************
* Summary:
*  Function which disables the USB 3.x connection.
*
* Parameters:
*  cy_stc_usb_app_ctxt_t *pAppCtxt: Application context structure pointer.

* Return:
*  void
*****************************************************************************/
void lightDisable (cy_stc_usb_app_ctxt_t *pAppCtxt)
{
    Cy_USBD_DisconnectDevice(pAppCtxt->pUsbdCtxt);
    Cy_USBSS_DeInit(pAppCtxt->pUsbdCtxt->pSsCalCtxt);
    pAppCtxt->devState = CY_USB_DEVICE_STATE_DISABLE;
}

/*******************************************************************************
 * Function Name: cm4_msg_callback
 ********************************************************************************
 * Summary:
 *   Callback function to execute when receiving a message from CM0+ to CM4.
 *
 * Parameters:
 *   msg: message received
 *
 *******************************************************************************/
static void cm4_msg_callback(uint32_t *msg)
{
    ipc_msg_t *ipc_recv_msg;
    uint32_t msg_value;

    if (msg != NULL) {
        /* Cast received message to the IPC message structure */
        ipc_recv_msg = (ipc_msg_t *) msg;

        /* Extract the message value */
        msg_value = ipc_recv_msg->value;
        DBG_APP_INFO("Number received from CM0+ is %x\r\n", msg_value);
    }
}

static ipc_msg_t gMsgToCm0 = {
    .client_id  = IPC_CM4_TO_CM0_CLIENT_ID,
    .cpu_status = 0,
    .intr_mask  = USER_IPC_PIPE_INTR_MASK,
    .cmd        = IPC_CMD_INIT,
};

/*******************************************************************************
 * Function Name: Cy_App_SendMsgToCm0
 ********************************************************************************
 * Summary:
 *   Function to send the desired IPC command to the Cortex-M0+
 *
 * Parameters:
 *   msg_code: Type of message to be sent.
 *
 *******************************************************************************/
void Cy_App_SendMsgToCm0 (uint8_t msg_code)
{
    gMsgToCm0.cmd = msg_code;
    Cy_IPC_Pipe_SendMessage(USER_IPC_PIPE_EP_ADDR_CM0, USER_IPC_PIPE_EP_ADDR_CM4, (void *) &gMsgToCm0, 0);
}

extern void Cy_App_MakeConfigDescriptor(void);

/*****************************************************************************
* Function Name: main(void)
******************************************************************************
* Summary:
*  Entry to the application.
*
* Parameters:
*  void

* Return:
*  Does not return.
*****************************************************************************/
int main (void)
{
    cy_en_ipc_pipe_status_t ipc_status;

    /* Initialize the PDL driver library and set the clock variables. */
    Cy_PDL_Init (&cy_deviceIpBlockCfgFX3G2);

    /* Init the IPC communication for Cortex-M4 */
    setup_ipc_communication_cm4();

    /* Do all the relevant clock configuration at start-up. */
    cybsp_init();
    Cy_Fx3g2_InitPeripheralClocks(true, true);

    /* Initialize the PDL and register ISR for USB block. */
    Cy_USB_USBSSInit();

    /* Disable the watchdog timer and reset. */
    Cy_WDT_Unlock();
    Cy_WDT_Disable();

#if CY_CPU_CORTEX_M4
    /*
     * If logging is done through the USBFS port, ISR execution is required in this application.
     * Set BASEPRI value to 0 to ensure all exceptions can run and then enable interrupts.
     */
    __set_BASEPRI(0);
#endif /* CY_CPU_CORTEX_M4 */
    __enable_irq ();
    
#if DEBUG_INFRA_EN

#if !USBFS_LOGS_ENABLE
    /* Initialize the UART for logging. */
    InitUart(LOGGING_SCB_IDX);
#endif /* USBFS_LOGS_ENABLE */

    Cy_Debug_LogInit(&dbgCfg);
    Cy_SysLib_Delay(500);
    Cy_Debug_AddToLog(1, "***** FX20: Cortex-M4 Application *****\r\n");

    /* Print application, USBD stack and HBDMA version information. */
    Cy_PrintVersionInfo("APP_VERSION: ", APP_VERSION_NUM);
    Cy_PrintVersionInfo("USBD_VERSION: ", USBD_VERSION_NUM);
    Cy_PrintVersionInfo("HBDMA_VERSION: ", HBDMA_VERSION_NUM);

    /* Create task for printing logs and check status. */
    xTaskCreate(Cy_PrintTaskHandler, "PrintLogTask", 512, NULL, 5, &printLogTaskHandle);
#endif /* DEBUG_INFRA_EN */

    /* Register the IPC Message Callback */
    ipc_status = Cy_IPC_Pipe_RegisterCallback(USER_IPC_PIPE_EP_ADDR, cm4_msg_callback, IPC_CM0_TO_CM4_CLIENT_ID);
    if (ipc_status != CY_IPC_PIPE_SUCCESS) {
        DBG_APP_ERR("IPC callback registration failed with code: %x\r\n", ipc_status);
    }

    memset((void *)&usbdCtxt, 0, sizeof(cy_stc_usb_usbd_ctxt_t));
    memset((void *)&hsCalCtxt, 0, sizeof(cy_stc_usb_cal_ctxt_t));
    memset((void *)&ssCalCtxt, 0, sizeof(cy_stc_usbss_cal_ctxt_t));
    memset((void *)&appCtxt, 0, sizeof(cy_stc_usb_app_ctxt_t));

    pCpuDmacBase = ((DMAC_Type *)DMAC_BASE);
    pCpuDw0Base  = ((DW_Type *)DW0_BASE);
    pCpuDw1Base  = ((DW_Type *)DW1_BASE);

    /* Store IP base address in CAL context. */
    ssCalCtxt.regBase  = USB32DEV;
    hsCalCtxt.pCalBase = MXS40USBHSDEV_USBHSDEV;
    hsCalCtxt.pPhyBase = MXS40USBHSDEV_USBHSPHY;

    USB32DEV->USB32DEV_MAIN.CTRL |= USB32DEV_MAIN_CTRL_IP_ENABLED_Msk;

    /*
     * Make sure any previous USB connection state is cleared.
     * Give some delay to allow the host to process disconnection.
     */
    Cy_USBSS_DeInit(&ssCalCtxt);
    Cy_SysLib_Delay(1000);

    /* Initialize the HbDma IP and DMA Manager */
    Cy_InitHbDma();
    DBG_APP_INFO("InitHbDma done\r\n");

    /* Initialize the USBD layer */
    Cy_USB_USBD_Init(&appCtxt, &usbdCtxt, pCpuDmacBase, &hsCalCtxt, &ssCalCtxt, &HBW_MgrCtxt);
    DBG_APP_INFO("USBD_Init done\r\n");

    /* Specify that DMA clock should be set to 240 MHz once USB 3.x connection is active. */
    Cy_USBD_SetDmaClkFreq(&usbdCtxt, CY_HBDMA_CLK_240_MHZ);

    /*
     * Routing of debug pins for monitoring:
     * DDFT0          (P11.0): U0
     * DDFT1          (P11.1): U3
     * USB.GPIO_DDFT0 (P9.2) : RX_LFPS from PHY RX
     * USB.GPIO_DDFT1 (P9.3) : TX_LFPS from PHY
     * SIP.GPIO_DDFT0 (P9.4) : Not used
     * SIP.GPIO_DDFT1 (P9.5) : Not used
     */
    Cy_UsbFx_SelectDFTFunctions(CY_FX_DBG_FUNC_USBSS_LTSSM_U0, CY_FX_DBG_FUNC_USBSS_LTSSM_U3,
            CY_FX_DBG_FUNC_USBPHY0_RX_LFPSDET_OUT, CY_FX_DBG_FUNC_USBPHY0_TX_LFPS_EN,
            CY_FX_DBG_FUNC_NONE, CY_FX_DBG_FUNC_NONE);

    DBG_APP_INFO("Calling SetDscr()  \r\n");
    /* USB 2.0 related descriptors. */
    Cy_USBD_SetDscr(&usbdCtxt, CY_USB_SET_HS_DEVICE_DSCR,
            0, (uint8_t *)CyFxUSB20DeviceDscr);
    Cy_USBD_SetDscr(&usbdCtxt, CY_USB_SET_FS_CONFIG_DSCR,
            0, (uint8_t *)CyFxUSBFSConfigDscr);
    Cy_USBD_SetDscr(&usbdCtxt, CY_USB_SET_HS_CONFIG_DSCR,
            0, (uint8_t *)CyFxUSBHSConfigDscr);
     Cy_USBD_SetDscr(&usbdCtxt, CY_USB_SET_DEVICE_QUAL_DSCR,
            0, (uint8_t *)CyFxDevQualDscr);
    Cy_USBD_SetDscr(&usbdCtxt, CY_USB_SET_HS_BOS_DSCR,
            0, (uint8_t *)CyFxBOSDscr);

    /* Create the configuration descriptor with the required number of endpoints. */
    Cy_App_MakeConfigDescriptor();

    /* Register USB descriptors with the stack. */
    Cy_USBD_SetDscr(&usbdCtxt, CY_USB_SET_SS_DEVICE_DSCR, 0, (uint8_t *)CyFxUSB30DeviceDscr);
    Cy_USBD_SetDscr(&usbdCtxt, CY_USB_SET_SS_CONFIG_DSCR, 0, (uint8_t *)CyFxUSBSSConfigDscr);
    Cy_USBD_SetDscr(&usbdCtxt, CY_USB_SET_SS_BOS_DSCR, 0, (uint8_t *)CyFxUSBBOSDscr);
    Cy_USBD_SetDscr(&usbdCtxt, CY_USB_SET_STRING_DSCR, 0, (uint8_t *)CyFxLangString);
    Cy_USBD_SetDscr(&usbdCtxt, CY_USB_SET_STRING_DSCR, 1, (uint8_t *)CyFxMfgString);
    Cy_USBD_SetDscr(&usbdCtxt, CY_USB_SET_STRING_DSCR, 2, (uint8_t *)CyFxProdString);

    /* Clear VBus present status. */
    appCtxt.vbusPresent    = false;
    appCtxt.usbConnectDone = false;

    /* Initialize the application and create echo device thread. */
    Cy_USB_AppInit(&appCtxt, &usbdCtxt, pCpuDmacBase, pCpuDw0Base, pCpuDw1Base, &HBW_MgrCtxt);

#if FREERTOS_ENABLE
    /* Invokes scheduler: Not expected to return. */
    vTaskStartScheduler();
#endif /* FREERTOS_ENABLE */

    while(1)
    {
        Cy_SysLib_Delay(10000);
    }

    return 0;
}

/* [] END OF FILE */
