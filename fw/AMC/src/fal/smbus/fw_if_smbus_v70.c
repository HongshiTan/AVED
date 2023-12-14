/**
* Copyright (c) 2023 Advanced Micro Devices, Inc. All rights reserved.
* SPDX-License-Identifier: MIT
*
* This file contains the FW IF SMBus V70-specific interface implementation.
*
* @file fw_if_smbus_v70.c
*
*/

#include <stdint.h>
#include <stdbool.h>

#include "fw_if.h"
#include "fw_if_smbus.h"
#include "fw_if_smbus_v70.h"

#include "xstatus.h"
#include "xparameters.h"
#include "FreeRTOS.h"
#include "portmacro.h"
#include "task.h"
#include "semphr.h"

#include "smbus.h"


#define CACHE_FAIL              ( -1 )
#define SMBUS_MAX_INSTANCES     ( 7 )

#define SMBUS_UPPER_FIREWALL    ( 0xBEEFCAFE )
#define SMBUS_LOWER_FIREWALL    ( 0xDEADFACE )
#define CHECK_FIREWALLS( f )    if( ( f->upperFirewall != SMBUS_UPPER_FIREWALL ) &&\
                                    ( f->lowerFirewall != SMBUS_LOWER_FIREWALL ) ) return FW_IF_ERRORS_INVALID_HANDLE
#define CHECK_HDL( f )          if( NULL == f ) return FW_IF_ERRORS_INVALID_HANDLE
#define CHECK_CFG( f )          if( NULL == ( f )->cfg  ) return FW_IF_ERRORS_INVALID_CFG
#define CHECK_DRIVER            if( FW_IF_FALSE == iInitialised ) return FW_IF_ERRORS_DRIVER_NOT_INITIALISED

static FW_IF_SMBUS_CFG *ucSMBusInstances[ SMBUS_MAX_INSTANCES ] = { 0 };

static SMBUS_PROFILE_TYPE xSMBusProfile = { 0 };
static int iInitialised = FW_IF_FALSE;

static uint16_t ucCallBackReturnedDataSize = 0;
static uint8_t pucCallBackReturnedData[ FW_IF_SMBUS_MAX_DATA ] = { 0 };

static uint16_t ucCallBackWriteDataSize = 0;
static uint8_t pucCallBackWriteData[ FW_IF_SMBUS_MAX_DATA ] = { 0 }; 

static int iUsePec = FW_IF_FALSE;
static int iIsController  = FW_IF_TRUE;
SemaphoreHandle_t xWriteCallbackSem = NULL;

#define MAX_COMMAND_VALUES 256

/**
 * @struct xCommand
 * @brief struct to store command data and size
 */
typedef struct {
    uint8_t ucSize;
    uint8_t pucData[ MAX_COMMAND_VALUES ];
} xCommand;

static xCommand xCommandsTable[ MAX_COMMAND_VALUES ] = { 0 };

/**
 *  @brief matches the command with the correct protocol
 *  @param ucCommand - command code of incoming packet
 *  @param pxProtocol - a pointer the protocol is written back to
 *  @warning called from ISR
 *  @return N/A
 */
void vGetProtocol( uint8_t ucCommand, SMBus_Command_Protocol_Type *pxProtocol );

/**
 *  @brief called when the controller requests to read data from us
 *  @param ucCommand - command code of incoming packet
 *  @param pucData - buffer data is written back to
 *  @param pusDatasize - size of data written back
 *  @warning called from ISR
 *  @return N/A
 */
void vReadData( uint8_t ucCommand, uint8_t *pucData, uint16_t *pusDatasize );

/**
 *  @brief called when the controller is sending us data to be written by us
 *  @param ucCommand - command code of incoming packet
 *  @param pucData - incoming data from the controller
 *  @param usDatasize - size of incoming data
 *  @param ulTransactionID - number to identify the transaction the data originates from
 *  @warning called from ISR
 *  @return N/A
 */
void vWriteData( uint8_t ucCommand, uint8_t *pucData, uint16_t usDatasize, uint32_t ulTransactionID );

