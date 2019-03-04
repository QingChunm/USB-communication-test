// 44.cpp : �������̨Ӧ�ó������ڵ㡣
//

#include "stdafx.h"
#include <rpc.h>
#include "devpkey.h"
#include "Misc\stdafx.h"
#include "ScsiAcc.h"
#include <cstddef>
#include <winioctl.h>
#include <Ntddscsi.h>
#include "..\3rd\include\spti\spti.h"
#include "cfgmgr32.h"
#include "export.h"
#include "DeviceManager.h"
#include "rpcdce.h"
#include <initguid.h>
#include <Wiaintfc.h>
#include <devguid.h>
#include <algorithm>
#include <vector>



#include "../Data/AX327X.h"


//#define     V_ID        0x1908
//#define     P_ID        0x3283
//#define     V_ID        0x0403
//#define     P_ID        0x6001
//#define     V_ID        0x0BDA
//#define     P_ID        0x0177
#define     V_ID        0x046D
#define     P_ID        0xC534

DEVPROPKEY DEVPKEY_Device_HardwareId = { { 0xA45C254E, 0xDF1C, 0x4EFD, 0x8020, 0x67D146A850E0 }, 3 };
DEVPROPKEY DEVPKEY_Device_ClassGui = { { 0xA45C254E, 0xDF1C, 0x4EFD, 0x8020, 0x67D146A850E0 }, 10 };
DEVPROPKEY DEVPKEY_Device_DeviceDes = { { 0xA45C254E, 0xDF1C, 0x4EFD, 0x8020, 0x67D146A850E0 }, 2 };
DEVPROPKEY DEVPKEY_Device_LocationInf = { { 0xA45C254E, 0xDF1C, 0x4EFD, 0x8020, 0x67D146A850E0 }, 15 };
DEVPROPKEY DEVPKEY_Device_FriendlyNam = { { 0xA45C254E, 0xDF1C, 0x4EFD, 0x8020, 0x67D146A850E0 }, 14 };



VOID GetDevicePropertiesCfgmgr32(VOID);
void GetDevicePropertySpecificDeviceCfgmgr32();

std::wstring _GetDeviceHardwareId(SP_DEVINFO_DATA devInfoData, HDEVINFO deviceInfoSet)
{
	DEVPROPTYPE propType = 0;
	DWORD bufferSize = 0;
	std::wstring devHardwareIds = L"";
	devHardwareIds.resize(1000);
	//�����豸ʵ������
	SetupDiGetDeviceProperty(deviceInfoSet, &devInfoData, &DEVPKEY_Device_HardwareId, &propType,
		reinterpret_cast<PBYTE>(&devHardwareIds[0]), devHardwareIds.size(), &bufferSize, 0);

	return devHardwareIds;
}

std::map<std::wstring, std::wstring>m_SupportedHardwareSpecificsMap = {
	{ L"BuildWinDiskInterface", L"Buildwin" },
	{ L"BuildWin", L"VID_1908" },
	{ L"iPhoneForTest", L"VID_05AC&PID_12A8" }
};

bool _IsSupportedDevice(SP_DEVINFO_DATA devInfoData, HDEVINFO deviceInfoSet)
{
	std::wstring devHardwareId = _GetDeviceHardwareId(devInfoData, deviceInfoSet);
	int a = 1;
	for each (auto item in m_SupportedHardwareSpecificsMap)
	{
		if (devHardwareId.find(item.second) != std::wstring::npos)
		{
			return true;
		}
	}

	return false;
}
std::map<std::wstring, AX32XXDevice*> m_Ax32xxDevMap;
std::function<void(int event,
	const wchar_t* devLocation, const wchar_t* devModel, const wchar_t* uvcInterfaceName)> m_deviceChangeNotifyFunc;

