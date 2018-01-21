// SysInfoMoniter.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#include <windows.h>
#include<Winternl.h>
#include <Wbemcli.h>
#include <Wbemidl.h>
#include <comutil.h>
#include <IPHlpApi.h>
#include <math.h>

#include <iostream>
using namespace std;

#pragma warning(disable: 4996)
#pragma warning(disable: 4244)
#pragma comment(lib, "wbemuuid.lib") 
#pragma comment(lib, "comsupp.lib")


wstring get_cpufan_state()
{
	IWbemLocator* pIWbemLocator = NULL;
	IWbemServices* pWbemServices = NULL;
	IEnumWbemClassObject* pEnumObject = NULL;

	BSTR bstrNamespace = (L"root\\cimv2");	
	HRESULT hRes;
	BSTR strQuery = (L"Select * from Win32_Fan");
	BSTR strQL = (L"WQL");
	VARIANT v;
	BSTR strClassProp = SysAllocString(L"Status");
	WCHAR* buf = NULL;
	_bstr_t value;		
	wstring strStatus;

	if (CoCreateInstance(CLSID_WbemAdministrativeLocator,
						 NULL ,
						 CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER ,
						 IID_IUnknown ,
						 ( void ** ) & pIWbemLocator) != S_OK)
	{
		goto END;
	}

	if (pIWbemLocator->ConnectServer(bstrNamespace, NULL, NULL, NULL, 0,
									 NULL, NULL,&pWbemServices) != S_OK)
	{
		goto END;
	}

	hRes = CoSetProxyBlanket(
							pWbemServices,               
							RPC_C_AUTHN_WINNT,          
							RPC_C_AUTHZ_NONE,           
							NULL,                        
							RPC_C_AUTHN_LEVEL_CALL,      
							RPC_C_IMP_LEVEL_IMPERSONATE, 
							NULL,                        
							EOAC_NONE                   
							);

	if (FAILED(hRes))
	{  
		goto END;
	}

	hRes = pWbemServices->ExecQuery(strQL, strQuery,
									WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumObject);
	if (hRes != S_OK)
	{	
		goto END;
	}

	hRes = pEnumObject->Reset();
	if (hRes != S_OK)
	{
		goto END;
	}

	ULONG uCount = 1, uReturned;
	IWbemClassObject* pClassObject = NULL;
	hRes = pEnumObject->Next(WBEM_INFINITE,uCount, &pClassObject, &uReturned);

	if(hRes != WBEM_S_NO_ERROR)
	{
		goto END;
	}

	hRes = pClassObject->Get(strClassProp, 0, &v, 0, 0);
	if (hRes != S_OK)
	{
		goto END;
	}

	value = _bstr_t(_variant_t(v));
	buf = value;
	strStatus = buf;

	SysFreeString(strClassProp);

	VariantClear(&v);

END:

	if(pIWbemLocator)pIWbemLocator->Release();
	if(pWbemServices)pWbemServices->Release();
	if(pEnumObject)pEnumObject->Release();
	if(pClassObject)pClassObject->Release();

	return strStatus;
}


UINT get_disk_free_space(TCHAR* szDisk)
{
	ULARGE_INTEGER uiSize;

	GetDiskFreeSpaceEx(szDisk,
					   &uiSize,
					   NULL,
					   NULL);

	return uiSize.QuadPart/1024/1024/1024;
}

UINT get_memory_percentage()
{
	MEMORYSTATUS mStatus;

	GlobalMemoryStatus(&mStatus);

	return mStatus.dwMemoryLoad;
}

typedef struct _IoSpeed
{
	UINT uReadPsec;
	UINT uWritePsec;
	
	_IoSpeed()
	{
		uReadPsec = uWritePsec = 0;
	}

}IoSpeed;