/**
 *  @brief called when a transaction is complete
 *  @param ucCommand - command code of incoming packet
 *  @param ulTransactionID - number to identify the transaction the call originates from
 *  @param ulStatus - number to indicate any errors that occured
 *  @warning called from ISR
 *  @return N/A
 */
void vAnnounceResult( uint8_t ucCommand, uint32_t ulTransactionID, uint32_t ulStatus );

/**
 *  @brief called when our address is changed by ARP
 *  @param ucNewAddress - new address assigned by ARP
 *  @warning called from ISR
 *  @return N/A
 */
void vAnnounceARP( uint8_t ucNewAddress );

/**
 *  @brief Add smbus instance to cache
 *  @param pxSmbusCfg - smbus config
 *  @param ucInstance - instance number for the config
 *  @return the instance number if successful, otherwise CACHE_FAIL
 */
static int iCacheInstance( FW_IF_SMBUS_CFG *pxSmbusCfg, uint8_t ucInstance );

/**
 *  @brief Remove smbus instance to cache
 *  @param ucInstance - instance number to remove
 *  @return the instance number if successful, otherwise CACHE_FAIL
 */
static int iInvalidateInstance( uint8_t ucInstance );

/**
 *  @brief Get the instance number associated with the config
 *  @param pxSmbusCfg - config to find instance of
 *  @return the instance number if successful, otherwise CACHE_FAIL
 */
static int ucGetInstance( FW_IF_SMBUS_CFG *pxSmbusCfg );

/**
 * @brief Create smbus instance
 */
static uint32_t ulSmbusOpen( void *pvFwIf )
{
    uint32_t status = FW_IF_ERRORS_NONE;

    FW_IF_CFG *pxThisIf = ( FW_IF_CFG* )pvFwIf;
    CHECK_HDL( pxThisIf );
    CHECK_CFG( pxThisIf );
    CHECK_FIREWALLS( pxThisIf );
    FW_IF_SMBUS_CFG *pxThisSmbusCfg = ( FW_IF_SMBUS_CFG* )pxThisIf->cfg;

    SMBUS_INSTANCE_TYPE tmpInstance = { 0 };
    uint8_t ucInstance = SMBUS_INVALID_INSTANCE;
    SMBus_ARP_Capability xARPCapability = SMBUS_ARP_CAPABILITY_UNKNOWN;

    switch( pxThisSmbusCfg->arpCapability )
    {
        case FW_IF_SMBUS_ARP_CAPABILITY:
            xARPCapability = SMBUS_ARP_CAPABLE;
            break;

        case FW_IF_SMBUS_ARP_FIXED_DISCOVERABLE:
            xARPCapability = SMBUS_ARP_FIXED_AND_DISCOVERABLE;
            break;

        case FW_IF_SMBUS_ARP_FIXED_NOT_DISCOVERABLE:
            xARPCapability = SMBUS_ARP_FIXED_NOT_DISCOVERABLE;
            break;

        case FW_IF_SMBUS_ARP_NON_ARP_CAPABLE:
            xARPCapability = SMBUS_ARP_NON_ARP_CAPABLE;
            break;

        default:
            break;
    }

    tmpInstance.ucSMBusAddress = pxThisSmbusCfg->port;

    int i = 0;

    for( i = 0; i < FW_IF_SMBUS_UDID_LEN; i++ )
    {
        tmpInstance.ucUDID[i] = pxThisSmbusCfg->udid[i];
    }

    tmpInstance.ucSimpleDevice = 0;
    tmpInstance.xARPCapability = xARPCapability;
    tmpInstance.pFnGetProtocol = ( SMBUS_USER_SUPPLIED_ENVIRONMENT_GET_PROTOCOL_TYPE ) vGetProtocol;
    tmpInstance.pFnGetData = ( SMBUS_USER_SUPPLIED_ENVIRONMENT_GET_DATA_TYPE ) vReadData;
    tmpInstance.pFnWriteData = ( SMBUS_USER_SUPPLIED_ENVIRONMENT_WRITE_DATA_TYPE ) vWriteData;
    tmpInstance.pFnAnnounceResult = ( SMBUS_USER_SUPPLIED_ENVIRONMENT_COMMAND_COMPLETE ) vAnnounceResult;
    tmpInstance.pFnArpAddressChange = ( SMBUS_USER_SUPPLIED_ENVIRONMENT_ARP_ADRRESS_CHANGE ) vAnnounceARP;

    ucInstance = ucCreateSMBusInstance( &xSMBusProfile, &tmpInstance );

    if( SMBUS_INVALID_INSTANCE != ucInstance )
    {   
        if( CACHE_FAIL == iCacheInstance( pxThisSmbusCfg, ucInstance ) )
        {
            status = FW_IF_ERRORS_OPEN;
        }
    }
    else
    {
        status = FW_IF_ERRORS_OPEN;
    }

    return status;
}

