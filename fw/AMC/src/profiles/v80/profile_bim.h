/**
 * Copyright (c) 2023 Advanced Micro Devices, Inc. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * This file contains the bim profile for the V80
 *
 * @file profile_bim.h
 *
 */

#ifndef _PROFILE_BIM_H_
#define _PROFILE_BIM_H_

#include "bim.h"

#include "amc_cfg.h"

/* proxy drivers */
#include "asc_proxy_driver.h"
#include "ami_proxy_driver.h"
#include "apc_proxy_driver.h"
#include "axc_proxy_driver.h"

/* AXC Event table */
BIM_EVENTS PROFILE_BIM_AXC_EVENTS[ MAX_AXC_PROXY_DRIVER_EVENTS ] =
{
    { AXC_PROXY_DRIVER_E_QSFP_PRESENT,     0, BIM_STATUS_HEALTHY },
    { AXC_PROXY_DRIVER_E_QSFP_NOT_PRESENT, 0, BIM_STATUS_HEALTHY },

};

/* APC Event table */
BIM_EVENTS PROFILE_BIM_APC_EVENTS[ MAX_APC_PROXY_DRIVER_EVENTS ] =
{
    { APC_PROXY_DRIVER_E_DOWNLOAD_STARTED,           0, BIM_STATUS_HEALTHY },
    { APC_PROXY_DRIVER_E_DOWNLOAD_COMPLETE,          0, BIM_STATUS_HEALTHY },
    { APC_PROXY_DRIVER_E_DOWNLOAD_BUSY,              0, BIM_STATUS_HEALTHY },
    { APC_PROXY_DRIVER_E_DOWNLOAD_FAILED,            0, BIM_STATUS_CRITICAL },
    { APC_PROXY_DRIVER_E_COPY_STARTED,               0, BIM_STATUS_HEALTHY },
    { APC_PROXY_DRIVER_E_COPY_COMPLETE,              0, BIM_STATUS_HEALTHY },
    { APC_PROXY_DRIVER_E_COPY_BUSY,                  0, BIM_STATUS_HEALTHY },
    { APC_PROXY_DRIVER_E_COPY_FAILED,                0, BIM_STATUS_CRITICAL },
    { APC_PROXY_DRIVER_E_PARTITION_SELECTED,         0, BIM_STATUS_HEALTHY },
    { APC_PROXY_DRIVER_E_PARTITION_SELECTION_FAILED, 0, BIM_STATUS_CRITICAL }

};

/* ASC Event table */
BIM_EVENTS PROFILE_BIM_ASC_EVENTS[ MAX_ASC_PROXY_DRIVER_EVENTS ] =
{
    { ASC_PROXY_DRIVER_E_SENSOR_UPDATE_COMPLETE, 0, BIM_STATUS_HEALTHY },
    { ASC_PROXY_DRIVER_E_SENSOR_UNAVAILABLE,     0, BIM_STATUS_DEGRADED },
    { ASC_PROXY_DRIVER_E_SENSOR_COMMS_FAILURE,   0, BIM_STATUS_CRITICAL },
    { ASC_PROXY_DRIVER_E_SENSOR_WARNING,         0, BIM_STATUS_DEGRADED },
    { ASC_PROXY_DRIVER_E_SENSOR_CRITICAL,        0, BIM_STATUS_CRITICAL },
    { ASC_PROXY_DRIVER_E_SENSOR_FATAL,           0, BIM_STATUS_FATAL },
    { ASC_PROXY_DRIVER_E_SENSOR_LOWER_WARNING,   0, BIM_STATUS_DEGRADED },
    { ASC_PROXY_DRIVER_E_SENSOR_LOWER_CRITICAL,  0, BIM_STATUS_CRITICAL },
    { ASC_PROXY_DRIVER_E_SENSOR_LOWER_FATAL,     0, BIM_STATUS_FATAL },
    { ASC_PROXY_DRIVER_E_SENSOR_UPPER_WARNING,   0, BIM_STATUS_DEGRADED },
    { ASC_PROXY_DRIVER_E_SENSOR_UPPER_CRITICAL,  0, BIM_STATUS_CRITICAL },
    { ASC_PROXY_DRIVER_E_SENSOR_UPPER_FATAL,     0, BIM_STATUS_FATAL }

};