AX32XXDevice* _AddDevice(const wchar_t* devSymbolicLink, SP_DEVINFO_DATA devInfoData)
{
	wchar_t uvcInterfaceName[MAX_PATH] = L"";

	DEVINST parentDevInst = 0;
	DEVINST tmpDevInst = devInfoData.DevInst;

	CONFIGRET result = CR_FAILURE;
	DEVPROPTYPE tmpPropType = 0;

	std::wstring parentDesc;
	unsigned long tmpBufferSize = 500;
	parentDesc.resize(tmpBufferSize);

	do
	{
		result = CM_Get_Parent(&parentDevInst, tmpDevInst, NULL);
		if (result != CR_SUCCESS)
		{
			break;
		}

		GUID parentGuid;
		(parentDevInst, &DEVPKEY_Device_ClassGui, &tmpPropType, (PBYTE)&parentGuid, &tmpBufferSize, 0);
		if (parentGuid != GUID_DEVCLASS_USB)
		{
			break;
		}

		CM_Get_DevNode_Property(parentDevInst, &DEVPKEY_Device_DeviceDes, &tmpPropType, (PBYTE)&parentDesc[0], &tmpBufferSize, 0);
		parentDesc.resize(tmpBufferSize / sizeof(wchar_t) - 1);

		CM_Get_DevNode_Property(parentDevInst, &DEVPKEY_Device_DeviceDes, &tmpPropType, (PBYTE)&parentDesc[0], &tmpBufferSize, 0);
		if (parentDesc == L"USB Composite Device")
		{
			break;
		}
		tmpDevInst = parentDevInst;
	} while (true);


	wchar_t devLocation[MAX_PATH] = { 0 };
	tmpBufferSize = sizeof(devLocation);
	result = CM_Get_DevNode_Property(parentDevInst, &DEVPKEY_Device_LocationInf, &tmpPropType, (PBYTE)devLocation, &tmpBufferSize, 0);
	if (result != CR_SUCCESS || _tcslen(devLocation) == 0)
	{
		return nullptr;
	}

	DEVINST childDevInst = NULL;
	CM_Get_Child(&childDevInst, parentDevInst, NULL);

	if (childDevInst == 0)
	{
		return nullptr;
	}

	while (true)
	{
		GUID guid, guid2;

		tmpBufferSize = sizeof(guid);
		result = CM_Get_DevNode_Property(childDevInst, &DEVPKEY_Device_ClassGui, &tmpPropType, (PBYTE)&guid,
			(PULONG)&tmpBufferSize, 0);

		// win10��cammera
		auto uuid2Str = L"CA3E7AB9-B4C3-4AE6-8251-579EF933890F";

		UuidFromString((RPC_WSTR)uuid2Str, &guid2);

		// image�ӿڣ�����UVC
		if (guid == GUID_DEVINTERFACE_IMAGE || guid == guid2)
		{
			tmpBufferSize = sizeof(uvcInterfaceName);
			CM_Get_DevNode_Property(childDevInst, &DEVPKEY_Device_FriendlyNam, &tmpPropType, (PBYTE)uvcInterfaceName,
				(PULONG)&tmpBufferSize, 0);
		}

		DEVINST nextChildDevInst = 0;
		if (CM_Get_Sibling(&nextChildDevInst, childDevInst, 0) == CR_SUCCESS)
		{
			childDevInst = nextChildDevInst;
		}
		else
		{
			break;
		}
	}

	if (_tcslen(uvcInterfaceName) == 0)
	{
		return nullptr;
	}

	AX32XXDevice* aX32XXDevice = new AX327X(devSymbolicLink, uvcInterfaceName, devLocation);
	aX32XXDevice->InitDebugParam();

	std::wstring devicePath = devSymbolicLink;
	std::transform(devicePath.begin(), devicePath.end(), devicePath.begin(), ::toupper);

	m_Ax32xxDevMap.insert(std::make_pair(devicePath, aX32XXDevice));

	m_deviceChangeNotifyFunc((int)DeviceEvent::Arrival, devLocation, L"AX327X", uvcInterfaceName);

	return aX32XXDevice;
}
int main(int argc, char* argv[])
{
	//��������һ���������������б�������豸��Ϣ���豸��Ϣ�����,������ʵ����һ��ָ��
	HDEVINFO hardware_dev_info_set = SetupDiGetClassDevs(&GUID_DEVINTERFACE_DISK, NULL, NULL, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);

	int devidx = 0;
	while (INVALID_HANDLE_VALUE != hardware_dev_info_set)
	{
		SP_DEVINFO_DATA devInfoData;
		devInfoData.cbSize = sizeof(devInfoData);
		//�� 0 ����豸��ʼ���� �豸��Ϣ�� �����ص���Ϣ����devInfoData��
		if (SetupDiEnumDeviceInfo(hardware_dev_info_set, devidx, &devInfoData))//
		{
			if (_IsSupportedDevice(devInfoData, hardware_dev_info_set))
			{
				SP_DEVICE_INTERFACE_DATA devInterfaceData;
				devInterfaceData.cbSize = sizeof(devInterfaceData);

				SetupDiEnumDeviceInterfaces(hardware_dev_info_set, nullptr, &GUID_DEVINTERFACE_DISK, devidx, &devInterfaceData);

				DWORD interfaceDetailSize = 0;
				SetupDiGetInterfaceDeviceDetail(hardware_dev_info_set, &devInterfaceData, nullptr, interfaceDetailSize,
					&interfaceDetailSize, nullptr);

				PSP_DEVICE_INTERFACE_DETAIL_DATA pDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)new char[interfaceDetailSize];
				pDetailData->cbSize = sizeof(SP_INTERFACE_DEVICE_DETAIL_DATA);
				//�豸·�� pDetailData->DevicePath 
				SetupDiGetInterfaceDeviceDetail(hardware_dev_info_set, &devInterfaceData, pDetailData, interfaceDetailSize,
					&interfaceDetailSize, nullptr);

				auto device = _AddDevice(pDetailData->DevicePath, devInfoData);
			}
			devidx++;
			continue;
		}
		else
		{
			break;
		}
	}

	if (INVALID_HANDLE_VALUE != hardware_dev_info_set)
	{
		SetupDiDestroyDeviceInfoList(hardware_dev_info_set);
	}


	GetDevicePropertySpecificDeviceCfgmgr32();
