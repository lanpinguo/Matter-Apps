/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <lib/support/logging/CHIPLogging.h>

#include "app_task.h"

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/ConcreteAttributePath.h>

using namespace ::chip;
using namespace ::chip::app::Clusters;
using namespace ::chip::app::Clusters::OnOff;

// extern "C" {
//     uint32_t nrfx_reset_reason_get(void);
// }

void MatterPostAttributeChangeCallback(const chip::app::ConcreteAttributePath &attributePath, uint8_t type,
				       uint16_t size, uint8_t *value)
{
	ClusterId clusterId = attributePath.mClusterId;
	AttributeId attributeId = attributePath.mAttributeId;
	EndpointId aEndpointId = attributePath.mEndpointId;

	if (clusterId == OnOff::Id && attributeId == OnOff::Attributes::OnOff::Id) {
        
		ChipLogProgress(Zcl, "Cluster OnOff: attribute OnOff set to %" PRIu8 "", *value);
		AppTask::Instance().GetPWMDevice().InitiateAction(*value ? PWMDevice::ON_ACTION : PWMDevice::OFF_ACTION,
								  static_cast<int32_t>(AppEventType::Lighting), value);

        AppTask::Instance().GetRelayDevice().Set(
                (static_cast<int32_t>(aEndpointId) - 1) / 4, (static_cast<int32_t>(aEndpointId) - 1) % 4, 
                *value ? IO_Relay::ON_ACTION : IO_Relay::OFF_ACTION);

	} else if (clusterId == LevelControl::Id && attributeId == LevelControl::Attributes::CurrentLevel::Id) {

		ChipLogProgress(Zcl, "Cluster LevelControl: attribute CurrentLevel set to %" PRIu8 "", *value);
        if (AppTask::Instance().GetPWMDevice(0).IsTurnedOn()) {
            AppTask::Instance().GetPWMDevice(0).InitiateAction(
                PWMDevice::LEVEL_ACTION, static_cast<int32_t>(AppEventType::Lighting), value);
        } else {
            ChipLogDetail(Zcl, "LED is off. Try to use move-to-level-with-on-off instead of move-to-level");
        }

	}
}

/** @brief OnOff Cluster Init
 *
 * This function is called when a specific cluster is initialized. It gives the
 * application an opportunity to take care of cluster initialization procedures.
 * It is called exactly once for each endpoint where cluster is present.
 *
 * @param endpoint   Ver.: always
 *
 * TODO Issue #3841
 * emberAfOnOffClusterInitCallback happens before the stack initialize the cluster
 * attributes to the default value.
 * The logic here expects something similar to the deprecated Plugins callback
 * emberAfPluginOnOffClusterServerPostInitCallback.
 *
 */
void emberAfOnOffClusterInitCallback(EndpointId endpoint)
{
	EmberAfStatus status;
	bool storedValue = false;
    BootReasonType reset_reason;

    reset_reason = AppTask::Instance().ResetReasonGet();
    
    if(reset_reason == BootReasonType::kPowerOnReboot){
		AppTask::Instance().GetPWMDevice().InitiateAction(
			PWMDevice::ON_ACTION,
			static_cast<int32_t>(AppEventType::Lighting), reinterpret_cast<uint8_t *>(&storedValue));
        AppTask::Instance().GetRelayDevice().Set(0, 0, IO_Relay::ON_ACTION);

    }
    else{
        /* Read storedValue on/off value */
        status = Attributes::OnOff::Get(endpoint, &storedValue);
        if (status == EMBER_ZCL_STATUS_SUCCESS) {
        	/* Set actual state to the cluster state that was last persisted */
        	AppTask::Instance().GetPWMDevice().InitiateAction(
        		storedValue ? PWMDevice::ON_ACTION : PWMDevice::OFF_ACTION,
        		static_cast<int32_t>(AppEventType::Lighting), reinterpret_cast<uint8_t *>(&storedValue));
            AppTask::Instance().GetRelayDevice().Set(0, 0, storedValue ? IO_Relay::ON_ACTION : IO_Relay::OFF_ACTION);

        }
    }


	AppTask::Instance().UpdateClusterState();
}