IoSpeed get_disk_speed(char* szDisk)
{

	if(szDisk == NULL)return IoSpeed();

	HRESULT                 hr = S_OK;
	IWbemRefresher          *pRefresher = NULL;
	IWbemConfigureRefresher *pConfig = NULL;
	IWbemHiPerfEnum         *pEnum = NULL;
	IWbemServices           *pNameSpace = NULL;
	IWbemLocator            *pWbemLocator = NULL;
	IWbemObjectAccess       **apEnumAccess = NULL;
	BSTR                    bstrNameSpace = NULL;
	long                    lID = 0;
	long                    lDiskReadBytesHandle = 0;
	long                    lDiskWriteBytesHandle = 0;
	long                    lDiskNameHandle = 0;
	DWORD                   dwBytesRead = 0;
	DWORD                   dwProcessId = 0;
	DWORD                   dwNumObjects = 0;
	DWORD                   dwNumReturned = 0;
	DWORD                   dwBytesWritten = 0;
	DWORD                   i=0;
	int                     x=0;

	IoSpeed dSpeed;

	if (FAILED (hr = CoCreateInstance(CLSID_WbemLocator, 
									  NULL,
									  CLSCTX_INPROC_SERVER,
									  IID_IWbemLocator,
									  (void**) &pWbemLocator)))
	{
		goto CLEANUP;
	}

	// Connect to the desired namespace.
	bstrNameSpace = SysAllocString(L"\\\\.\\root\\cimv2");
	if (NULL == bstrNameSpace)
	{
		hr = E_OUTOFMEMORY;
		goto CLEANUP;
	}
	if (FAILED (hr = pWbemLocator->ConnectServer(bstrNameSpace,
												NULL, // User name
												NULL, // Password
												NULL, // Locale
												0L,   // Security flags
												NULL, // Authority
												NULL, // Wbem context
												&pNameSpace)))
	{
		goto CLEANUP;
	}

	pWbemLocator->Release();
	pWbemLocator=NULL;
	SysFreeString(bstrNameSpace);
	bstrNameSpace = NULL;

	if (FAILED (hr = CoCreateInstance(CLSID_WbemRefresher,
										NULL,
										CLSCTX_INPROC_SERVER,
										IID_IWbemRefresher, 
										(void**) &pRefresher)))
	{
		goto CLEANUP;
	}

	if (FAILED (hr = pRefresher->QueryInterface(IID_IWbemConfigureRefresher,
												(void **)&pConfig)))
	{
		goto CLEANUP;
	}

	// Add an enumerator to the refresher.
	if (FAILED (hr = pConfig->AddEnum(pNameSpace, 
										L"Win32_PerfFormattedData_PerfDisk_LogicalDisk", 
										0, 
										NULL, 
										&pEnum, 
										&lID)))
	{
		goto CLEANUP;
	}
	pConfig->Release();
	pConfig = NULL;

	// Refresh the object ten times and retrieve the value.
	for(x = 0; x < 2; x++)
	{
		dwNumReturned = 0;
		dwBytesWritten = 0;
		dwNumObjects = 0;

		if (FAILED (hr =pRefresher->Refresh(0L)))
		{
			goto CLEANUP;
		}

		hr = pEnum->GetObjects(0L, 
								dwNumObjects, 
								apEnumAccess, 
								&dwNumReturned);

		if (hr == WBEM_E_BUFFER_TOO_SMALL 
			&& dwNumReturned > dwNumObjects)
		{
			apEnumAccess = new IWbemObjectAccess*[dwNumReturned];
			if (NULL == apEnumAccess)
			{
				hr = E_OUTOFMEMORY;
				goto CLEANUP;
			}
			SecureZeroMemory(apEnumAccess,
							 dwNumReturned*sizeof(IWbemObjectAccess*));
			dwNumObjects = dwNumReturned;

			if (FAILED (hr = pEnum->GetObjects(0L, 
												dwNumObjects, 
												apEnumAccess, 
												&dwNumReturned)))
			{
				goto CLEANUP;
			}
		}
		else
		{
			if (hr == WBEM_S_NO_ERROR)
			{
				hr = WBEM_E_NOT_FOUND;
				goto CLEANUP;
			}
		}

		// First time through, get the handles.
		if (0 == x){

			CIMTYPE DiskReadBytesType;
			CIMTYPE DiskWriteBytesType;
			CIMTYPE DiskNameType;

			if (FAILED (hr = apEnumAccess[0]->GetPropertyHandle(L"DiskReadBytesPerSec",
																&DiskReadBytesType,
																&lDiskReadBytesHandle)))
			{
				goto CLEANUP;
			}
			if (FAILED (hr = apEnumAccess[0]->GetPropertyHandle(L"DiskWriteBytesPerSec",
																&DiskWriteBytesType,
																&lDiskWriteBytesHandle)))
			{
				goto CLEANUP;
			}

			if (FAILED (hr = apEnumAccess[0]->GetPropertyHandle(L"Name",
																&DiskNameType,
																&lDiskNameHandle)))
			{
				goto CLEANUP;
			}
		}

		for (i = 0; i < dwNumReturned; i++)
		{
			if (FAILED (hr = apEnumAccess[i]->ReadDWORD(lDiskReadBytesHandle,
														&dwBytesRead)))
			{
				goto CLEANUP;
			}
			if (FAILED (hr = apEnumAccess[i]->ReadDWORD(lDiskWriteBytesHandle,
														&dwBytesWritten)))
			{
				goto CLEANUP;
			}

			long byteReturned = 0;
			byte pData[256] = {0};

			if (FAILED (hr = apEnumAccess[i]->ReadPropertyValue(lDiskNameHandle,
																256,
																&byteReturned,
																pData)))
			{
				goto CLEANUP;
			}

			if(pData[0] == szDisk[0] || pData[0]-32 == szDisk[0] || pData[0]+32 == szDisk[0])
			{
				dSpeed.uReadPsec = dwBytesRead;
				dSpeed.uWritePsec = dwBytesWritten;
			}

			// Done with the object
			apEnumAccess[i]->Release();
			apEnumAccess[i] = NULL;
		}

		if (NULL != apEnumAccess)
		{
			delete [] apEnumAccess;
			apEnumAccess = NULL;
		}

		Sleep(1000);
	}

CLEANUP:

	if (NULL != bstrNameSpace){
		SysFreeString(bstrNameSpace);
	}

	if (NULL != apEnumAccess){
		for (i = 0; i < dwNumReturned; i++){
			if (apEnumAccess[i] != NULL){
				apEnumAccess[i]->Release();
				apEnumAccess[i] = NULL;
			}
		}
		delete [] apEnumAccess;
	}
	if (NULL != pWbemLocator){
		pWbemLocator->Release();
	}
	if (NULL != pNameSpace){
		pNameSpace->Release();
	}
	if (NULL != pEnum){
		pEnum->Release();
	}
	if (NULL != pConfig){
		pConfig->Release();
	}
	if (NULL != pRefresher){
		pRefresher->Release();
	}
	if (FAILED(hr)){
		cout << "Error status=0X" << hex<< hr << dec << endl;
	}

	return dSpeed;
}

