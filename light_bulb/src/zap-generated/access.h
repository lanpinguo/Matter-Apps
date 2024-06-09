/*
 *
 *    Copyright (c) 2022 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

// THIS FILE IS GENERATED BY ZAP

// Prevent multiple inclusion
#pragma once

#include <app/util/privilege-storage.h>

// Prevent changing generated format
// clang-format off

////////////////////////////////////////////////////////////////////////////////

// Parallel array data (*cluster*, attribute, privilege) for read attribute
#define GENERATED_ACCESS_READ_ATTRIBUTE__CLUSTER { \
    /* Cluster: On/Off, Attribute: StartUpOnOff, Privilege: view */ \
    /* Cluster: Level Control, Attribute: StartUpCurrentLevel, Privilege: view */ \
    0x0000001F, /* Cluster: Access Control, Attribute: ACL, Privilege: administer */ \
    0x0000001F, /* Cluster: Access Control, Attribute: Extension, Privilege: administer */ \
    /* Cluster: Access Control, Attribute: SubjectsPerAccessControlEntry, Privilege: view */ \
    /* Cluster: Access Control, Attribute: TargetsPerAccessControlEntry, Privilege: view */ \
    /* Cluster: Access Control, Attribute: AccessControlEntriesPerFabric, Privilege: view */ \
    /* Cluster: Basic Information, Attribute: NodeLabel, Privilege: view */ \
    /* Cluster: Basic Information, Attribute: Location, Privilege: view */ \
    /* Cluster: General Commissioning, Attribute: Breadcrumb, Privilege: view */ \
    0x00000031, /* Cluster: Network Commissioning, Attribute: MaxNetworks, Privilege: administer */ \
    0x00000031, /* Cluster: Network Commissioning, Attribute: Networks, Privilege: administer */ \
    /* Cluster: Network Commissioning, Attribute: InterfaceEnabled, Privilege: view */ \
    0x00000031, /* Cluster: Network Commissioning, Attribute: LastNetworkingStatus, Privilege: administer */ \
    0x00000031, /* Cluster: Network Commissioning, Attribute: LastNetworkID, Privilege: administer */ \
    0x00000031, /* Cluster: Network Commissioning, Attribute: LastConnectErrorValue, Privilege: administer */ \
    0x0000003E, /* Cluster: Operational Credentials, Attribute: NOCs, Privilege: administer */ \
    /* Cluster: Group Key Management, Attribute: GroupKeyMap, Privilege: view */ \
}

// Parallel array data (cluster, *attribute*, privilege) for read attribute
#define GENERATED_ACCESS_READ_ATTRIBUTE__ATTRIBUTE { \
    /* Cluster: On/Off, Attribute: StartUpOnOff, Privilege: view */ \
    /* Cluster: Level Control, Attribute: StartUpCurrentLevel, Privilege: view */ \
    0x00000000, /* Cluster: Access Control, Attribute: ACL, Privilege: administer */ \
    0x00000001, /* Cluster: Access Control, Attribute: Extension, Privilege: administer */ \
    /* Cluster: Access Control, Attribute: SubjectsPerAccessControlEntry, Privilege: view */ \
    /* Cluster: Access Control, Attribute: TargetsPerAccessControlEntry, Privilege: view */ \
    /* Cluster: Access Control, Attribute: AccessControlEntriesPerFabric, Privilege: view */ \
    /* Cluster: Basic Information, Attribute: NodeLabel, Privilege: view */ \
    /* Cluster: Basic Information, Attribute: Location, Privilege: view */ \
    /* Cluster: General Commissioning, Attribute: Breadcrumb, Privilege: view */ \
    0x00000000, /* Cluster: Network Commissioning, Attribute: MaxNetworks, Privilege: administer */ \
    0x00000001, /* Cluster: Network Commissioning, Attribute: Networks, Privilege: administer */ \
    /* Cluster: Network Commissioning, Attribute: InterfaceEnabled, Privilege: view */ \
    0x00000005, /* Cluster: Network Commissioning, Attribute: LastNetworkingStatus, Privilege: administer */ \
    0x00000006, /* Cluster: Network Commissioning, Attribute: LastNetworkID, Privilege: administer */ \
    0x00000007, /* Cluster: Network Commissioning, Attribute: LastConnectErrorValue, Privilege: administer */ \
    0x00000000, /* Cluster: Operational Credentials, Attribute: NOCs, Privilege: administer */ \
    /* Cluster: Group Key Management, Attribute: GroupKeyMap, Privilege: view */ \
}