/**
 * @brief Destroy smbus instance
 */
static uint32_t ulSmbusClose( void *pvFwIf )
{
    uint32_t status = FW_IF_ERRORS_NONE;

    FW_IF_CFG *pxThisIf = ( FW_IF_CFG* )pvFwIf;
    CHECK_HDL( pxThisIf );
    CHECK_CFG( pxThisIf );
    CHECK_FIREWALLS( pxThisIf );
    CHECK_DRIVER;
    FW_IF_SMBUS_CFG *pxThisSmbusCfg = ( FW_IF_SMBUS_CFG* )pxThisIf->cfg;

    uint8_t ucSMBusInstanceID = ucGetInstance( pxThisSmbusCfg );

    if( CACHE_FAIL != ucSMBusInstanceID )
    {
        if( SMBUS_ERROR != xDestroySMBusInstance( &xSMBusProfile, ucSMBusInstanceID ) )
        {
            if( CACHE_FAIL == iInvalidateInstance( ucSMBusInstanceID ) )
            {
                status = FW_IF_ERRORS_CLOSE;
            }
        }
        else
        {
            status = FW_IF_ERRORS_CLOSE;
        }
    }
    else
    {
        status = FW_IF_ERRORS_INVALID_HANDLE;
    }

    return status;
}

/**
 * @brief Write some data
 */
static uint32_t ulSmbusWrite( void *pvFwIf, uint64_t dstPort, uint8_t *pucData, uint32_t ulSize, uint32_t timeoutMs )
{
    uint32_t status = FW_IF_ERRORS_NONE;

    FW_IF_CFG *pxThisIf = ( FW_IF_CFG* )pvFwIf;
    CHECK_HDL( pxThisIf );
    CHECK_CFG( pxThisIf );
    CHECK_FIREWALLS( pxThisIf );
    CHECK_DRIVER;
    FW_IF_SMBUS_CFG *pxThisSmbusCfg = ( FW_IF_SMBUS_CFG* )pxThisIf->cfg;

    if( ( NULL == pucData ) || ( FW_IF_SMBUS_MAX_DATA < ulSize ) )
    {
        status = FW_IF_ERRORS_PARAMS; 
    } 
    else
    {
        if( FW_IF_TRUE == iIsController )
        {
            uint32_t ulCommandTransactionID = 0;
            uint8_t ucCommandByte = pucData[0];
            pucData[0] = ulSize - 1;
            SMBus_Command_Protocol_Type xProtocol = SMBUS_PROTOCOL_NONE;

            switch( ulSize - 1 )
            {
                case 1:
                    xProtocol = SMBUS_PROTOCOL_WRITE_BYTE;
                    break;

                case 2:
                    xProtocol = SMBUS_PROTOCOL_WRITE_WORD;
                    break;

                case 4:
                    xProtocol = SMBUS_PROTOCOL_WRITE_32;
                    break;
                
                case 8:
                    xProtocol = SMBUS_PROTOCOL_WRITE_64;
                    break;

                default:
                    xProtocol = SMBUS_PROTOCOL_BLOCK_WRITE;
                    break;
            }

            uint8_t ucSMBusInstanceID = ucGetInstance( pxThisSmbusCfg );

            if( CACHE_FAIL == ucSMBusInstanceID )
            {
                status = FW_IF_ERRORS_WRITE;
            }
            else
            {
                if( SMBUS_SUCCESS != ( xSMBusControllerInitiateCommand( &xSMBusProfile, 
                                                                        ucSMBusInstanceID, 
                                                                        dstPort, 
                                                                        ucCommandByte,
                                                                        xProtocol, 
                                                                        xProtocol == SMBUS_PROTOCOL_BLOCK_WRITE ? ( ulSize ) : ( ulSize - 1 ),
                                                                        xProtocol == SMBUS_PROTOCOL_BLOCK_WRITE ? ( pucData ): ( pucData + 1 ),
                                                                        iUsePec, 
                                                                        &ulCommandTransactionID ) ) )
                {
                    status = FW_IF_ERRORS_WRITE;
                }
            }
        }
        else
        {
            taskENTER_CRITICAL();
            memcpy( xCommandsTable[ pucData[0] ].pucData, ( pucData + 1 ), ( ulSize - 1) );
            xCommandsTable[ pucData[0] ].ucSize = ( ulSize - 1 );
            taskEXIT_CRITICAL();
        }
    }

    return status;
}