IoSpeed get_system_io_speed()
{
	HRESULT                 hr = S_OK;
	IWbemRefresher          *pRefresher = NULL;
	IWbemConfigureRefresher *pConfig = NULL;
	IWbemHiPerfEnum         *pEnum = NULL;
	IWbemServices           *pNameSpace = NULL;
	IWbemLocator            *pWbemLocator = NULL;
	IWbemObjectAccess       **apEnumAccess = NULL;
	BSTR                    bstrNameSpace = NULL;
	long                    lID = 0;
	long                    lSystemReadBytesHandle = 0;
	long                    lSystemWriteBytesHandle = 0;
	DWORD                   dwBytesRead = 0;
	DWORD                   dwProcessId = 0;
	DWORD                   dwNumObjects = 0;
	DWORD                   dwNumReturned = 0;
	DWORD                   dwBytesWritten = 0;
	DWORD                   i=0;
	int                     x=0;

	IoSpeed dSpeed;

	if (FAILED (hr = CoCreateInstance(
										CLSID_WbemLocator, 
										NULL,
										CLSCTX_INPROC_SERVER,
										IID_IWbemLocator,
										(void**) &pWbemLocator)))
	{
		goto CLEANUP;
	}

	// Connect to the desired namespace.
	bstrNameSpace = SysAllocString(L"\\\\.\\root\\cimv2");
	if (NULL == bstrNameSpace)
	{
		hr = E_OUTOFMEMORY;
		goto CLEANUP;
	}
	if (FAILED (hr = pWbemLocator->ConnectServer(
												bstrNameSpace,
												NULL, // User name
												NULL, // Password
												NULL, // Locale
												0L,   // Security flags
												NULL, // Authority
												NULL, // Wbem context
												&pNameSpace)))
	{
		goto CLEANUP;
	}

	pWbemLocator->Release();
	pWbemLocator=NULL;
	SysFreeString(bstrNameSpace);
	bstrNameSpace = NULL;

	if (FAILED (hr = CoCreateInstance(CLSID_WbemRefresher,
									  NULL,
									  CLSCTX_INPROC_SERVER,
									  IID_IWbemRefresher, 
									  (void**) &pRefresher)))
	{
		goto CLEANUP;
	}

	if (FAILED (hr = pRefresher->QueryInterface(IID_IWbemConfigureRefresher,
												(void **)&pConfig)))
	{
		goto CLEANUP;
	}

	// Add an enumerator to the refresher.
	if (FAILED (hr = pConfig->AddEnum(pNameSpace, 
										L"Win32_PerfFormattedData_PerfOS_System", 
										0, 
										NULL, 
										&pEnum, 
										&lID)))
	{
		goto CLEANUP;
	}
	pConfig->Release();
	pConfig = NULL;

	// Refresh the object ten times and retrieve the value.
	for(x = 0; x < 2; x++)
	{
		dwNumReturned = 0;
		dwBytesWritten = 0;
		dwNumObjects = 0;

		if (FAILED (hr =pRefresher->Refresh(0L)))
		{
			goto CLEANUP;
		}

		hr = pEnum->GetObjects(0L, 
								dwNumObjects, 
								apEnumAccess, 
								&dwNumReturned);

		if (hr == WBEM_E_BUFFER_TOO_SMALL 
			&& dwNumReturned > dwNumObjects)
		{
			apEnumAccess = new IWbemObjectAccess*[dwNumReturned];
			if (NULL == apEnumAccess)
			{
				hr = E_OUTOFMEMORY;
				goto CLEANUP;
			}
			SecureZeroMemory(apEnumAccess,
			                 dwNumReturned*sizeof(IWbemObjectAccess*));
			dwNumObjects = dwNumReturned;

			if (FAILED (hr = pEnum->GetObjects(0L, 
												dwNumObjects, 
												apEnumAccess, 
												&dwNumReturned)))
			{
				goto CLEANUP;
			}
		}
		else
		{
			if (hr == WBEM_S_NO_ERROR)
			{
				hr = WBEM_E_NOT_FOUND;
				goto CLEANUP;
			}
		}

		// First time through, get the handles.
		if (0 == x){

			CIMTYPE FileReadBytesType;
			CIMTYPE FileWriteBytesType;

			if (FAILED (hr = apEnumAccess[0]->GetPropertyHandle(L"FileReadBytesPerSec",
																&FileReadBytesType,
																&lSystemReadBytesHandle)))
			{
				goto CLEANUP;
			}
			if (FAILED (hr = apEnumAccess[0]->GetPropertyHandle(L"FileWriteBytesPerSec",
																&FileWriteBytesType,
																&lSystemWriteBytesHandle)))
			{
				goto CLEANUP;
			}
		}

		for (i = 0; i < dwNumReturned; i++)
		{
			if (FAILED (hr = apEnumAccess[i]->ReadDWORD(lSystemReadBytesHandle,
														&dwBytesRead)))
			{
				goto CLEANUP;
			}
			if (FAILED (hr = apEnumAccess[i]->ReadDWORD(lSystemWriteBytesHandle,
														&dwBytesWritten)))
			{
				goto CLEANUP;
			}

			dSpeed.uReadPsec = dwBytesRead;
			dSpeed.uWritePsec = dwBytesWritten;

			// Done with the object
			apEnumAccess[i]->Release();
			apEnumAccess[i] = NULL;
		}

		if (NULL != apEnumAccess){

			delete [] apEnumAccess;
			apEnumAccess = NULL;
		}

		Sleep(1000);
	}

CLEANUP:

	if (bstrNameSpace)SysFreeString(bstrNameSpace);
	
	if (apEnumAccess){
		for (i = 0; i < dwNumReturned; i++){
			if (apEnumAccess[i] != NULL){
				apEnumAccess[i]->Release();
				apEnumAccess[i] = NULL;
			}
		}
		delete [] apEnumAccess;
	}
	if (pWbemLocator)pWbemLocator->Release();
	if (pNameSpace)pNameSpace->Release();
	if (pEnum)pEnum->Release();
	if (pConfig)pConfig->Release();
	if (pRefresher)pRefresher->Release();

	if (FAILED(hr)){
		cout << "Error status=0X" << hex<< hr << dec << endl;
	}

	return dSpeed;
} 

