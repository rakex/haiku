/*
 * Copyright 2013, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Ingo Weinhold <ingo_weinhold@gmx.de>
 */
#ifndef _PACKAGE__PRIVATE__PACKAGE_FS_H_
#define _PACKAGE__PRIVATE__PACKAGE_FS_H_


#include <Drivers.h>


enum PackageFSMountType {
	PACKAGE_FS_MOUNT_TYPE_SYSTEM,
	PACKAGE_FS_MOUNT_TYPE_HOME,
	PACKAGE_FS_MOUNT_TYPE_CUSTOM,

	PACKAGE_FS_MOUNT_TYPE_ENUM_COUNT
};


enum {
	PACKAGE_FS_OPERATION_GET_VOLUME_INFO		= B_DEVICE_OP_CODES_END + 1,
	PACKAGE_FS_OPERATION_GET_PACKAGE_INFOS,
	PACKAGE_FS_OPERATION_CHANGE_ACTIVATION
};


// PACKAGE_FS_OPERATION_GET_VOLUME_INFO

struct PackageFSVolumeInfo {
	PackageFSMountType	mountType;

	// device and node id of the respective package FS root scope (e.g. "/boot"
	// for the three standard volumes)
	dev_t				rootDeviceID;
	ino_t				rootDirectoryID;

	// device and node id of the volume's packages directory
	dev_t				packagesDeviceID;
	ino_t				packagesDirectoryID;
};


// PACKAGE_FS_OPERATION_GET_PACKAGE_INFOS

struct PackageFSPackageInfo {
	// node_ref of the package file
	dev_t							packageDeviceID;
	ino_t							packageNodeID;
};

struct PackageFSGetPackageInfosRequest {
	// Filled in by the FS. packageCount is set to the actual package count,
	// even if it is greater than the array, so the caller can determine whether
	// the array was large enough.
	uint32							packageCount;
	PackageFSPackageInfo			infos[1];
};


// PACKAGE_FS_OPERATION_CHANGE_ACTIVATION

enum PackageFSActivationChangeType {
	PACKAGE_FS_ACTIVATE_PACKAGE,
	PACKAGE_FS_DEACTIVATE_PACKAGE,
	PACKAGE_FS_REACTIVATE_PACKAGE
};

struct PackageFSActivationChangeItem {
	PackageFSActivationChangeType	type;

	// node_ref of the package file
	dev_t							packageDeviceID;
	ino_t							packageNodeID;

	// entry_ref of the package file
	uint32							nameLength;
	dev_t							parentDeviceID;
	ino_t							parentDirectoryID;
	char*							name;
										// must point to a location within the
										// request
};

struct PackageFSActivationChangeRequest {
	uint32							itemCount;
	PackageFSActivationChangeItem	items[0];
};


#endif	// _PACKAGE__PRIVATE__PACKAGE_FS_H_