/**
 *  @brief Read some data
 */
static uint32_t ulSmbusRead( void *pvFwIf, uint64_t ullSrcPort, uint8_t *pucData, uint32_t *pulSize, uint32_t timeoutMs )
{
    uint32_t status = FW_IF_ERRORS_NONE;

    FW_IF_CFG *pxThisIf = ( FW_IF_CFG* )pvFwIf;
    CHECK_HDL( pxThisIf );
    CHECK_CFG( pxThisIf );
    CHECK_FIREWALLS( pxThisIf );
    CHECK_DRIVER;
    FW_IF_SMBUS_CFG *pxThisSmbusCfg = ( FW_IF_SMBUS_CFG* )pxThisIf->cfg;

    if( ( NULL == pucData ) || ( NULL == pulSize ) || ( FW_IF_SMBUS_MAX_DATA < *pulSize ))
    {
        status = FW_IF_ERRORS_PARAMS;
    }
    else
    {
        if( FW_IF_TRUE == iIsController )
        {
            memset( pucCallBackReturnedData, 0, sizeof(pucCallBackReturnedData) );
            ucCallBackReturnedDataSize = 0;

            uint32_t ulCommandTransactionID = 0;
            uint8_t ucCommandByte = pucData[0];
            SMBus_Command_Protocol_Type xProtocol = SMBUS_PROTOCOL_NONE;

            switch( *pulSize - 1 )
            {
                case 1:
                    xProtocol = SMBUS_PROTOCOL_READ_BYTE;
                    break;

                case 2:
                    xProtocol = SMBUS_PROTOCOL_READ_WORD;
                    break;

                case 4:
                    xProtocol = SMBUS_PROTOCOL_READ_32;
                    break;
                
                case 8:
                    xProtocol = SMBUS_PROTOCOL_READ_64;
                    break;

                default:
                    xProtocol = SMBUS_PROTOCOL_BLOCK_READ;
                    break;
            }

            uint8_t ucSMBusInstanceID = ucGetInstance( pxThisSmbusCfg );

            if( CACHE_FAIL == ucSMBusInstanceID )
            {
                status = FW_IF_ERRORS_WRITE;
            }
            else
            {
                if( SMBUS_SUCCESS != ( xSMBusControllerInitiateCommand( &xSMBusProfile, 
                                                                        ucSMBusInstanceID, 
                                                                        ullSrcPort,
                                                                        ucCommandByte,
                                                                        xProtocol, 
                                                                        1, 
                                                                        pucData + 1,
                                                                        iUsePec, 
                                                                        &ulCommandTransactionID ) ) )
                {
                    status = FW_IF_ERRORS_WRITE;
                }
                else
                {
                    if( FW_IF_TIMEOUT_NO_WAIT == timeoutMs )
                    {
                        if( pdFALSE == xSemaphoreTake( xWriteCallbackSem, 0 ) )
                        {
                            status = FW_IF_ERRORS_TIMEOUT;
                        }
                        else
                        {
                            memcpy( pucData, pucCallBackReturnedData, ucCallBackReturnedDataSize );
                            *pulSize = ucCallBackReturnedDataSize;
                        }
                    }
                    else if( FW_IF_TIMEOUT_WAIT_FOREVER == timeoutMs )
                    {
                        xSemaphoreTake( xWriteCallbackSem, portMAX_DELAY );

                        memcpy( pucData, pucCallBackReturnedData, ucCallBackReturnedDataSize );
                        *pulSize = ucCallBackReturnedDataSize;
                    }
                    else
                    {
                        if( pdFALSE == xSemaphoreTake( xWriteCallbackSem, timeoutMs ) )
                        {
                            status = FW_IF_ERRORS_TIMEOUT;
                        }
                        else
                        {
                            memcpy( pucData, pucCallBackReturnedData, ucCallBackReturnedDataSize );
                            *pulSize = ucCallBackReturnedDataSize;
                        }
                    }
                }
            }
        }
        else
        {
            if( FW_IF_TIMEOUT_NO_WAIT == timeoutMs )
            {
                if( pdFALSE == xSemaphoreTake( xWriteCallbackSem, 0 ) )
                {
                    status = FW_IF_ERRORS_TIMEOUT;
                }
                else
                {
                    memcpy( pucData, pucCallBackWriteData, ucCallBackWriteDataSize );
                    *pulSize = ucCallBackWriteDataSize;
                }
            }
            else if( FW_IF_TIMEOUT_WAIT_FOREVER == timeoutMs )
            {
                xSemaphoreTake( xWriteCallbackSem, portMAX_DELAY );

                memcpy( pucData, pucCallBackWriteData, ucCallBackWriteDataSize );
                *pulSize = ucCallBackWriteDataSize;
            }
            else
            {
                if( pdFALSE == xSemaphoreTake( xWriteCallbackSem, timeoutMs ) )
                {
                    status = FW_IF_ERRORS_TIMEOUT;
                }
                else
                {
                    memcpy( pucData, pucCallBackWriteData, ucCallBackWriteDataSize );
                    *pulSize = ucCallBackWriteDataSize;
                }
            }
        }
    }

    return status;
}

