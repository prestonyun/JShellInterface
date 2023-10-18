#include "pch.h"
#include "Pipeline.hpp"
#include "JavaAPI.hpp"
#include <iostream>

Pipeline::Pipeline(const std::wstring& pipeName, size_t bufferSize)
    : pipeName(pipeName), bufferSize(bufferSize), hPipe(INVALID_HANDLE_VALUE) {}

Pipeline::~Pipeline() {
    DisconnectAndClose();
}

void Pipeline::StartServer() {
    hPipe = CreateNamedPipe(
        pipeName.c_str(),
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        static_cast<DWORD>(bufferSize),
        static_cast<DWORD>(bufferSize),
        0,
        NULL);

    if (hPipe == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        LPVOID lpMsgBuf = nullptr;

        FormatMessage(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
            NULL,
            error,
            0, // Default language
            (LPWSTR)&lpMsgBuf,
            0,
            NULL
        );

        // Display the error message in a message box
        MessageBox(NULL, (LPCTSTR)lpMsgBuf, L"Error", MB_OK | MB_ICONERROR);

        LocalFree(lpMsgBuf);
        throw std::runtime_error("Failed to create named pipe.");
    }
    else running = true;
}

bool Pipeline::ReadFromPipe(std::vector<char>& buffer, DWORD& bytesRead) {
    std::lock_guard<std::mutex> lock(mtx);
    buffer.resize(bufferSize);
    if (!::ReadFile(hPipe, buffer.data(), static_cast<DWORD>(buffer.size() - 1), &bytesRead, NULL)) {
        DWORD error = GetLastError();
        std::wcerr << L"Failed to read from pipe. Error Code: " << error << std::endl;
        return false;
    }

    buffer[bytesRead] = '\0'; // safe null-termination
    return true;
}


bool Pipeline::WriteResponse(const std::string& response) {
    std::lock_guard<std::mutex> lock(mtx);
    DWORD bytesWritten;
    if (!::WriteFile(hPipe, response.c_str(), static_cast<DWORD>(response.size()), &bytesWritten, NULL)) {
        std::cout << "Failed to write to pipe" << std::endl;
        return false;
    }
    FlushFileBuffers(hPipe);  // flush the pipe
    return true;
}

void Pipeline::DisconnectAndClose() {
    if (hPipe != INVALID_HANDLE_VALUE) {
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
        hPipe = INVALID_HANDLE_VALUE;
        running = false;
    }
}

DWORD WINAPI Pipeline::ClientThread(LPVOID lpParam) {
    Pipeline* pipeline = static_cast<Pipeline*>(lpParam);
    JavaAPI javaAPI;
    std::vector<char> buffer;
    DWORD bytesRead;

    while (pipeline->running) {
        if (pipeline->ReadFromPipe(buffer, bytesRead)) {
            buffer[bytesRead] = '\0';
            std::string instruction(buffer.data());
            std::cout << "Received instruction: " << instruction << std::endl;
            std::string response = javaAPI.ProcessInstruction(instruction);
            std::cout << "Sending response: " << response << std::endl;
            pipeline->WriteResponse(response);
        }
    }
    DisconnectNamedPipe(pipeline->hPipe);
    return 0;
}

DWORD WINAPI Pipeline::RunServer(LPVOID lpParam) {
    Pipeline* pipeline = static_cast<Pipeline*>(lpParam);
    pipeline->StartServer();

    while (pipeline->running) {
        BOOL connected = ConnectNamedPipe(pipeline->hPipe, NULL);
        if (!connected) {
            if (GetLastError() == ERROR_PIPE_CONNECTED) {
                connected = TRUE;
            }
            else if (GetLastError() == ERROR_PIPE_BUSY) {
                if (WaitNamedPipeW(pipeline->pipeName.c_str(), 3000 /* 3 seconds */)) {
                    continue;
                }
            }
            else {
                pipeline->DisconnectAndClose(); // Close the current pipe handle
                pipeline->StartServer(); // Recreate the pipe
                continue; // Try to connect again
            }
        }

        if (connected) {
            // Create a new thread to handle the client's requests.
            HANDLE hThread = CreateThread(NULL, 0, ClientThread, pipeline, 0, NULL);
            if (hThread) {
                CloseHandle(hThread);
            }
        }
    }

    return 0;
}


