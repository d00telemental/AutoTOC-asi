// dllmain.cpp : Defines the entry point for the DLL application.
#include <windows.h>
#include <stdio.h>
#include <fstream>
#include "Shlwapi.h"
#include "Strsafe.h"
#include <stdlib.h>
#include <vector>
#include "../detours/detours.h"


#define _CRT_SECURE_NO_WARNINGS
#pragma comment(lib, "detours.lib") //Library needed for Hooking part.

#pragma pack(1)

//#define LOGGING

#include "TOCUpdater.h"

using namespace std;

using tProcessEvent = void(__thiscall* )(void*, void*, void*, void*);
tProcessEvent ProcessEvent = reinterpret_cast<tProcessEvent>(0x00453120);

typedef unsigned short ushort;

struct fileData {
	wstring fileName;
	int fileSize;

	fileData(const wstring fileName, const int fileSize) : fileName(fileName), fileSize(fileSize) {}
};

void write(ofstream &file, void* data, streamsize size);

void writeTOC(TCHAR  tocPath[260], TCHAR  baseDir[260], bool isDLC);

void write(ofstream &file, byte data);

void write(ofstream &file, ushort data);

void write(ofstream &file, int data);

void getFiles(vector<fileData> &files, TCHAR* basepath, TCHAR* searchPath);

void AutoToc(HMODULE hModule);

#ifdef LOGGING
wofstream logFile;
#endif // LOGGING

static HHOOK msgHook = nullptr;
LRESULT CALLBACK wndProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode >= HC_ACTION)
	{
		auto* const msgStruct = reinterpret_cast<LPCWPSTRUCT>(lParam);
		if (msgStruct->message == WM_APP + 'T' + 'O' + 'C')
		{
#ifdef LOGGING
			logFile << "Recieved Message From ME3Explorer..." << endl;
#endif
			TOCUpdater tocUpdater;
			const bool success = tocUpdater.TryUpdate();
#ifdef LOGGING
			logFile << (success ? "TOC Updated!" : "TOC Update FAILED!") << endl;
#endif
		}
	}
	return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

bool hookedMessages = false;
void __fastcall HookedPE(void* pObject, void* edx, void* pFunction, void* pParms, void* pResult)
{
	if (!hookedMessages)
	{
		hookedMessages = true;
		msgHook = SetWindowsHookEx(WH_CALLWNDPROC, wndProc, nullptr, GetCurrentThreadId());
	}

	ProcessEvent(pObject, pFunction, pParms, pResult);
	
	if (hookedMessages)
	{
		DetourTransactionBegin();
		DetourDetach(&(PVOID&)ProcessEvent, HookedPE);
		DetourTransactionCommit();
	}
}

void onAttach()
{
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread()); //This command set the current working thread to the game current thread.
	DetourAttach(&(PVOID&)ProcessEvent, HookedPE); //This command will start your Hook.
	DetourTransactionCommit();
}

BOOL APIENTRY DllMain( HMODULE hModule, DWORD  reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		AutoToc(hModule);
		CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)onAttach, NULL, 0, NULL);
	}
	if (reason == DLL_PROCESS_DETACH)
	{
		UnhookWindowsHookEx(msgHook);
#ifdef LOGGING
		logFile.close();
#endif
		
	}

	return TRUE;
}

void AutoToc(HMODULE hModule)
{
	TCHAR path[MAX_PATH];
	GetModuleFileName(hModule, path, MAX_PATH);
	PathRemoveFileSpec(path);
#ifdef LOGGING
	StringCchCat(path, MAX_PATH, L"\\AutoTOC.log");
	logFile.open(path);
	logFile << "AutoTOC log:\n";
	PathRemoveFileSpec(path);
#endif // LOGGING

	//get us to the ME3 base folder
	PathRemoveFileSpec(path);
	PathRemoveFileSpec(path);
	PathRemoveFileSpec(path);
	StringCchCat(path, MAX_PATH, L"\\");


	TCHAR baseDir[MAX_PATH];
	StringCchCopy(baseDir, MAX_PATH, path);
	TCHAR tocPath[MAX_PATH];
	StringCchCopy(tocPath, MAX_PATH, path);
	StringCchCat(tocPath, MAX_PATH, L"\\BIOGame\\PCConsoleTOC.bin");
#ifdef LOGGING
	logFile << "writing basegame toc..\n";
#endif // LOGGING
	writeTOC(tocPath, baseDir, false);

#ifdef LOGGING
	logFile << "TOCing dlc:\n";
#endif // LOGGING
	StringCchCat(path, MAX_PATH, L"BIOGame\\DLC\\");
	StringCchCopy(baseDir, MAX_PATH, path);

	WIN32_FIND_DATA fd;
	HANDLE hFind = INVALID_HANDLE_VALUE;
	TCHAR enumeratePath[MAX_PATH];
	StringCchCopy(enumeratePath, MAX_PATH, path);
	StringCchCat(enumeratePath, MAX_PATH, L"*");
	hFind = FindFirstFile(enumeratePath, &fd);
	do
	{
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			// 0 means true for lstrcmp
			if (wcslen(fd.cFileName) > 3 && fd.cFileName[0] == L'D' && fd.cFileName[1] == L'L' && fd.cFileName[2] == L'C')
			{
#ifdef LOGGING
				logFile << "writing toc for " << fd.cFileName << "\n";
#endif // LOGGING
				StringCchCopy(baseDir, MAX_PATH, path);
				StringCchCat(baseDir, MAX_PATH, fd.cFileName);
				StringCchCat(baseDir, MAX_PATH, L"\\");
				StringCchCopy(tocPath, MAX_PATH, baseDir);
				StringCchCat(tocPath, MAX_PATH, L"PCConsoleTOC.bin");
				writeTOC(tocPath, baseDir, true);
			}
		}
	} while (FindNextFile(hFind, &fd) != 0);
	FindClose(hFind);