//	GetDevicePropertiesCfgmgr32();
	return 0;
}


VOID GetDevicePropertiesCfgmgr32(VOID)
{
	CONFIGRET cr = CR_SUCCESS;
	PWSTR DeviceList = NULL;
	ULONG DeviceListLength = 0;
	PWSTR CurrentDevice;
	DEVINST Devinst;
	WCHAR DeviceDesc[2048];
	DEVPROPTYPE PropertyType;
	ULONG PropertySize;
	DWORD Index = 0;
	DEVPROPKEY DEVPKEY_Device_DeviceDesc;
	cr = CM_Get_Device_ID_List_Size(
		&DeviceListLength,				//���ձ�ʾ���軺������С��ֵ�����ַ�Ϊ��λ��
		NULL,							//�������ṩ��ָ���ַ�����ָ�룬ָ�������豸ʵ����ʶ�����Ӽ�����NULL��
		CM_GETIDLIST_FILTER_PRESENT);	//���ص��б������ϵͳ�ϵ�ǰ���ڵ��豸ʵ��

	if (cr != CR_SUCCESS)
	{
		goto Exit;
	}

	DeviceList = (PWSTR)HeapAlloc(GetProcessHeap(),//����ѿռ�
		HEAP_ZERO_MEMORY,
		DeviceListLength * sizeof(WCHAR));

	if (DeviceList == NULL) {
		goto Exit;
	}

	cr = CM_Get_Device_ID_List(
		NULL,							//�豸ʵ����ʶ���Ӽ� �� NULL
		DeviceList,						//���ڽ���һ����NULL��β���豸ʵ����ʶ���ַ����Ļ������ĵ�ַ(����)��locate֮ǰӦ����CM_Get_Device_ID_List_Size��ȡ����Ļ�������С
		DeviceListLength,				//����CM_Get_Device_ID_List_Size��ȡ����Ļ�������С
		CM_GETIDLIST_FILTER_PRESENT);	//���ص��б������ϵͳ�ϵ�ǰ���ڵ��豸ʵ��

	if (cr != CR_SUCCESS)
	{
		goto Exit;
	}

	for (CurrentDevice = DeviceList;
		*CurrentDevice;
		CurrentDevice += wcslen(CurrentDevice) + 1)
	{
		
		// If the list of devices also includes non-present devices,
		// CM_LOCATE_DEVNODE_PHANTOM should be used in place of
		// CM_LOCATE_DEVNODE_NORMAL.
		//�����豸ʵ��ID ��ȡ�豸�ľ��
		cr = CM_Locate_DevNode(
			&Devinst,					//ָ��CM_Locate_DevNode�������豸ʵ�������ָ��
			CurrentDevice,				//ָ����NULL��β���ַ�����ָ�룬��ʾ�豸ʵ��ID�������ֵΪNULL��������ָ���㳤���ַ�������ú����������豸���������豸���豸ʵ�������
			CM_LOCATE_DEVNODE_NORMAL);	//�����豸��ǰ���豸��������ʱ���ú����Ż����ָ���豸���豸ʵ�������

		if (cr != CR_SUCCESS)
		{
			goto Exit;
		}
			
		// Query a property on the device.  For example, the device description.
		//�����豸��������豸ʵ������
		//sprintf((char *)&DEVPKEY_Device_DeviceDesc.fmtid, "{36fc9e60-c465-11cf-8056-444553540000}");
		//DEVPKEY_Device_DeviceDesc.pid = 0x3283;
		PropertySize = sizeof(DeviceDesc);
		cr = CM_Get_DevNode_Property(
			Devinst,							//�󶨵����ؼ�������豸ʵ�������
			(const DEVPROPKEY *)&DEVPKEY_Device_DeviceDesc,			//ָ��DEVPROPKEY�ṹ��ָ�룬�ýṹ��ʾ��������豸ʵ�����Ե��豸���Լ�
			&PropertyType,						//ָ��DEVPROPTYPE - typed������ָ�룬�ñ���������������豸ʵ�����Ե�property - data - type��ʶ��������property - data - type��ʶ���ǻ����������ͱ�ʶ��֮��İ�λOR������ǻ�������type���޸ģ����������������η���
			(PBYTE)DeviceDesc,					//ָ�������������豸ʵ�����ԵĻ�������ָ�롣�����������㹻����������������ֵ����ʱ��CM_Get_DevNode_Property�Ż��������������ԡ�ָ�����ΪNULL��
			&PropertySize,						//PropertyBuffer�������Ĵ�С�����ֽ�Ϊ��λ�������PropertyBuffer����ΪNULL����* PropertyBufferSize��������Ϊ�㡣��Ϊ��������������������������������ֵ���ݣ�CM_Get_DevNode_Property������* PropertyBufferSize�����ݵĴ�С�����ֽ�Ϊ��λ����
			0);

		if (cr != CR_SUCCESS)
		{
			Index++;
			continue;
		}

		if (PropertyType != DEVPROP_TYPE_STRING)
		{
			Index++;
			continue;
		}

		Index++;
	}

Exit:

	if (DeviceList != NULL)
	{
		HeapFree(GetProcessHeap(),
			0,
			DeviceList);
	}

	return;
}