DWORD CurrentInBytes = 0;
DWORD CurrentOutBytes = 0;
DWORD lastInBytes = 0;
DWORD lastOutBytes = 0;

BOOL GetNetInformation()   
{   
	MIB_IFTABLE *pIfTable = NULL;   
	MIB_IFROW *pIfRow=NULL;   
	ULONG dwSize = 0;   

	DWORD dwRet;   

	dwRet = GetIfTable(pIfTable, &dwSize, TRUE ); //第一次调用获取结构大小  
	if ( dwRet == ERROR_INSUFFICIENT_BUFFER )   
	{   
		pIfTable = (MIB_IFTABLE*)new char[dwSize];   

		if (pIfTable != NULL )   
		{   
			dwRet = GetIfTable( pIfTable, &dwSize, TRUE ); //获得相关信息  
			
			if (dwRet == NO_ERROR)   
			{   
				for(DWORD i = 0; i < pIfTable->dwNumEntries; ++i)
				{
					if(pIfTable->table[i].dwType == MIB_IF_TYPE_ETHERNET)
					{
						pIfRow = (MIB_IFROW *) & pIfTable->table[1]; 
						
						CurrentInBytes=pIfRow->dwInOctets; //保存当次的输入字节数  
						CurrentOutBytes=pIfRow->dwOutOctets; //保存当次的输出字节数 

						break;
					}
				}
			}   
			else   
			{   
				return FALSE;   
			}   
		}   
		else   
		{   
			return FALSE;   
		}   
	}   
	else   
	{    
		return FALSE;   
	}   
	return TRUE;   
}   

