/* C:B**************************************************************************
This software is Copyright 2014-2017 Bright Plaza Inc. <drivetrust@drivetrust.com>

This file is part of sedutil.

sedutil is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

sedutil is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with sedutil.  If not, see <http://www.gnu.org/licenses/>.

* C:E********************************************************************** */
#pragma once
#include "os.h"
#include <stdio.h>
#include <iostream>
#pragma warning(push)
#pragma warning(disable : 4091)
#include <Ntddscsi.h>
#pragma warning(pop)
#include <winioctl.h>
#include <vector>
#include "DtaDiskNVMe.h"
#include "DtaEndianFixup.h"
#include "DtaStructures.h"
#include "DtaHexDump.h"

using namespace std;

// STORAGE_PROTOCOL_SPECIFIC_DATA, STORAGE_PROTOCOL_DATA_DESCRIPTOR and
// StorageAdapterProtocolSpecificProperty used to be defined here by hand.
// The Windows 10 SDK (winioctl.h) now provides them.

#define NVME_MAX_LOG_SIZE 4096  // value from random internet search



DtaDiskNVMe::DtaDiskNVMe() {};
void DtaDiskNVMe::init(const char * devref)
{
    LOG(D1) << "Creating DtaDiskNVMe::DtaDiskNVMe() " << devref;

     hDev = CreateFile(devref,
                      GENERIC_WRITE | GENERIC_READ,
                      FILE_SHARE_WRITE | FILE_SHARE_READ,
                      NULL,
                      OPEN_EXISTING,
                      0,
                      NULL);
    if (INVALID_HANDLE_VALUE == hDev) 
		return;
    else 
        isOpen = TRUE;
}

/* Opal commands are sent as SCSI SECURITY PROTOCOL IN / OUT through
 * IOCTL_SCSI_PASS_THROUGH_DIRECT. The Windows NVMe driver (stornvme)
 * translates these into NVMe Security Receive / Security Send.
 * Ported from the Drive Trust Alliance sedutil (windows/DtaDiskNVMe.cpp).
 */
uint8_t DtaDiskNVMe::sendCmd(ATACOMMAND cmd, uint8_t protocol, uint16_t comID,
                        void * buffer, uint32_t bufferlen)
{
	LOG(D1) << "Entering DtaDiskNVMe::sendCmd";
	DWORD bytesReturned = 0;
	if (!isOpen) {
		LOG(E) << "Device open failed";
		return DTAERROR_OPEN_ERR;
	}
	SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER sptdwb;
	memset(&sptdwb, 0, sizeof(sptdwb));
	sptdwb.sptd.Length = sizeof(SCSI_PASS_THROUGH_DIRECT);
	sptdwb.sptd.PathId = 0;
	sptdwb.sptd.TargetId = 1;
	sptdwb.sptd.Lun = 0;
	sptdwb.sptd.SenseInfoLength = sizeof(sptdwb.ucSenseBuf);
	sptdwb.sptd.SenseInfoOffset =
		offsetof(SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER, ucSenseBuf);
	sptdwb.sptd.DataTransferLength = bufferlen;
	sptdwb.sptd.TimeOutValue = 20; // seconds; some Opal operations are slow
	sptdwb.sptd.DataBuffer = buffer;
	switch (cmd) {
	case IF_RECV: // SECURITY PROTOCOL IN
	case IF_SEND: // SECURITY PROTOCOL OUT
		sptdwb.sptd.Cdb[0] = (IF_RECV == cmd) ? 0xa2 : 0xb5;
		sptdwb.sptd.Cdb[1] = protocol;
		sptdwb.sptd.Cdb[2] = (UCHAR)((comID & 0xff00) >> 8);
		sptdwb.sptd.Cdb[3] = (UCHAR)(comID & 0x00ff);
		sptdwb.sptd.Cdb[6] = (UCHAR)((bufferlen & 0xff000000) >> 24);
		sptdwb.sptd.Cdb[7] = (UCHAR)((bufferlen & 0x00ff0000) >> 16);
		sptdwb.sptd.Cdb[8] = (UCHAR)((bufferlen & 0x0000ff00) >> 8);
		sptdwb.sptd.Cdb[9] = (UCHAR)(bufferlen & 0x000000ff);
		sptdwb.sptd.CdbLength = 12;
		sptdwb.sptd.DataIn = (IF_RECV == cmd) ? SCSI_IOCTL_DATA_IN : SCSI_IOCTL_DATA_OUT;
		break;
	default:
		LOG(E) << "DtaDiskNVMe::sendCmd invalid command " << (uint16_t)cmd;
		return DTAERROR_COMMAND_ERROR;
	}
	BOOL iorc = DeviceIoControl(hDev,
		IOCTL_SCSI_PASS_THROUGH_DIRECT,
		&sptdwb, sizeof(sptdwb),
		&sptdwb, sizeof(sptdwb),
		&bytesReturned,
		NULL);
	if (iorc && (0 == sptdwb.sptd.ScsiStatus)) {
		return 0;
	}
	LOG(D1) << "DtaDiskNVMe::sendCmd failed: GetLastError() = " << GetLastError()
		<< " ScsiStatus = " << (uint16_t)sptdwb.sptd.ScsiStatus;
	return DTAERROR_COMMAND_ERROR;
}