/**
 * @brief Change smbus settings
 */
static uint32_t ulSmbusIoctrl( void *pvFwIf, uint32_t ulOption, void *pvValue )
{
    uint32_t status = FW_IF_ERRORS_NONE;

    FW_IF_CFG *pxThisIf = ( FW_IF_CFG* )pvFwIf;
    CHECK_HDL( pxThisIf );
    CHECK_CFG( pxThisIf );
    CHECK_FIREWALLS( pxThisIf );
    CHECK_DRIVER;
    FW_IF_SMBUS_CFG *pxThisSmbusCfg = ( FW_IF_SMBUS_CFG* )pxThisIf->cfg;

    if( ( NULL == pvFwIf) || ( NULL == pvValue && ulOption == FW_IF_SMBUS_IOCTRL_ENABLE_PEC))
    {
        status = FW_IF_ERRORS_PARAMS;
    }
    else
    {
        switch( ulOption )
        {
            case FW_IF_SMBUS_IOCTRL_ENABLE_PEC:
                iUsePec = *( (bool *)pvValue );
                break;

            case FW_IF_SMBUS_IOCTRL_SET_TARGET:
                taskENTER_CRITICAL();
                iIsController = FW_IF_FALSE;
                taskEXIT_CRITICAL();
                pxThisSmbusCfg->role = FW_IF_SMBUS_ROLE_TARGET;

                break;

            case FW_IF_SMBUS_IOCTRL_SET_CONTROLLER:
                taskENTER_CRITICAL();
                iIsController = FW_IF_TRUE;
                taskEXIT_CRITICAL();
                pxThisSmbusCfg->role = FW_IF_SMBUS_ROLE_CONTROLLER;
                break;

            default:
                status = FW_IF_ERRORS_UNRECOGNISED_OPTION;
                break;
        }
    }

    return status;
}

/**
 * @brief Bind interface to user
 */
static uint32_t ulSmbusBindCallback( void *pvFwIf, FW_IF_callback *pxNewFunc )
{
    //TODO
    return 0;
}

/**
 *  @brief matches the command with the correct protocol
 */