UINT get_network_Inspeed()
{
	GetNetInformation();  

	lastInBytes=CurrentInBytes;  
	lastOutBytes=CurrentOutBytes;  

	Sleep(1000);

	if(GetNetInformation())  
	{  
		return (float(CurrentInBytes-lastInBytes))/1024;  
	}  

	return 0;
}

UINT get_network_Outspeed()
{
	GetNetInformation();  

	lastInBytes=CurrentInBytes;  
	lastOutBytes=CurrentOutBytes;  

	Sleep(1000);

	if(GetNetInformation())  
	{  
		return (float(CurrentOutBytes-lastOutBytes));  
	} 

	return 0;
}

UINT get_cpu_percentage()
{
	FILETIME fIdleTime1;
	FILETIME fIdleTime2;
	FILETIME fKernelTime1;
	FILETIME fKernelTime2;
	FILETIME fUserTime1;
	FILETIME fUserTime2;

	GetSystemTimes(&fIdleTime1, &fKernelTime1, &fUserTime1);
	
	LARGE_INTEGER liIdleTime1;
	liIdleTime1.HighPart = fIdleTime1.dwHighDateTime;
	liIdleTime1.LowPart = fIdleTime1.dwLowDateTime;
	
	LARGE_INTEGER liKernelTime1;
	liKernelTime1.HighPart = fKernelTime1.dwHighDateTime;
	liKernelTime1.LowPart = fKernelTime1.dwLowDateTime;
	
	LARGE_INTEGER liUserTime1;
	liUserTime1.HighPart = fUserTime1.dwHighDateTime;
	liUserTime1.LowPart = fUserTime1.dwLowDateTime;	

	Sleep(500);
	
	GetSystemTimes(&fIdleTime2, &fKernelTime2, &fUserTime2);

	LARGE_INTEGER liIdleTime2;
	liIdleTime2.HighPart = fIdleTime2.dwHighDateTime;
	liIdleTime2.LowPart = fIdleTime2.dwLowDateTime;

	LARGE_INTEGER liKernelTime2;
	liKernelTime2.HighPart = fKernelTime2.dwHighDateTime;
	liKernelTime2.LowPart = fKernelTime2.dwLowDateTime;
	
	LARGE_INTEGER liUserTime2;
	liUserTime2.HighPart = fUserTime2.dwHighDateTime;
	liUserTime2.LowPart = fUserTime2.dwLowDateTime;
	
	double dIdle = liIdleTime2.QuadPart - liIdleTime1.QuadPart;
	double dKernel =  liKernelTime2.QuadPart - liKernelTime1.QuadPart;
	double dUser = liUserTime2.QuadPart - liUserTime1.QuadPart;

	return 100 - dIdle/(dUser+ dKernel) * 100;
}