void GetDevicePropertySpecificDeviceCfgmgr32(VOID)
{
	CONFIGRET cr = CR_SUCCESS;
	DEVINST Devinst;
	WCHAR DeviceDesc[2048];
	DEVPROPTYPE PropertyType;
	ULONG PropertySize;
	ULONG DevKeySize;
	DEVPROPKEY DEVPKEY_Device_DeviceDesc;
	// If MY_DEVICE could be a non-present device, CM_LOCATE_DEVNODE_PHANTOM
	// should be used in place of CM_LOCATE_DEVNODE_NORMAL.
	//wchar_t *  CurrentDevice = L"USB\\VID_1908&PID_3283&MI_04\\6&2662cd8f&1&0004";
	wchar_t * CurrentDevice = L"USB\\VID_1908&PID_3283&MI_04\\6&13a91cea&0&0004";

	cr = CM_Locate_DevNode(&Devinst,
		CurrentDevice,
		CM_LOCATE_DEVNODE_NORMAL);

	if (cr != CR_SUCCESS)
	{
		goto Exit;
	}
	DevKeySize = sizeof(DEVPROPKEY);
	cr = CM_Get_DevNode_Property_Keys(Devinst, &DEVPKEY_Device_DeviceDesc, &DevKeySize, 0);
	if (CR_BUFFER_SMALL == cr){
		cr = CM_Get_DevNode_Property_Keys(Devinst, &DEVPKEY_Device_DeviceDesc, &DevKeySize, 0);
	}
	HANDLE m_fileHandle = ::CreateFile(L"\\\\?\\usbstor#disk&ven_buildwin&prod_media-player&rev_1.00#7&3356cc50&0#{53f56307-b6bf-11d0-94f2-00a0c91efb8b}",
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL);  //���豸
	// Query a property on the device.  For example, the device description.
	//sprintf((char *)&DEVPKEY_Device_DeviceDesc.fmtid, "{36fc9e60-c465-11cf-8056-444553540000}");
	//DEVPKEY_Device_DeviceDesc.pid = 0x3283;
	PropertySize = sizeof(DeviceDesc);
	cr = CM_Get_DevNode_Property(Devinst,
		&DEVPKEY_Device_DeviceDesc,
		&PropertyType,
		(PBYTE)DeviceDesc,
		&PropertySize,
		0);

	if (cr != CR_SUCCESS)
	{
		goto Exit;
	}

	if (PropertyType != DEVPROP_TYPE_STRING)
	{
		goto Exit;
	}

Exit:

	return;
}

//
//+pDetailData	0x0e797da0 {cbSize = 6 DevicePath = 0x0e797da4 L"\\\\?\\usbstor#disk&ven_buildwin&prod_media-player&rev_1.00#7&3356cc50&0#{53f56307-b6bf-11d0-94f2-00a0c91efb8b}" }	_SP_DEVICE_INTERFACE_DETAIL_DATA_W *
//pDetailData->DevicePath = 0x0e797da4 L"\\\\?\\usbstor#disk&ven_buildwin&prod_media-player&rev_1.00#7&3356cc50&0#{53f56307-b6bf-11d0-94f2-00a0c91efb8b}"
