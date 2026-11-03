#pragma once
#include <windows.h>
HANDLE WINAPI TestServiceCreateEventW(LPSECURITY_ATTRIBUTES, BOOL, BOOL,
                                      LPCWSTR);
HANDLE WINAPI TestServiceCreateThread(LPSECURITY_ATTRIBUTES, SIZE_T,
                                      LPTHREAD_START_ROUTINE, LPVOID, DWORD,
                                      LPDWORD);
BOOL WINAPI TestServiceCloseHandle(HANDLE);
#define CreateEventW TestServiceCreateEventW
#define CreateThread TestServiceCreateThread
#define CloseHandle TestServiceCloseHandle