DWORD GetRaidHardDisks()
{
	DWORD dw = 0;

	HRESULT                 hr = S_OK;
	IWbemRefresher          *pRefresher = NULL;
	IWbemConfigureRefresher *pConfig = NULL;
	IWbemHiPerfEnum         *pEnum = NULL;
	IWbemServices           *pNameSpace = NULL;
	IWbemLocator            *pWbemLocator = NULL;
	IWbemObjectAccess       **apEnumAccess = NULL;
	BSTR                    bstrNameSpace = NULL;
	long                    lID = 0;
	long                    lDiskIndexHandle = 0;
	long                    lDiskCaptionHandle = 0;
	DWORD                   dwBytesRead = 0;
	DWORD                   dwNumObjects = 0;
	DWORD                   dwNumReturned = 0;
	DWORD                   i=0;
	int                     x=0;

	if (FAILED (hr = CoCreateInstance(CLSID_WbemLocator, NULL,CLSCTX_INPROC_SERVER,IID_IWbemLocator,(void**) &pWbemLocator)))
	{
		goto CLEANUP;
	}

	// Connect to the desired namespace.
	bstrNameSpace = SysAllocString(L"\\\\.\\root\\cimv2");
	if (NULL == bstrNameSpace)
	{
		hr = E_OUTOFMEMORY;
		goto CLEANUP;
	}
	if (FAILED (hr = pWbemLocator->ConnectServer(bstrNameSpace,
												 NULL, // User name
												 NULL, // Password
												 NULL, // Locale
												 0L,   // Security flags
												 NULL, // Authority
												 NULL, // Wbem context
												 &pNameSpace)))
	{
		goto CLEANUP;
	}

	pWbemLocator->Release();
	pWbemLocator=NULL;
	SysFreeString(bstrNameSpace);
	bstrNameSpace = NULL;

	if (FAILED (hr = CoCreateInstance(CLSID_WbemRefresher,NULL,CLSCTX_INPROC_SERVER,IID_IWbemRefresher, (void**) &pRefresher)))
	{
		goto CLEANUP;
	}

	if (FAILED (hr = pRefresher->QueryInterface(IID_IWbemConfigureRefresher,(void **)&pConfig)))
	{
		goto CLEANUP;
	}

	// Add an enumerator to the refresher.
	if (FAILED (hr = pConfig->AddEnum(pNameSpace, L"Win32_DiskDrive", 0, NULL, &pEnum, &lID)))
	{
		goto CLEANUP;
	}
	pConfig->Release();
	pConfig = NULL;

	// Refresh the object ten times and retrieve the value.
	for(x = 0; x < 2; x++)
	{
		dwNumReturned = 0;
		dwNumObjects = 0;

		if (FAILED (hr =pRefresher->Refresh(0L)))
		{
			goto CLEANUP;
		}

		hr = pEnum->GetObjects(0L, dwNumObjects, apEnumAccess, &dwNumReturned);

		if (hr == WBEM_E_BUFFER_TOO_SMALL && dwNumReturned > dwNumObjects)
		{
			apEnumAccess = new IWbemObjectAccess*[dwNumReturned];
			
			if (NULL == apEnumAccess)
			{
				hr = E_OUTOFMEMORY;
				goto CLEANUP;
			}
			
			SecureZeroMemory(apEnumAccess,dwNumReturned*sizeof(IWbemObjectAccess*));
			
			dwNumObjects = dwNumReturned;

			if (FAILED (hr = pEnum->GetObjects(0L, dwNumObjects, apEnumAccess, &dwNumReturned)))
			{
				goto CLEANUP;
			}
		}
		else
		{
			if (hr == WBEM_S_NO_ERROR)
			{
				hr = WBEM_E_NOT_FOUND;
				goto CLEANUP;
			}
		}

		// First time through, get the handles.
		if (0 == x){

			CIMTYPE DiskIndexType;
			CIMTYPE DiskCaptionType;

			if (FAILED (hr = apEnumAccess[0]->GetPropertyHandle(L"Index",&DiskIndexType,&lDiskIndexHandle)))
			{
				goto CLEANUP;
			}

			if (FAILED (hr = apEnumAccess[0]->GetPropertyHandle(L"Caption",&DiskCaptionType,&lDiskCaptionHandle)))
			{
				goto CLEANUP;
			}
		}

		for (i = 0; i < dwNumReturned; i++)
		{
			byte pData[1024] = {0};
			long byteReturned = 0;

			DWORD dwIndex = 0;

			if (FAILED (hr = apEnumAccess[i]->ReadDWORD(lDiskIndexHandle,&dwIndex)))
			{
				goto CLEANUP;
			}

			if (FAILED (hr = apEnumAccess[i]->ReadPropertyValue(lDiskCaptionHandle,1024,&byteReturned,pData)))
			{
				goto CLEANUP;
			}

			if(wcsstr((LPCWSTR)pData, L"MegaRAID"))
			{
				int i = 1;

				i = i << dwIndex;

				dw = dw | i;
			}

			// Done with the object
			apEnumAccess[i]->Release();
			apEnumAccess[i] = NULL;
		}

		if (NULL != apEnumAccess){

			delete [] apEnumAccess;
			apEnumAccess = NULL;
		}

		Sleep(1000);
	}

CLEANUP:

	if (bstrNameSpace) SysFreeString(bstrNameSpace);

	if (apEnumAccess){
		for (i = 0; i < dwNumReturned; i++){
			if (apEnumAccess[i] != NULL){
				apEnumAccess[i]->Release();
				apEnumAccess[i] = NULL;
			}
		}
		delete [] apEnumAccess;
	}
	if (pWbemLocator)pWbemLocator->Release();
	if (pNameSpace)pNameSpace->Release();
	if (pEnum)pEnum->Release();
	if (pConfig)pConfig->Release();
	if (pRefresher)pRefresher->Release();
	
	if (FAILED(hr)){
		cout << "Error status=0X" << hex<< hr << dec << endl;
	}

	return dw;
}

