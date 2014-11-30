/**
 ******************************************************************************
 * @addtogroup OpenPilotModules OpenPilot Modules
 * @{
 * @addtogroup ManualControlModule Manual Control Module
 * @brief Provide manual control or allow it alter flight mode.
 * @{
 *
 * Reads in the ManualControlCommand FlightMode setting from receiver then either
 * pass the settings straght to ActuatorDesired object (manual mode) or to
 * AttitudeDesired object (stabilized mode)
 *
 * @file       manualcontrol.c
 * @author     The OpenPilot Team, http://www.openpilot.org Copyright (C) 2014.
 * @brief      ManualControl module. Handles safety R/C link and flight mode.
 *
 * @see        The GNU Public License (GPL) Version 3
 *
 *****************************************************************************/
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "inc/manualcontrol.h"
#include <sanitycheck.h>
#include <manualcontrolsettings.h>
#include <manualcontrolcommand.h>
#include <flightmodesettings.h>
#include <flightstatus.h>
#include <systemsettings.h>
#include <stabilizationdesired.h>
#include <callbackinfo.h>
#ifndef PIOS_EXCLUDE_ADVANCED_FEATURES
#include <vtolpathfollowersettings.h>
#include <stabilizationsettings.h>
#endif

// Private constants
#if defined(PIOS_MANUAL_STACK_SIZE)
#define STACK_SIZE_BYTES  PIOS_MANUAL_STACK_SIZE
#else
#define STACK_SIZE_BYTES  1152
#endif

#define CALLBACK_PRIORITY CALLBACK_PRIORITY_REGULAR
#define CBTASK_PRIORITY   CALLBACK_TASK_FLIGHTCONTROL


// defined handlers

static const controlHandler handler_MANUAL = {
    .controlChain      = {
        .Stabilization = false,
        .PathFollower  = false,
        .PathPlanner   = false,
    },
    .handler           = &manualHandler,
};
static const controlHandler handler_STABILIZED = {
    .controlChain      = {
        .Stabilization = true,
        .PathFollower  = false,
        .PathPlanner   = false,
    },
    .handler           = &stabilizedHandler,
};


static const controlHandler handler_AUTOTUNE = {
    .controlChain      = {
        .Stabilization = false,
        .PathFollower  = false,
        .PathPlanner   = false,
    },
    .handler           = NULL,
};

#ifndef PIOS_EXCLUDE_ADVANCED_FEATURES
static const controlHandler handler_PATHFOLLOWER = {
    .controlChain      = {
        .Stabilization = true,
        .PathFollower  = true,
        .PathPlanner   = false,
    },
    .handler           = &pathFollowerHandler,
};

static const controlHandler handler_PATHPLANNER = {
    .controlChain      = {
        .Stabilization = true,
        .PathFollower  = true,
        .PathPlanner   = true,
    },
    .handler           = &pathPlannerHandler,
};

#endif /* ifndef PIOS_EXCLUDE_ADVANCED_FEATURES */
// Private variables
static DelayedCallbackInfo *callbackHandle;

// Private functions
static void configurationUpdatedCb(UAVObjEvent *ev);
static void commandUpdatedCb(UAVObjEvent *ev);
static void manualControlTask(void);
#ifndef PIOS_EXCLUDE_ADVANCED_FEATURES
static uint8_t isGPSAssistedFlightMode( uint8_t position);
#endif

#define assumptions (assumptions1 && assumptions2 && assumptions3 && assumptions4 && assumptions5 && assumptions6 && assumptions7 && assumptions_flightmode)

/**
 * Module starting
 */
int32_t ManualControlStart()
{
    // Run this initially to make sure the configuration is checked
    configuration_check();

    // Whenever the configuration changes, make sure it is safe to fly
    SystemSettingsConnectCallback(configurationUpdatedCb);
    ManualControlSettingsConnectCallback(configurationUpdatedCb);
    ManualControlCommandConnectCallback(commandUpdatedCb);

    // clear alarms
    AlarmsClear(SYSTEMALARMS_ALARM_MANUALCONTROL);

    // Make sure unarmed on power up
    armHandler(true);

#ifndef PIOS_EXCLUDE_ADVANCED_FEATURES
    takeOffLocationHandlerInit();
#endif
    // Start main task
    PIOS_CALLBACKSCHEDULER_Dispatch(callbackHandle);

    return 0;
}

