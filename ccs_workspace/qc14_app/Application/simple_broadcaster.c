/******************************************************************************

 @file  simple_broadcaster.c

 @brief This file contains the Simple BLE Broadcaster sample application for
        use with the CC2650 Bluetooth Low Energy Protocol Stack.

 Group: WCS, BTS
 Target Device: CC2650, CC2640, CC1350

 ******************************************************************************
 
 Copyright (c) 2011-2016, Texas Instruments Incorporated
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions
 are met:

 *  Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

 *  Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

 *  Neither the name of Texas Instruments Incorporated nor the names of
    its contributors may be used to endorse or promote products derived
    from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 ******************************************************************************
 Release Name: ble_sdk_2_02_01_18
 Release Date: 2016-10-26 15:20:04
 *****************************************************************************/

/*********************************************************************
 * INCLUDES
 */

#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Semaphore.h>
#include <ti/sysbios/knl/Queue.h>

#include "gatt.h"
#include "gapgattserver.h"
#include "gattservapp.h"

#include "broadcaster.h"
#include "gapbondmgr.h"

#include "osal_snv.h"
#include "icall_apimsg.h"

#ifdef USE_RCOSC
#include "rcosc_calibration.h"
#endif //USE_RCOSC

#include "util.h"
#include <ti/mw/display/Display.h>
#include "board.h"

#include "simple_broadcaster.h"

#include "qc14.h"

/*********************************************************************
 * MACROS
 */

/*********************************************************************
 * CONSTANTS
 */

// What is the advertising interval when device is discoverable (units of 625us, 160=100ms)
#define DEFAULT_ADVERTISING_INTERVAL          160

// Task configuration
#define SBB_TASK_PRIORITY                     1

#ifndef SBB_TASK_STACK_SIZE
#define SBB_TASK_STACK_SIZE                   660
#endif

// Internal Events for RTOS application
#define SBB_STATE_CHANGE_EVT                  0x0001
#define SBB_KEY_CHANGE_EVT                    0x0002

/*********************************************************************
 * TYPEDEFS
 */

// App event passed from profiles.
typedef struct
{
  appEvtHdr_t hdr; // Event header.
} sbbEvt_t;

/*********************************************************************
 * GLOBAL VARIABLES
 */

// Display Interface
Display_Handle dispHandle = NULL;

/*********************************************************************
 * EXTERNAL VARIABLES
 */

/*********************************************************************
 * EXTERNAL FUNCTIONS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */

// Entity ID globally used to check for source and/or destination of messages
static ICall_EntityID selfEntity;

// Semaphore globally used to post events to the application thread
//static ICall_Semaphore sem;

// Queue object used for app messages
static Queue_Struct appMsg;
static Queue_Handle appMsgQueue;

// Task configuration
Task_Struct sbbTask;
Char sbbTaskStack[SBB_TASK_STACK_SIZE];

// GAP - SCAN RSP data (max size = 31 bytes)
static uint8 scanRspData[] =
{
  // complete name
  17,   // length of this data
  GAP_ADTYPE_LOCAL_NAME_COMPLETE,
  '1',
  '2',
  '3',
  '4',
  '5',
  '6',
  '7',
  '8',
  '\'',
  's',
  ' ',
  'Q',
  'C',
  'u',
  'b',
  'e',

  // Tx power level
  11,   // length of this data
  GAP_ADTYPE_MANUFACTURER_SPECIFIC,
  0xd3,
  0x04,
  0x41,
  0x52,
  0x4F,
  0x59,
  0x47,
  0x42,
  0x49,
  0x56
};

// GAP - Advertisement data (max size = 31 bytes, though this is
// best kept short to conserve power while advertisting)
uint8 advertData[] =
{
  // Flags; this sets the device to use limited discoverable
  // mode (advertises for 30 seconds at a time) instead of general
  // discoverable mode (advertises indefinitely)
  2,   // length of this data
  GAP_ADTYPE_FLAGS, // 0x01
  GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED, // 0x04

  // Appearance: This is a #badgelife header.
  3,    // Length of this data
  GAP_ADTYPE_APPEARANCE, // Data type: "Appearance" // 0x19
  0xDC, // DC
  0x19, // 19 #badgelife

  // Queercon data: ID, current icon, etc
  14, // length of this data including the data type byte
  GAP_ADTYPE_MANUFACTURER_SPECIFIC, // manufacturer specific adv data type // 0xff
  0xD3, // Company ID - Fixed (queercon)
  0x04, // Company ID - Fixed (queercon)
  0x00, // Badge ID MSB
  0x00, // Badge ID LSB
  0x00, // Current icon ID
  0x00, // icon 40..47
  0x00, // icon 32..39
  0x00, // icon 24..31
  0x00, // icon 16..23
  0x00, // icon  8..15
  0x00, // icon  0.. 7
  0x00, // CHECK
  0x00, // CHECK
};