DWORD GetRaidLogicalDisks()
{
	DWORD dw = GetLogicalDrives();
	DWORD dwHard = GetRaidHardDisks();

	IWbemLocator* pIWbemLocator = NULL;
	IWbemServices* pWbemServices = NULL;
	IEnumWbemClassObject* pEnumObject = NULL;
	IWbemClassObject* pClassObject = NULL;

	BSTR bstrNamespace = SysAllocString(L"\\\\.\\root\\cimv2");	
	HRESULT hRes;
	BSTR strQL = (L"WQL");		

	if (CoCreateInstance(CLSID_WbemAdministrativeLocator,NULL ,
						 CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER ,
						 IID_IUnknown ,
						 ( void ** ) & pIWbemLocator) != S_OK)
	{
		dw = 0;
		goto END;
	}

	if (pIWbemLocator->ConnectServer(bstrNamespace, NULL, NULL, NULL, 0,NULL, NULL,&pWbemServices) != S_OK)
	{
		dw = 0;
		goto END;
	}

	hRes = CoSetProxyBlanket(
								pWbemServices,               // Indicates the proxy to set
								RPC_C_AUTHN_WINNT,           // RPC_C_AUTHN_xxx
								RPC_C_AUTHZ_NONE,            // RPC_C_AUTHZ_xxx
								NULL,                        // Server principal name 
								RPC_C_AUTHN_LEVEL_CALL,      // RPC_C_AUTHN_LEVEL_xxx 
								RPC_C_IMP_LEVEL_IMPERSONATE, // RPC_C_IMP_LEVEL_xxx
								NULL,                        // client identity
								EOAC_NONE                    // proxy capabilities 
								);

	if (FAILED(hRes))
	{  
		dw = 0;
		goto END;
	}

	BSTR strClassProp = SysAllocString(L"DiskIndex");

	for(DWORD i = 0; i < 26; ++i)
	{
		int n = 1;

		n = n << i;

		if( (dw & n) != 0)
		{
			_bstr_t bstrQuery(L"Associators of {win32_LogicalDisk='");

			WCHAR wLetter[2] = {0};

			wLetter[0] = i + 65;

			bstrQuery += wLetter;

			bstrQuery += L":'} where resultClass=Win32_DiskPartition";

			hRes = pWbemServices->ExecQuery(strQL, bstrQuery,
											WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumObject);
			if (hRes != S_OK)
			{	
				dw = 0;
				goto END;
			}

			hRes = pEnumObject->Reset();
			if (hRes != S_OK)
			{
				dw = 0;
				goto END;
			}

			ULONG uCount = 1, uReturned;
			hRes = pEnumObject->Next(WBEM_INFINITE,uCount, &pClassObject, &uReturned);

			if(hRes != WBEM_S_NO_ERROR)
			{
				// There is no more CIM info to get

				int x = 0xFFFFFFFF;

				x = x ^ (1 << i);

				dw = dw & x;
			}

			VARIANT v;
			
			hRes = pClassObject->Get(strClassProp, 0, &v, 0, 0);
			
			if (hRes != S_OK)
			{
				int x = 0xFFFFFFFF;

				x = x ^ (1 << i);

				dw = dw & x;
			}	

			_bstr_t value = _bstr_t(_variant_t(v));
			WCHAR* buf = NULL;
			buf = value;

			// index of physical disk on which logical disk resides
			int index = _ttoi(buf);

			int m = 1;

			m = m << index;

			if((dwHard & m) == 0)
			{
				int x = 0xFFFFFFFF;

				x = x ^ (1 << i);

				dw = dw & x;
			}

			VariantClear(&v);
		}		
	}

	SysFreeString(strClassProp);

END:

	if (bstrNamespace)SysFreeString(bstrNamespace);

	if(pIWbemLocator)pIWbemLocator->Release();
	if(pWbemServices)pWbemServices->Release();
	if(pEnumObject)pEnumObject->Release();
	if(pClassObject)pClassObject->Release();

	return dw;
}