// Parallel array data (cluster, attribute, *privilege*) for read attribute
#define GENERATED_ACCESS_READ_ATTRIBUTE__PRIVILEGE { \
    /* Cluster: On/Off, Attribute: StartUpOnOff, Privilege: view */ \
    /* Cluster: Level Control, Attribute: StartUpCurrentLevel, Privilege: view */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Access Control, Attribute: ACL, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Access Control, Attribute: Extension, Privilege: administer */ \
    /* Cluster: Access Control, Attribute: SubjectsPerAccessControlEntry, Privilege: view */ \
    /* Cluster: Access Control, Attribute: TargetsPerAccessControlEntry, Privilege: view */ \
    /* Cluster: Access Control, Attribute: AccessControlEntriesPerFabric, Privilege: view */ \
    /* Cluster: Basic Information, Attribute: NodeLabel, Privilege: view */ \
    /* Cluster: Basic Information, Attribute: Location, Privilege: view */ \
    /* Cluster: General Commissioning, Attribute: Breadcrumb, Privilege: view */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Network Commissioning, Attribute: MaxNetworks, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Network Commissioning, Attribute: Networks, Privilege: administer */ \
    /* Cluster: Network Commissioning, Attribute: InterfaceEnabled, Privilege: view */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Network Commissioning, Attribute: LastNetworkingStatus, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Network Commissioning, Attribute: LastNetworkID, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Network Commissioning, Attribute: LastConnectErrorValue, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Operational Credentials, Attribute: NOCs, Privilege: administer */ \
    /* Cluster: Group Key Management, Attribute: GroupKeyMap, Privilege: view */ \
}

////////////////////////////////////////////////////////////////////////////////

// Parallel array data (*cluster*, attribute, privilege) for write attribute
#define GENERATED_ACCESS_WRITE_ATTRIBUTE__CLUSTER { \
    0x00000006, /* Cluster: On/Off, Attribute: StartUpOnOff, Privilege: manage */ \
    0x00000008, /* Cluster: Level Control, Attribute: StartUpCurrentLevel, Privilege: manage */ \
    0x0000001F, /* Cluster: Access Control, Attribute: ACL, Privilege: administer */ \
    0x0000001F, /* Cluster: Access Control, Attribute: Extension, Privilege: administer */ \
    0x00000028, /* Cluster: Basic Information, Attribute: NodeLabel, Privilege: manage */ \
    0x00000028, /* Cluster: Basic Information, Attribute: Location, Privilege: administer */ \
    0x00000030, /* Cluster: General Commissioning, Attribute: Breadcrumb, Privilege: administer */ \
    0x00000031, /* Cluster: Network Commissioning, Attribute: InterfaceEnabled, Privilege: administer */ \
    0x0000003F, /* Cluster: Group Key Management, Attribute: GroupKeyMap, Privilege: manage */ \
}

// Parallel array data (cluster, *attribute*, privilege) for write attribute
#define GENERATED_ACCESS_WRITE_ATTRIBUTE__ATTRIBUTE { \
    0x00004003, /* Cluster: On/Off, Attribute: StartUpOnOff, Privilege: manage */ \
    0x00004000, /* Cluster: Level Control, Attribute: StartUpCurrentLevel, Privilege: manage */ \
    0x00000000, /* Cluster: Access Control, Attribute: ACL, Privilege: administer */ \
    0x00000001, /* Cluster: Access Control, Attribute: Extension, Privilege: administer */ \
    0x00000005, /* Cluster: Basic Information, Attribute: NodeLabel, Privilege: manage */ \
    0x00000006, /* Cluster: Basic Information, Attribute: Location, Privilege: administer */ \
    0x00000000, /* Cluster: General Commissioning, Attribute: Breadcrumb, Privilege: administer */ \
    0x00000004, /* Cluster: Network Commissioning, Attribute: InterfaceEnabled, Privilege: administer */ \
    0x00000000, /* Cluster: Group Key Management, Attribute: GroupKeyMap, Privilege: manage */ \
}