void vGetProtocol( uint8_t ucCommand, SMBus_Command_Protocol_Type *pxProtocol )
{
    switch( ucCommand )
    {
        case FW_IF_SMBUS_V70_FRU_DATA_READ:
            *pxProtocol = SMBUS_PROTOCOL_READ_BYTE;
            break;

        case FW_IF_SMBUS_V70_MAX_DIMM_TEMP:
            *pxProtocol = SMBUS_PROTOCOL_READ_BYTE;
            break;

        case FW_IF_SMBUS_V70_BOARD_TEMP:
            *pxProtocol = SMBUS_PROTOCOL_READ_BYTE;
            break;

        case FW_IF_SMBUS_V70_BOARD_POWER_CONSUMPTION:
            *pxProtocol = SMBUS_PROTOCOL_READ_WORD;
            break;

        case FW_IF_SMBUS_V70_SC_FW_VER:
            *pxProtocol = SMBUS_PROTOCOL_READ_32;
            break;

        case FW_IF_SMBUS_V70_FPGA_TEMP:
            *pxProtocol = SMBUS_PROTOCOL_READ_BYTE;
            break;

        case FW_IF_SMBUS_V70_MAX_QSFP_TEMP:
            *pxProtocol = SMBUS_PROTOCOL_READ_BYTE;
            break;

        case FW_IF_SMBUS_V70_FPGA_RESET:
            *pxProtocol = SMBUS_PROTOCOL_WRITE_BYTE;
            break;

        case FW_IF_SMBUS_V70_FRU_DATA_WRITE:
            *pxProtocol = SMBUS_PROTOCOL_READ_BYTE;
            break;

        default:
            break;
    }
}

/**
 *  @brief called when the controller requests to read data from us
 */
void vReadData( uint8_t ucCommand, uint8_t *pucData, uint16_t *pusDatasize )
{
    memcpy( pucData, xCommandsTable[ ucCommand ].pucData, xCommandsTable[ ucCommand ].ucSize );
    *pusDatasize = xCommandsTable[ ucCommand ].ucSize;
}

/**
 *  @brief called when the controller is sending us data to be read by us
 */
void vWriteData( uint8_t ucCommand, uint8_t *pucData, uint16_t usDatasize, uint32_t ulTransactionID )
{
    if( ( NULL != pucData ) && ( 0 != usDatasize ) )
    {
        if( FW_IF_TRUE == iIsController )
        {
            memcpy( pucCallBackReturnedData, pucData, usDatasize );
            ucCallBackReturnedDataSize = usDatasize;
        }
        else
        {
            pucCallBackWriteData[ 0 ] = ucCommand;
            memcpy( pucCallBackWriteData + 1, pucData, usDatasize );
            ucCallBackWriteDataSize = usDatasize + 1;
        }
    }

    if( NULL != xWriteCallbackSem )
    {
        xSemaphoreGiveFromISR( xWriteCallbackSem, NULL );
    }
}

/**
 *  @brief called when a transaction is complete
 */
void vAnnounceResult( uint8_t ucCommand, uint32_t ulTransactionID, uint32_t ulStatus )
{
    //TODO
}

/**
 *  @brief called when our address is changed by ARP
 */
void vAnnounceARP( uint8_t ucNewAddress )
{
    //TODO
}

/**
 *  @brief Add smbus instance to cache
 */
static int iCacheInstance( FW_IF_SMBUS_CFG *pxSmbusCfg, uint8_t ucInstance )
{
    int iStatus = CACHE_FAIL;

    if( ( 0 <= ucInstance ) &&
        ( SMBUS_MAX_INSTANCES > ucInstance ) &&
        ( NULL == ucSMBusInstances[ ucInstance ] ) )
    {
        ucSMBusInstances[ ucInstance ] = pxSmbusCfg;
        iStatus = ucInstance;
    }

    return iStatus;
}

/**
 *  @brief Remove smbus instance to cache
 */
static int iInvalidateInstance( uint8_t ucInstance )
{
    int iStatus = CACHE_FAIL;

    if( ( 0 <= ucInstance ) &&
        ( SMBUS_MAX_INSTANCES > ucInstance ) &&
        ( NULL != ucSMBusInstances[ ucInstance ] ) )
    {
        ucSMBusInstances[ ucInstance ] = NULL;
        iStatus = ucInstance;
    }

    return iStatus;
}