/*********************************************************************
 * LOCAL FUNCTIONS
 */

static void SimpleBLEBroadcaster_init(void);
static void SimpleBLEBroadcaster_taskFxn(UArg a0, UArg a1);

static void SimpleBLEBroadcaster_processStackMsg(ICall_Hdr *pMsg);
static void SimpleBLEBroadcaster_processAppMsg(sbbEvt_t *pMsg);
static void SimpleBLEBroadcaster_processStateChangeEvt(gaprole_States_t newState);

static void SimpleBLEBroadcaster_stateChangeCB(gaprole_States_t newState);

/*********************************************************************
 * PROFILE CALLBACKS
 */

// GAP Role Callbacks
static gapRolesCBs_t simpleBLEBroadcaster_BroadcasterCBs =
{
  SimpleBLEBroadcaster_stateChangeCB   // Profile State Change Callbacks
};

/*********************************************************************
 * PUBLIC FUNCTIONS
 */

/*********************************************************************
 * @fn      SimpleBLEBroadcaster_createTask
 *
 * @brief   Task creation function for the Simple BLE Broadcaster.
 *
 * @param   none
 *
 * @return  none
 */
void SimpleBLEBroadcaster_createTask(void)
{
  Task_Params taskParams;

  // Configure task
  Task_Params_init(&taskParams);
  taskParams.stack = sbbTaskStack;
  taskParams.stackSize = SBB_TASK_STACK_SIZE;
  taskParams.priority = SBB_TASK_PRIORITY;

  Task_construct(&sbbTask, SimpleBLEBroadcaster_taskFxn, &taskParams, NULL);
}
/*
 * Initialize the "Simple Broadcaster" - the BLE portion of this code.
 */
static void SimpleBLEBroadcaster_init(void)
{
  // ******************************************************************
  // N0 STACK API CALLS CAN OCCUR BEFORE THIS CALL TO ICall_registerApp
  // ******************************************************************
  // Register the current thread as an ICall dispatcher application
  // so that the application can send and receive messages.
  ICall_registerApp(&selfEntity, &ble_sem);

#ifdef USE_RCOSC
  RCOSC_enableCalibration();
#endif // USE_RCOSC

  // Create an RTOS queue for message from profile to be sent to app.
  appMsgQueue = Util_constructQueue(&appMsg);

  // Setup the GAP Broadcaster Role Profile
  {
    // For all hardware platforms, device starts advertising upon initialization
    uint8_t initial_advertising_enable = TRUE;

    // By setting this to zero, the device will go into the waiting state after
    // being discoverable for 30.72 second, and will not being advertising again
    // until the enabler is set back to TRUE
    uint16_t gapRole_AdvertOffTime = 0;

    uint8_t advType = GAP_ADTYPE_ADV_SCAN_IND; // use scannable undirected adv

    // Set the GAP Role Parameters
    GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t),
                         &initial_advertising_enable);
    GAPRole_SetParameter(GAPROLE_ADVERT_OFF_TIME, sizeof(uint16_t),
                         &gapRole_AdvertOffTime);

    GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof (scanRspData),
                         scanRspData);
    GAPRole_SetParameter(GAPROLE_ADVERT_DATA, sizeof(advertData), advertData);

    GAPRole_SetParameter(GAPROLE_ADV_EVENT_TYPE, sizeof(uint8_t), &advType);
  }

  // Set advertising interval
  {
    uint16_t advInt = DEFAULT_ADVERTISING_INTERVAL;

    GAP_SetParamValue(TGAP_LIM_DISC_ADV_INT_MIN, advInt);
    GAP_SetParamValue(TGAP_LIM_DISC_ADV_INT_MAX, advInt);
    GAP_SetParamValue(TGAP_GEN_DISC_ADV_INT_MIN, advInt);
    GAP_SetParamValue(TGAP_GEN_DISC_ADV_INT_MAX, advInt);
  }

  // Start the Device
  VOID GAPRole_StartDevice(&simpleBLEBroadcaster_BroadcasterCBs);

}

/*********************************************************************
 * @fn      SimpleBLEBroadcaster_processEvent
 *
 * @brief   Application task entry point for the Simple BLE Broadcaster.
 *
 * @param   none
 *
 * @return  none
 */