// Parallel array data (cluster, attribute, *privilege*) for write attribute
#define GENERATED_ACCESS_WRITE_ATTRIBUTE__PRIVILEGE { \
    kMatterAccessPrivilegeManage, /* Cluster: On/Off, Attribute: StartUpOnOff, Privilege: manage */ \
    kMatterAccessPrivilegeManage, /* Cluster: Level Control, Attribute: StartUpCurrentLevel, Privilege: manage */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Access Control, Attribute: ACL, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Access Control, Attribute: Extension, Privilege: administer */ \
    kMatterAccessPrivilegeManage, /* Cluster: Basic Information, Attribute: NodeLabel, Privilege: manage */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Basic Information, Attribute: Location, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: General Commissioning, Attribute: Breadcrumb, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Network Commissioning, Attribute: InterfaceEnabled, Privilege: administer */ \
    kMatterAccessPrivilegeManage, /* Cluster: Group Key Management, Attribute: GroupKeyMap, Privilege: manage */ \
}

////////////////////////////////////////////////////////////////////////////////

// Parallel array data (*cluster*, command, privilege) for invoke command
#define GENERATED_ACCESS_INVOKE_COMMAND__CLUSTER { \
    0x00000003, /* Cluster: Identify, Command: Identify, Privilege: manage */ \
    0x00000003, /* Cluster: Identify, Command: TriggerEffect, Privilege: manage */ \
    0x00000004, /* Cluster: Groups, Command: AddGroup, Privilege: manage */ \
    0x00000004, /* Cluster: Groups, Command: RemoveGroup, Privilege: manage */ \
    0x00000004, /* Cluster: Groups, Command: RemoveAllGroups, Privilege: manage */ \
    0x00000004, /* Cluster: Groups, Command: AddGroupIfIdentifying, Privilege: manage */ \
    0x00000030, /* Cluster: General Commissioning, Command: ArmFailSafe, Privilege: administer */ \
    0x00000030, /* Cluster: General Commissioning, Command: SetRegulatoryConfig, Privilege: administer */ \
    0x00000030, /* Cluster: General Commissioning, Command: CommissioningComplete, Privilege: administer */ \
    0x00000031, /* Cluster: Network Commissioning, Command: ScanNetworks, Privilege: administer */ \
    0x00000031, /* Cluster: Network Commissioning, Command: AddOrUpdateWiFiNetwork, Privilege: administer */ \
    0x00000031, /* Cluster: Network Commissioning, Command: AddOrUpdateThreadNetwork, Privilege: administer */ \
    0x00000031, /* Cluster: Network Commissioning, Command: RemoveNetwork, Privilege: administer */ \
    0x00000031, /* Cluster: Network Commissioning, Command: ConnectNetwork, Privilege: administer */ \
    0x00000031, /* Cluster: Network Commissioning, Command: ReorderNetwork, Privilege: administer */ \
    0x00000033, /* Cluster: General Diagnostics, Command: TestEventTrigger, Privilege: manage */ \
    0x0000003C, /* Cluster: Administrator Commissioning, Command: OpenCommissioningWindow, Privilege: administer */ \
    0x0000003C, /* Cluster: Administrator Commissioning, Command: OpenBasicCommissioningWindow, Privilege: administer */ \
    0x0000003C, /* Cluster: Administrator Commissioning, Command: RevokeCommissioning, Privilege: administer */ \
    0x0000003E, /* Cluster: Operational Credentials, Command: AttestationRequest, Privilege: administer */ \
    0x0000003E, /* Cluster: Operational Credentials, Command: CertificateChainRequest, Privilege: administer */ \
    0x0000003E, /* Cluster: Operational Credentials, Command: CSRRequest, Privilege: administer */ \
    0x0000003E, /* Cluster: Operational Credentials, Command: AddNOC, Privilege: administer */ \
    0x0000003E, /* Cluster: Operational Credentials, Command: UpdateNOC, Privilege: administer */ \
    0x0000003E, /* Cluster: Operational Credentials, Command: UpdateFabricLabel, Privilege: administer */ \
    0x0000003E, /* Cluster: Operational Credentials, Command: RemoveFabric, Privilege: administer */ \
    0x0000003E, /* Cluster: Operational Credentials, Command: AddTrustedRootCertificate, Privilege: administer */ \
    0x0000003F, /* Cluster: Group Key Management, Command: KeySetWrite, Privilege: administer */ \
    0x0000003F, /* Cluster: Group Key Management, Command: KeySetRead, Privilege: administer */ \
    0x0000003F, /* Cluster: Group Key Management, Command: KeySetRemove, Privilege: administer */ \
    0x0000003F, /* Cluster: Group Key Management, Command: KeySetReadAllIndices, Privilege: administer */ \
}