/**
 *  @brief Get the instance number associated with the config
 */
static int ucGetInstance( FW_IF_SMBUS_CFG *pxSmbusCfg )
{
    uint8_t ucInstance = CACHE_FAIL;
    int i = 0;

    /* check each entry in the cache */
    for( i = 0; i < SMBUS_MAX_INSTANCES; i++ )
    {
        if( pxSmbusCfg == ucSMBusInstances[ i ] )
        {
            ucInstance = i;
            break;
        }
    }

    return ucInstance;
}

/**
 *  @brief Get the current tick value
 */
void vGetTicks( uint32_t *pulTicks )
{
    if( NULL != pulTicks )
    {
        *pulTicks =  xTaskGetTickCountFromISR();
    }
}

/**
 * @brief initialisation function for smbus interfaces (generic across all smbus interfaces)
 */
uint32_t ulFW_IF_SMBUS_Init( FW_IF_SMBUS_INIT_CFG *pxCfg )
{
    uint32_t status = FW_IF_ERRORS_NONE;

    if( FW_IF_FALSE != iInitialised )
    {
        status = FW_IF_ERRORS_DRIVER_IN_USE;
    }
    else
    {
        if( NULL == pxCfg )
        {
            status = FW_IF_ERRORS_PARAMS;
        } 
        else
        {
            xWriteCallbackSem = xSemaphoreCreateBinary();
            if( NULL == xWriteCallbackSem )
            {
                status = FW_IF_ERRORS_DRIVER_NOT_INITIALISED;
            }
            else
            {
                if( SMBUS_SUCCESS != xInitSMBus( &xSMBusProfile, pxCfg->baudRate, (void *)pxCfg->baseAddr, LOG_LEVEL_DEBUG, vGetTicks ) )
                {
                    status = FW_IF_ERRORS_DRIVER_NOT_INITIALISED;
                }
                else
                {
                    if( pdPASS != xPortInstallInterruptHandler( XPAR_FABRIC_BLP_BLP_LOGIC_AXI_SMBUS_RPU_IP2INTC_IRPT_INTR, vSMBusInterruptHandler, &xSMBusProfile ) )
                    {
                        status = FW_IF_ERRORS_DRIVER_NOT_INITIALISED;
                    }
                    else
                    {
                        vPortEnableInterrupt( XPAR_FABRIC_BLP_BLP_LOGIC_AXI_SMBUS_RPU_IP2INTC_IRPT_INTR );

                        if( SMBUS_SUCCESS != xSMBusInterruptEnableInterrupts( &xSMBusProfile ) )
                        {
                            status = FW_IF_ERRORS_DRIVER_NOT_INITIALISED;
                        }
                        else
                        {
                            iInitialised = FW_IF_TRUE;
                        }
                    }
                }
            }
        }
    }

    return status;
}

/**
 * @brief creates an instance of the smbus interface
 */
uint32_t ulFW_IF_SMBUS_Create( FW_IF_CFG *pxFwIf, FW_IF_SMBUS_CFG *pxSmbusCfg )
{
    CHECK_DRIVER;
    uint32_t status = FW_IF_ERRORS_NONE;

    if( ( NULL != pxFwIf ) && ( NULL != pxSmbusCfg ) )
    {
        FW_IF_CFG xLocalIfCfg =
        {
            .upperFirewall  = SMBUS_UPPER_FIREWALL,
            .open           = &ulSmbusOpen,
            .close          = &ulSmbusClose,
            .write          = &ulSmbusWrite,
            .read           = &ulSmbusRead,
            .ioctrl         = &ulSmbusIoctrl,
            .bindCallback   = &ulSmbusBindCallback,
            .cfg            = ( void* )pxSmbusCfg,
            .lowerFirewall  = SMBUS_LOWER_FIREWALL
        };

        memcpy( fwIf, &xLocalIfCfg, sizeof( FW_IF_CFG ) );
    }
    else
    {
        status = FW_IF_ERRORS_PARAMS;
    }

    return status;
}