/**
 * Module initialization
 */
int32_t ManualControlInitialize()
{
    /* Check the assumptions about uavobject enum's are correct */
    PIOS_STATIC_ASSERT(assumptions);

    ManualControlCommandInitialize();
    FlightStatusInitialize();
    ManualControlSettingsInitialize();
    FlightModeSettingsInitialize();
    SystemSettingsInitialize();
#ifndef PIOS_EXCLUDE_ADVANCED_FEATURES
    VtolPathFollowerSettingsInitialize();
    StabilizationSettingsInitialize();
#endif
    callbackHandle = PIOS_CALLBACKSCHEDULER_Create(&manualControlTask, CALLBACK_PRIORITY, CBTASK_PRIORITY, CALLBACKINFO_RUNNING_MANUALCONTROL, STACK_SIZE_BYTES);

    return 0;
}
MODULE_INITCALL(ManualControlInitialize, ManualControlStart);

/**
 * Module task
 */
static void manualControlTask(void)
{
    // Process Arming
    armHandler(false);
#ifndef PIOS_EXCLUDE_ADVANCED_FEATURES
    takeOffLocationHandler();
#endif
    // Process flight mode
    FlightStatusData flightStatus;

    FlightStatusGet(&flightStatus);
    ManualControlCommandData cmd;
    ManualControlCommandGet(&cmd);

    FlightModeSettingsData modeSettings;
    FlightModeSettingsGet(&modeSettings);

    uint8_t position = cmd.FlightModeSwitchPosition;
    uint8_t newMode  = flightStatus.FlightMode;
    uint8_t newFlightModeGPSAssist = flightStatus.FlightModeGPSAssist;
    uint8_t newPositionRoamState  = flightStatus.PositionRoamState;
    uint8_t newPositionRoamThrustMode  = flightStatus.PositionRoamThrustMode;
    if (position < FLIGHTMODESETTINGS_FLIGHTMODEPOSITION_NUMELEM) {
        newMode = modeSettings.FlightModePosition[position];
    }

    // Depending on the mode update the Stabilization or Actuator objects
    const controlHandler *handler = &handler_MANUAL;
    switch (newMode) {
    case FLIGHTSTATUS_FLIGHTMODE_MANUAL:
        handler = &handler_MANUAL;
        break;
    case FLIGHTSTATUS_FLIGHTMODE_STABILIZED1:
    case FLIGHTSTATUS_FLIGHTMODE_STABILIZED2:
    case FLIGHTSTATUS_FLIGHTMODE_STABILIZED3:
    case FLIGHTSTATUS_FLIGHTMODE_STABILIZED4:
    case FLIGHTSTATUS_FLIGHTMODE_STABILIZED5:
    case FLIGHTSTATUS_FLIGHTMODE_STABILIZED6:
        handler = &handler_STABILIZED;

#ifndef PIOS_EXCLUDE_ADVANCED_FEATURES
        newFlightModeGPSAssist = isGPSAssistedFlightMode( position );
        if (newFlightModeGPSAssist) {
            if (fabsf(cmd.Roll) > 0.0f || fabsf(cmd.Pitch) > 0.0f) {
                newPositionRoamState = FLIGHTSTATUS_POSITIONROAMSTATE_STABILIZED;

                // Check vtol thrust control and override if need be the thrust mode
                // of the PositionRoamStabiSelect-ed option
                VtolPathFollowerSettingsData vtolPathFollowerSettings;
	        VtolPathFollowerSettingsGet(&vtolPathFollowerSettings);
                if (vtolPathFollowerSettings.ThrustControl == VTOLPATHFOLLOWERSETTINGS_THRUSTCONTROL_MANUAL) {
                    newPositionRoamThrustMode = FLIGHTSTATUS_POSITIONROAMTHRUSTMODE_MANUAL;
                }
                else { //auto thrust control requires altitude controlled throttle in the Stabi mode
                    newPositionRoamThrustMode = FLIGHTSTATUS_POSITIONROAMTHRUSTMODE_MIXED;
                }
            }
            else {

        	// ok sticks centered (pitch and roll is 0.0 exactly thanks to deadband code in receiver.c
        	// handler is pathfollower
        	handler = &handler_PATHFOLLOWER;

        	// if existing state is none is previously stablised, initiate to braking
        	if ( flightStatus.PositionRoamState == FLIGHTSTATUS_POSITIONROAMSTATE_NONE ||
       	             flightStatus.PositionRoamState == FLIGHTSTATUS_POSITIONROAMSTATE_STABILIZED ) {
        	    newPositionRoamState = FLIGHTSTATUS_POSITIONROAMSTATE_BRAKING;
        	}
            }
        }
#endif
        break;
#ifndef PIOS_EXCLUDE_ADVANCED_FEATURES

    case FLIGHTSTATUS_FLIGHTMODE_POSITIONHOLD:
    case FLIGHTSTATUS_FLIGHTMODE_POSITIONVARIOFPV:
    case FLIGHTSTATUS_FLIGHTMODE_POSITIONVARIOLOS:
    case FLIGHTSTATUS_FLIGHTMODE_POSITIONVARIONSEW:
    case FLIGHTSTATUS_FLIGHTMODE_RETURNTOBASE:
    case FLIGHTSTATUS_FLIGHTMODE_LAND:
    case FLIGHTSTATUS_FLIGHTMODE_POI:
    case FLIGHTSTATUS_FLIGHTMODE_AUTOCRUISE:
        handler = &handler_PATHFOLLOWER;
        break;
    case FLIGHTSTATUS_FLIGHTMODE_PATHPLANNER:
        handler = &handler_PATHPLANNER;
        break;
#endif
    case FLIGHTSTATUS_FLIGHTMODE_AUTOTUNE:
        handler = &handler_AUTOTUNE;
        break;
        // There is no default, so if a flightmode is forgotten the compiler can throw a warning!
    }

    bool newinit = false;

    // FlightMode needs to be set correctly on first run (otherwise ControlChain is invalid)
    static bool firstRun = true;

    if (flightStatus.FlightMode != newMode || firstRun ||
	newPositionRoamState != flightStatus.PositionRoamState) {
        firstRun = false;
        flightStatus.ControlChain = handler->controlChain;
        flightStatus.FlightMode   = newMode;
        flightStatus.FlightModeGPSAssist = newFlightModeGPSAssist;
        flightStatus.PositionRoamState = newPositionRoamState;
        flightStatus.PositionRoamThrustMode = newPositionRoamThrustMode;
        FlightStatusSet(&flightStatus);
        newinit = true;
    }
    if (handler->handler) {
        handler->handler(newinit);
    }
}

