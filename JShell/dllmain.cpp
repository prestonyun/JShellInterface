#include "pch.h"
#include "JavaAPI.hpp"
#include <vector>
#include "Pipeline.hpp"

// Global handle for the server thread (if needed)
HANDLE serverThread = NULL;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    static Pipeline pipeline(L"\\\\.\\pipe\\jshellpipe", 65535); // Static initialization will persist

    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        serverThread = CreateThread(NULL, 0, Pipeline::RunServer, &pipeline, 0, NULL);
        if (!serverThread) {
            // Handle error, perhaps logging or alerting the user.
        }
        break;

    case DLL_PROCESS_DETACH:
        // Signal the server to stop
        pipeline.running = false;

        // Wait for the server thread to finish
        if (serverThread) {
            WaitForSingleObject(serverThread, INFINITE);

            // Close the handle:
            CloseHandle(serverThread);
        }
        break;
    }

    return TRUE;
}