/** adds the IDENTIFY information to the disk_info structure
 *
 * Reads the NVMe Identify Controller data. The serial number read here is
 * the salt for password hashing, so it must match what the Linux PBA reads
 * (Identify Controller bytes 4-23), or the same password would produce a
 * different key on Windows.
 */
void DtaDiskNVMe::identify(OPAL_DiskInfo& disk_info)
{
	LOG(D1) << "Entering DtaDiskNVMe::identify()";
	DWORD dwReturned = 0;
	ULONG bufferLength = FIELD_OFFSET(STORAGE_PROPERTY_QUERY, AdditionalParameters)
		+ sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) + NVME_MAX_LOG_SIZE;
	PVOID buffer = malloc(bufferLength);
	if (buffer == NULL) {
		LOG(E) << "DtaDiskNVMe::identify: allocate buffer failed";
		return;
	}
	ZeroMemory(buffer, bufferLength);

	// The protocol-specific request sits in STORAGE_PROPERTY_QUERY.AdditionalParameters.
	PSTORAGE_PROPERTY_QUERY query = (PSTORAGE_PROPERTY_QUERY)buffer;
	PSTORAGE_PROTOCOL_SPECIFIC_DATA protocolData =
		(PSTORAGE_PROTOCOL_SPECIFIC_DATA)query->AdditionalParameters;
	query->PropertyId = StorageAdapterProtocolSpecificProperty;
	query->QueryType = PropertyStandardQuery;
	protocolData->ProtocolType = ProtocolTypeNvme;
	protocolData->DataType = NVMeDataTypeIdentify;
	protocolData->ProtocolDataRequestValue = 1; // NVME_IDENTIFY_CNS_CONTROLLER
	protocolData->ProtocolDataRequestSubValue = 0;
	protocolData->ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
	protocolData->ProtocolDataLength = NVME_MAX_LOG_SIZE;

	BOOL iorc = DeviceIoControl(hDev, IOCTL_STORAGE_QUERY_PROPERTY,
		buffer, bufferLength, buffer, bufferLength, &dwReturned, NULL);

	// On return the buffer holds a STORAGE_PROTOCOL_DATA_DESCRIPTOR.
	PSTORAGE_PROTOCOL_DATA_DESCRIPTOR descr = (PSTORAGE_PROTOCOL_DATA_DESCRIPTOR)buffer;
	PSTORAGE_PROTOCOL_SPECIFIC_DATA returned = &descr->ProtocolSpecificData;
	if (!iorc ||
		(descr->Version != sizeof(STORAGE_PROTOCOL_DATA_DESCRIPTOR)) ||
		(descr->Size != sizeof(STORAGE_PROTOCOL_DATA_DESCRIPTOR)) ||
		(returned->ProtocolDataOffset < sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA)) ||
		(returned->ProtocolDataLength < 4096)) {
		LOG(D) << "NVMe Identify Controller failed: GetLastError() = " << GetLastError();
		disk_info.devType = DEVICE_TYPE_OTHER;
		free(buffer);
		return;
	}
	disk_info.devType = DEVICE_TYPE_NVME;
	UINT8 *results = (UINT8 *)returned + returned->ProtocolDataOffset;
	results += 4; // skip PCI vendor and subsystem vendor IDs
	memcpy(disk_info.serialNum, results, sizeof(disk_info.serialNum));
	results += sizeof(disk_info.serialNum);
	memcpy(disk_info.modelNum, results, sizeof(disk_info.modelNum));
	results += sizeof(disk_info.modelNum);
	memcpy(disk_info.firmwareRev, results, sizeof(disk_info.firmwareRev));
	free(buffer);
	return;
}

/** Close the filehandle so this object can be delete. */
DtaDiskNVMe::~DtaDiskNVMe()
{
    LOG(D1) << "Destroying DtaDiskNVMe";
    CloseHandle(hDev);

}