// Parallel array data (cluster, *command*, privilege) for invoke command
#define GENERATED_ACCESS_INVOKE_COMMAND__COMMAND { \
    0x00000000, /* Cluster: Identify, Command: Identify, Privilege: manage */ \
    0x00000040, /* Cluster: Identify, Command: TriggerEffect, Privilege: manage */ \
    0x00000000, /* Cluster: Groups, Command: AddGroup, Privilege: manage */ \
    0x00000003, /* Cluster: Groups, Command: RemoveGroup, Privilege: manage */ \
    0x00000004, /* Cluster: Groups, Command: RemoveAllGroups, Privilege: manage */ \
    0x00000005, /* Cluster: Groups, Command: AddGroupIfIdentifying, Privilege: manage */ \
    0x00000000, /* Cluster: General Commissioning, Command: ArmFailSafe, Privilege: administer */ \
    0x00000002, /* Cluster: General Commissioning, Command: SetRegulatoryConfig, Privilege: administer */ \
    0x00000004, /* Cluster: General Commissioning, Command: CommissioningComplete, Privilege: administer */ \
    0x00000000, /* Cluster: Network Commissioning, Command: ScanNetworks, Privilege: administer */ \
    0x00000002, /* Cluster: Network Commissioning, Command: AddOrUpdateWiFiNetwork, Privilege: administer */ \
    0x00000003, /* Cluster: Network Commissioning, Command: AddOrUpdateThreadNetwork, Privilege: administer */ \
    0x00000004, /* Cluster: Network Commissioning, Command: RemoveNetwork, Privilege: administer */ \
    0x00000006, /* Cluster: Network Commissioning, Command: ConnectNetwork, Privilege: administer */ \
    0x00000008, /* Cluster: Network Commissioning, Command: ReorderNetwork, Privilege: administer */ \
    0x00000000, /* Cluster: General Diagnostics, Command: TestEventTrigger, Privilege: manage */ \
    0x00000000, /* Cluster: Administrator Commissioning, Command: OpenCommissioningWindow, Privilege: administer */ \
    0x00000001, /* Cluster: Administrator Commissioning, Command: OpenBasicCommissioningWindow, Privilege: administer */ \
    0x00000002, /* Cluster: Administrator Commissioning, Command: RevokeCommissioning, Privilege: administer */ \
    0x00000000, /* Cluster: Operational Credentials, Command: AttestationRequest, Privilege: administer */ \
    0x00000002, /* Cluster: Operational Credentials, Command: CertificateChainRequest, Privilege: administer */ \
    0x00000004, /* Cluster: Operational Credentials, Command: CSRRequest, Privilege: administer */ \
    0x00000006, /* Cluster: Operational Credentials, Command: AddNOC, Privilege: administer */ \
    0x00000007, /* Cluster: Operational Credentials, Command: UpdateNOC, Privilege: administer */ \
    0x00000009, /* Cluster: Operational Credentials, Command: UpdateFabricLabel, Privilege: administer */ \
    0x0000000A, /* Cluster: Operational Credentials, Command: RemoveFabric, Privilege: administer */ \
    0x0000000B, /* Cluster: Operational Credentials, Command: AddTrustedRootCertificate, Privilege: administer */ \
    0x00000000, /* Cluster: Group Key Management, Command: KeySetWrite, Privilege: administer */ \
    0x00000001, /* Cluster: Group Key Management, Command: KeySetRead, Privilege: administer */ \
    0x00000003, /* Cluster: Group Key Management, Command: KeySetRemove, Privilege: administer */ \
    0x00000004, /* Cluster: Group Key Management, Command: KeySetReadAllIndices, Privilege: administer */ \
}