#ifdef LOGGING
	logFile << "done\n";
	logFile.flush();
#endif // LOGGING
}

void writeTOC(TCHAR  tocPath[260], TCHAR  baseDir[260], bool isDLC)
{
	ofstream toc;
	toc.open(tocPath, ios::out | ios::binary | ios::trunc);
	//header
	write(toc, 0x3AB70C13);
	write(toc, 0x0);
	write(toc, 0x1);
	write(toc, 0x8);
	vector<fileData> files;
#ifdef LOGGING
	logFile << "getting files..\n";
#endif // LOGGING
	if (isDLC)
	{
		getFiles(files, baseDir, L"");
	}
	else
	{
		getFiles(files, baseDir, L"BIOGame\\");
	}
	int stringLength;
#ifdef LOGGING
	logFile << "writing file data..\n";
#endif // LOGGING
	CHAR filepath[MAX_PATH];
	const int numFiles = files.size();
	write(toc, numFiles);
	for (vector<fileData>::size_type i = 0; i != numFiles; i++)
	{
		size_t _;
		wcstombs_s(&_, filepath, files[i].fileName.c_str(), MAX_PATH);
		stringLength = strlen(filepath);
		if (i == files.size() - 1)
		{
			//last entry doesn't have to have a size for some reason
			write(toc, ushort(0));
		}
		else
		{	//size of entry: everything before the string, the stringlength, and the null character
			write(toc, ushort(0x1D + stringLength));
		}
		write(toc, static_cast<ushort>(0));
		write(toc, files[i].fileSize);
		write(toc, int(0));
		write(toc, int(0));
		write(toc, int(0));
		write(toc, int(0));
		write(toc, int(0));
		toc.write(filepath, stringLength);
		write(toc, byte(0));
	}
	toc.close();
}


void write(ofstream &file, void* data, const streamsize size) {
	file.write(static_cast<char*>(data), size);
}

void write(ofstream &file, byte data) {
	file.write(reinterpret_cast<char*>(&data), 1);
}

void write(ofstream &file, ushort data) {
	file.write(reinterpret_cast<char*>(&data), 2);
}

void write(ofstream &file, int data) {
	file.write(reinterpret_cast<char*>(&data), 4);
}

void getFiles(vector<fileData> &files, TCHAR* basepath, TCHAR* searchPath) {
	WIN32_FIND_DATA fd;
	LARGE_INTEGER fileSize;
	HANDLE hFind = INVALID_HANDLE_VALUE;
	TCHAR enumeratePath[MAX_PATH];
	TCHAR tmpPath[MAX_PATH];
	TCHAR* ext;
	StringCchCopy(enumeratePath, MAX_PATH, basepath);
	StringCchCat(enumeratePath, MAX_PATH, searchPath);
	StringCchCat(enumeratePath, MAX_PATH, L"*");

#ifdef LOGGING
	logFile << "enumerating files..\n";
#endif // LOGGING
	hFind = FindFirstFile(enumeratePath, &fd);
	do
	{
		StringCchCopy(tmpPath, MAX_PATH, searchPath);
		StringCchCat(tmpPath, MAX_PATH, fd.cFileName);
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			// 0 means true for lstrcmp
			if (lstrcmp(PathFindFileName(fd.cFileName), L"DLC") != 0 &&
				lstrcmp(PathFindFileName(fd.cFileName), L"Patches") != 0 &&
				lstrcmp(PathFindFileName(fd.cFileName), L"Splash") != 0 &&
				lstrcmp(PathFindFileName(fd.cFileName), L".") != 0 &&
				lstrcmp(PathFindFileName(fd.cFileName), L"..") != 0)
			{
#ifdef LOGGING
				logFile << "getting files from Directory:" << fd.cFileName << "\n";
#endif // LOGGING
				StringCchCat(tmpPath, MAX_PATH, L"\\");
				getFiles(files, basepath, tmpPath);
			}
			
		}
		else
		{
			//allowed = { "*.pcc", "*.afc", "*.bik", "*.bin", "*.tlk", "*.txt", "*.cnd", "*.upk", "*.tfc" }
			ext = PathFindExtension(tmpPath);
			if (lstrcmp(ext, L".pcc") == 0 ||
				lstrcmp(ext, L".afc") == 0 ||
				lstrcmp(ext, L".bik") == 0 ||
				lstrcmp(ext, L".bin") == 0 ||
				lstrcmp(ext, L".tlk") == 0 ||
				lstrcmp(ext, L".txt") == 0 ||
				lstrcmp(ext, L".cnd") == 0 ||
				lstrcmp(ext, L".upk") == 0 ||
				lstrcmp(ext, L".tfc") == 0)
			{
				fileSize.HighPart = fd.nFileSizeHigh;
				fileSize.LowPart = fd.nFileSizeLow;
				wstring fileName = tmpPath;
#ifdef LOGGING
				logFile << "found file: ";
				logFile.flush();
				logFile.write(fileName.c_str(), fileName.size() + 1);
				logFile << "\n";
#endif // LOGGING
				files.emplace_back(fileName, int(fileSize.QuadPart));
			}
			
		}
	} while (FindNextFile(hFind, &fd) != 0);
	FindClose(hFind);
}
