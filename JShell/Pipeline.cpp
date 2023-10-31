#include "pch.h"
#include "Pipeline.hpp"
#include "JavaAPI.hpp"
#include <iostream>

Pipeline::Pipeline(const std::wstring& pipeName, size_t bufferSize)
    : pipeName(pipeName), bufferSize(bufferSize), hPipe(INVALID_HANDLE_VALUE) {
	running = false;
}

JavaAPI Pipeline::javaAPI;

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

bool Pipeline::ReadFromPipeWithTimeout(std::vector<char>& buffer, DWORD& bytesRead, DWORD timeoutMillis) {
    std::lock_guard<std::mutex> lock(mtx);
    buffer.resize(bufferSize);

    ULONGLONG startTime = GetTickCount64();
    ULONGLONG elapsedTime = 0;

    while (elapsedTime < timeoutMillis) {
        DWORD bytesAvailable = 0;
        if (!::PeekNamedPipe(hPipe, NULL, 0, NULL, &bytesAvailable, NULL)) {
            DWORD error = GetLastError();
            std::wcerr << L"Failed to peek named pipe. Error Code: " << error << std::endl;
            Sleep(10);  // wait for a second before retrying
            continue;
        }

        if (bytesAvailable > 0) {
            if (!::ReadFile(hPipe, buffer.data(), static_cast<DWORD>(buffer.size() - 1), &bytesRead, NULL)) {
                DWORD error = GetLastError();
                std::wcerr << L"Failed to read from timeout pipe. Error Code: " << error << std::endl;
                Sleep(10);  // wait for a second before retrying
                continue;
            }

            buffer[bytesRead] = '\0'; // safe null-termination
            return true;
        }

        // If data isn't ready yet, wait for a short interval and check again
        Sleep(10);
        elapsedTime = GetTickCount64() - startTime;
    }

    std::wcerr << L"Timeout: Failed to read from pipe within the specified timeout." << std::endl;
    Sleep(10);  // wait for a second before retrying
    return false;
}


DWORD WINAPI Pipeline::ClientThread(LPVOID lpParam) {
    Pipeline* pipeline = static_cast<Pipeline*>(lpParam);
    JavaAPI javaAPI;
    std::vector<char> buffer;
    DWORD bytesRead;
    int handshakeRetries = 3;

    while (handshakeRetries > 0) {
        if (pipeline->ReadFromPipeWithTimeout(buffer, bytesRead, 1000 /*timeout in ms*/)) {
            buffer[bytesRead] = '\0';
            std::string handshakeMessage(buffer.data());
            if (handshakeMessage == "READY") {
                std::cout << "Received handshake request." << std::endl;
                pipeline->WriteResponse("GO_AHEAD<END>");
                std::cout << "Sent handshake acknowledgment." << std::endl;
                break; // break out of handshake loop
            }
        }
        handshakeRetries--;
    }

    if (handshakeRetries <= 0) {
        std::cerr << "Failed to establish handshake after 3 retries." << std::endl;
        //DisconnectNamedPipe(pipeline->hPipe);
        pipeline->DisconnectAndClose();
        return 1; // Error code
    }

    // Now, continue reading instructions from the client until the client disconnects or an error occurs
    while (pipeline->running) {
        Sleep(10);
        if (pipeline->ReadFromPipeWithTimeout(buffer, bytesRead, 1000)) {
            buffer[bytesRead] = '\0';
            std::string instruction(buffer.data());
            std::cout << "Received instruction: " << instruction << std::endl;
            std::string response = javaAPI.ProcessInstruction(instruction);

            response += "<END>";

            std::cout << "Sending response: " << response << std::endl;
            pipeline->WriteResponse(response);
            
            Sleep(10);
        }
        else {
            // Possibly handle errors or disconnections here.
            break;  // break out if reading from the pipe fails, which implies client has disconnected.
        }
    }
    pipeline->DisconnectAndClose();
    //DisconnectNamedPipe(pipeline->hPipe);  // only disconnect once we're done with this client
    return 0;
}

DWORD WINAPI Pipeline::RunServer(LPVOID lpParam) {
    Pipeline* pipeline = static_cast<Pipeline*>(lpParam);
    pipeline->StartServer();

    // Main server loop. Wait for a connection, handle it, then wait for the next one.
    while (pipeline->running) {
        std::cout << "Waiting for a connection..." << std::endl;
        BOOL connected = ConnectNamedPipe(pipeline->hPipe, NULL);
        if (!connected) {
            if (GetLastError() == ERROR_PIPE_CONNECTED) {
                connected = TRUE;
            }
            else {
                pipeline->DisconnectAndClose();  // Close the current pipe handle
                pipeline->StartServer();        // Recreate the pipe
                continue;                       // Try to connect again
            }
        }

        if (connected) {
            std::cout << "Attempting handshake..." << std::endl;
            // Create a new thread to handle the client's requests.
            HANDLE hThread = CreateThread(NULL, 0, ClientThread, pipeline, 0, NULL);
            if (hThread) {
                // Wait for the client thread to finish. This ensures we handle one client at a time.
                WaitForSingleObject(hThread, INFINITE);
                CloseHandle(hThread);
            }
        }
    }

    return 0;
}