// Parallel array data (cluster, command, *privilege*) for invoke command
#define GENERATED_ACCESS_INVOKE_COMMAND__PRIVILEGE { \
    kMatterAccessPrivilegeManage, /* Cluster: Identify, Command: Identify, Privilege: manage */ \
    kMatterAccessPrivilegeManage, /* Cluster: Identify, Command: TriggerEffect, Privilege: manage */ \
    kMatterAccessPrivilegeManage, /* Cluster: Groups, Command: AddGroup, Privilege: manage */ \
    kMatterAccessPrivilegeManage, /* Cluster: Groups, Command: RemoveGroup, Privilege: manage */ \
    kMatterAccessPrivilegeManage, /* Cluster: Groups, Command: RemoveAllGroups, Privilege: manage */ \
    kMatterAccessPrivilegeManage, /* Cluster: Groups, Command: AddGroupIfIdentifying, Privilege: manage */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: General Commissioning, Command: ArmFailSafe, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: General Commissioning, Command: SetRegulatoryConfig, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: General Commissioning, Command: CommissioningComplete, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Network Commissioning, Command: ScanNetworks, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Network Commissioning, Command: AddOrUpdateWiFiNetwork, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Network Commissioning, Command: AddOrUpdateThreadNetwork, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Network Commissioning, Command: RemoveNetwork, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Network Commissioning, Command: ConnectNetwork, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Network Commissioning, Command: ReorderNetwork, Privilege: administer */ \
    kMatterAccessPrivilegeManage, /* Cluster: General Diagnostics, Command: TestEventTrigger, Privilege: manage */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Administrator Commissioning, Command: OpenCommissioningWindow, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Administrator Commissioning, Command: OpenBasicCommissioningWindow, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Administrator Commissioning, Command: RevokeCommissioning, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Operational Credentials, Command: AttestationRequest, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Operational Credentials, Command: CertificateChainRequest, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Operational Credentials, Command: CSRRequest, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Operational Credentials, Command: AddNOC, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Operational Credentials, Command: UpdateNOC, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Operational Credentials, Command: UpdateFabricLabel, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Operational Credentials, Command: RemoveFabric, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Operational Credentials, Command: AddTrustedRootCertificate, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Group Key Management, Command: KeySetWrite, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Group Key Management, Command: KeySetRead, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Group Key Management, Command: KeySetRemove, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Group Key Management, Command: KeySetReadAllIndices, Privilege: administer */ \
}

////////////////////////////////////////////////////////////////////////////////

// Parallel array data (*cluster*, event, privilege) for read event
#define GENERATED_ACCESS_READ_EVENT__CLUSTER { \
    0x0000001F, /* Cluster: Access Control, Event: AccessControlEntryChanged, Privilege: administer */ \
    0x0000001F, /* Cluster: Access Control, Event: AccessControlExtensionChanged, Privilege: administer */ \
}

// Parallel array data (cluster, *event*, privilege) for read event
#define GENERATED_ACCESS_READ_EVENT__EVENT { \
    0x00000000, /* Cluster: Access Control, Event: AccessControlEntryChanged, Privilege: administer */ \
    0x00000001, /* Cluster: Access Control, Event: AccessControlExtensionChanged, Privilege: administer */ \
}

// Parallel array data (cluster, event, *privilege*) for read event
#define GENERATED_ACCESS_READ_EVENT__PRIVILEGE { \
    kMatterAccessPrivilegeAdminister, /* Cluster: Access Control, Event: AccessControlEntryChanged, Privilege: administer */ \
    kMatterAccessPrivilegeAdminister, /* Cluster: Access Control, Event: AccessControlExtensionChanged, Privilege: administer */ \
}

////////////////////////////////////////////////////////////////////////////////

// clang-format on