/* AMI Event table */
BIM_EVENTS PROFILE_BIM_AMI_EVENTS[ MAX_AMI_PROXY_DRIVER_EVENTS ] =
{
    { AMI_PROXY_DRIVER_E_PDI_DOWNLOAD_START, 0, BIM_STATUS_HEALTHY },
    { AMI_PROXY_DRIVER_E_PDI_COPY_START,     0, BIM_STATUS_HEALTHY },
    { AMI_PROXY_DRIVER_E_SENSOR_READ,        0, BIM_STATUS_HEALTHY },
    { AMI_PROXY_DRIVER_E_GET_IDENTITY,       0, BIM_STATUS_HEALTHY },
    { AMI_PROXY_DRIVER_E_BOOT_SELECT,        0, BIM_STATUS_HEALTHY },
    { AMI_PROXY_DRIVER_E_HEARTBEAT,          0, BIM_STATUS_HEALTHY },
    { AMI_PROXY_DRIVER_E_EEPROM_READ_WRITE,  0, BIM_STATUS_HEALTHY }

};

/* BMC Event table */
BIM_EVENTS PROFILE_BIM_BMC_EVENTS[ MAX_BMC_PROXY_DRIVER_EVENTS ] =
{
    { BMC_PROXY_DRIVER_E_MSG_ARRIVAL, 0, BIM_STATUS_HEALTHY },
    { BMC_PROXY_DRIVER_E_GET_PDR, 0, BIM_STATUS_HEALTHY },
    { BMC_PROXY_DRIVER_E_GET_PDR_REPOSITORY_INFO, 0, BIM_STATUS_HEALTHY },
    { BMC_PROXY_DRIVER_E_GET_SENSOR_INFO, 0, BIM_STATUS_HEALTHY },
    { BMC_PROXY_DRIVER_E_ENABLE_SENSOR, 0, BIM_STATUS_HEALTHY },
    { BMC_PROXY_DRIVER_E_INVALID_REQUEST_RECVD, 0, BIM_STATUS_DEGRADED }

};

/* Module data table */
BIM_MODULES PROFILE_BIM_MODULE_DATA[ MAX_AMC_CFG_UNIQUE_ID ] =
{
    { AMC_CFG_UNIQUE_ID_AXC, BIM_STATUS_HEALTHY, BIM_STATUS_DEGRADED, PROFILE_BIM_AXC_EVENTS,
      MAX_AXC_PROXY_DRIVER_EVENTS },
    { AMC_CFG_UNIQUE_ID_APC, BIM_STATUS_HEALTHY, BIM_STATUS_CRITICAL, PROFILE_BIM_APC_EVENTS,
      MAX_APC_PROXY_DRIVER_EVENTS },
    { AMC_CFG_UNIQUE_ID_ASC, BIM_STATUS_HEALTHY, BIM_STATUS_CRITICAL, PROFILE_BIM_ASC_EVENTS,
      MAX_ASC_PROXY_DRIVER_EVENTS },
    { AMC_CFG_UNIQUE_ID_AMI, BIM_STATUS_HEALTHY, BIM_STATUS_FATAL, PROFILE_BIM_AMI_EVENTS,
      MAX_AMI_PROXY_DRIVER_EVENTS },
    { AMC_CFG_UNIQUE_ID_BMC, BIM_STATUS_HEALTHY, BIM_STATUS_DEGRADED, PROFILE_BIM_BMC_EVENTS,
      MAX_BMC_PROXY_DRIVER_EVENTS }

};

#endif