static void SimpleBLEBroadcaster_taskFxn(UArg a0, UArg a1)
{
  // Initialize application
  SimpleBLEBroadcaster_init();

  // Application main loop
  for (;;)
  {
    // Get the ticks since startup
    uint32_t tickStart = Clock_getTicks();

    // Waits for a signal to the semaphore associated with the calling thread.
    // Note that the semaphore associated with a thread is signaled when a
    // message is queued to the message receive queue of the thread or when
    // ICall_signal() function is called onto the semaphore.
    ICall_Errno errno = ICall_wait(ICALL_TIMEOUT_FOREVER);

    if (errno == ICALL_ERRNO_SUCCESS)
    {
      ICall_EntityID dest;
      ICall_ServiceEnum src;
      ICall_HciExtEvt *pMsg = NULL;

      if (ICall_fetchServiceMsg(&src, &dest,
                                (void **)&pMsg) == ICALL_ERRNO_SUCCESS)
      {
        if ((src == ICALL_SERVICE_CLASS_BLE) && (dest == selfEntity))
        {
          // Process inter-task message
          SimpleBLEBroadcaster_processStackMsg((ICall_Hdr *)pMsg);
        }

        if (pMsg)
        {
          ICall_freeMsg(pMsg);
        }
      }

      // If RTOS queue is not empty, process app message.
      while (!Queue_empty(appMsgQueue))
      {
        sbbEvt_t *pMsg = (sbbEvt_t *)Util_dequeueMsg(appMsgQueue);
        if (pMsg)
        {
          // Process message.
          SimpleBLEBroadcaster_processAppMsg(pMsg);

          // Free the space from the message.
          ICall_free(pMsg);
        }
      }

      if (update_ble) {
          update_ble = 0;
          GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof (scanRspData),
                               scanRspData);
          GAPRole_SetParameter(GAPROLE_ADVERT_DATA, sizeof(advertData), advertData);
      }

    }
  }
}

/*********************************************************************
 * @fn      SimpleBLEBroadcaster_processStackMsg
 *
 * @brief   Process an incoming stack message.
 *
 * @param   pMsg - message to process
 *
 * @return  none
 */
static void SimpleBLEBroadcaster_processStackMsg(ICall_Hdr *pMsg)
{
  switch (pMsg->event)
  {
    default:
      // do nothing
      break;
  }
}

/*********************************************************************
 * @fn      SimpleBLEPeripheral_processAppMsg
 *
 * @brief   Process an incoming callback from a profile.
 *
 * @param   pMsg - message to process
 *
 * @return  none
 */
static void SimpleBLEBroadcaster_processAppMsg(sbbEvt_t *pMsg)
{
  switch (pMsg->hdr.event)
  {
    case SBB_STATE_CHANGE_EVT:
      SimpleBLEBroadcaster_processStateChangeEvt((gaprole_States_t)pMsg->
                                                 hdr.state);
      break;

    default:
      // Do nothing.
      break;
  }
}

/*********************************************************************
 * @fn      SimpleBLEBroadcaster_stateChangeCB
 *
 * @brief   Callback from GAP Role indicating a role state change.
 *
 * @param   newState - new state
 *
 * @return  none
 */
static void SimpleBLEBroadcaster_stateChangeCB(gaprole_States_t newState)
{
  sbbEvt_t *pMsg;

  // Create dynamic pointer to message.
  if ((pMsg = ICall_malloc(sizeof(sbbEvt_t))))
  {
    pMsg->hdr.event = SBB_STATE_CHANGE_EVT;
    pMsg->hdr.state = newState;

    // Enqueue the message.
    Util_enqueueMsg(appMsgQueue, ble_sem, (uint8*)pMsg);
  }
}

/*********************************************************************
 * @fn      SimpleBLEBroadcaster_processStateChangeEvt
 *
 * @brief   Notification from the profile of a state change.
 *
 * @param   newState - new state
 *
 * @return  none
 */
static void SimpleBLEBroadcaster_processStateChangeEvt(gaprole_States_t newState)
{
  switch (newState)
  {
    case GAPROLE_STARTED:
      {
        uint8 ownAddress[B_ADDR_LEN];

        GAPRole_GetParameter(GAPROLE_BD_ADDR, ownAddress);
      }
      break;

    case GAPROLE_ADVERTISING:
      {
      }
      break;

    case GAPROLE_WAITING:
      {
      }
      break;

    case GAPROLE_ERROR:
      {
      }
      break;

    default:
      {
      }
      break;
  }
}

/*********************************************************************
*********************************************************************/