int _tmain(int argc, _TCHAR* argv[])
{
	CoInitializeEx(NULL,COINIT_MULTITHREADED);

	CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_PKT,RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, 0);
	
	HANDLE hConsole;

	hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

	if(!hConsole) return 1;

	while(1)
	{
		cout << "CPU fan state: ";

		SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN);  

		wstring strState = get_cpufan_state();

		wcout << strState.c_str() << '\n'<< endl;

		SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE|FOREGROUND_GREEN|FOREGROUND_RED); 

		cout << "CPU load percentage: ";

		SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN); 

		UINT uPercentage = get_cpu_percentage();

		cout << uPercentage << "%" << '\n'<< endl;

		UINT uMemUsed = get_memory_percentage();

		SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE|FOREGROUND_GREEN|FOREGROUND_RED); 

		cout << "Memory used: ";

		SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN);  

		cout << uMemUsed << "%" << '\n'<<  endl;

		DWORD dwLogical = GetRaidLogicalDisks();

		for(DWORD i = 0; i < 26; ++i)
		{
			int n = 1;

			n = n << i;

			if((dwLogical & n) != 0)
			{
				CHAR Disk[4] = {0};
				Disk[0] = i + 65;
				Disk[1] = ':';

				SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE|FOREGROUND_GREEN|FOREGROUND_RED); 

				cout << "Logical disk " << Disk << " is RAID disk!" << '\n' << endl;

				Disk[2] = '\\';

				WCHAR wDisk[4] = {0};

				MultiByteToWideChar(CP_ACP,
									0,
									Disk,
									4,
									wDisk,
									4);

				UINT uFreeSize = get_disk_free_space(wDisk);

				Disk[2] = 0;

				cout << Disk << " free size: " ;

				SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN);  

				cout << uFreeSize << " GB" << endl << endl;

				Disk[1] = 0;

				IoSpeed dSpeed = get_disk_speed(Disk);

				SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE|FOREGROUND_GREEN|FOREGROUND_RED); 

				cout << "Disk " << Disk << " Read speed: ";

				SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN); 

				cout << dSpeed.uReadPsec << " Bytes/s" << '\n'<<  endl;

				SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE|FOREGROUND_GREEN|FOREGROUND_RED); 

				cout << "Disk " << Disk << " Write speed: ";

				SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN); 

				cout << dSpeed.uWritePsec<< " Bytes/s" << '\n'<<  endl;
			}
		}

		IoSpeed sSpeed = get_system_io_speed();

		SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE|FOREGROUND_GREEN|FOREGROUND_RED); 

		cout << "System Io read speed: ";
		
		SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN); 

		cout << sSpeed.uReadPsec << " Bytes/s" << '\n'<<  endl;

		SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE|FOREGROUND_GREEN|FOREGROUND_RED); 

		cout << "System Io write speed: ";

		SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN); 
		
		cout << sSpeed.uWritePsec<< " Bytes/s" << '\n'<<  endl;

		float inSpeed = get_network_Inspeed();

		SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE|FOREGROUND_GREEN|FOREGROUND_RED); 

		cout << "Network in speed: ";

		SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN);
		
		cout << inSpeed << " Bytes/s" << '\n'<<  endl;

		float outSpeed = get_network_Outspeed();

		SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE|FOREGROUND_GREEN|FOREGROUND_RED);

		cout << "Network out speed: ";

		SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN);
		
		cout << outSpeed << " Bytes/s" << endl << endl << endl;

		Sleep(10000);
		
		system("cls");

		Sleep(500);
		
		SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE|FOREGROUND_GREEN|FOREGROUND_RED);
	}

	CloseHandle(hConsole);

	CoUninitialize();

	return 0;
}