/**
 * Called whenever a critical configuration component changes
 */
static void configurationUpdatedCb(__attribute__((unused)) UAVObjEvent *ev)
{
    configuration_check();
}

/**
 * Called whenever a critical configuration component changes
 */
static void commandUpdatedCb(__attribute__((unused)) UAVObjEvent *ev)
{
    PIOS_CALLBACKSCHEDULER_Dispatch(callbackHandle);
}


/**
 * Check and set modes for gps assisted stablised flight modes
 */
#ifndef PIOS_EXCLUDE_ADVANCED_FEATURES
static uint8_t isGPSAssistedFlightMode( uint8_t position)
{

    uint8_t isGPSAssistedFlag = STABILIZATIONSETTINGS_FLIGHTMODEGPSASSISTMAP_NONE;
    uint8_t FlightModeGPSAssistMap[STABILIZATIONSETTINGS_FLIGHTMODEGPSASSISTMAP_NUMELEM];

    StabilizationSettingsFlightModeGPSAssistMapGet(FlightModeGPSAssistMap);

    if (position < STABILIZATIONSETTINGS_FLIGHTMODEGPSASSISTMAP_NUMELEM) {
        isGPSAssistedFlag = FlightModeGPSAssistMap[position];
    }

    return isGPSAssistedFlag;
}
#endif

/**
 * @}
 * @}
 */
