#include "pch.h"
#include <string>
#include <vector>
#include <Windows.h>
#include <mutex>
#include "JavaAPI.hpp"

class Pipeline {
public:
    Pipeline(const std::wstring& pipeName, size_t bufferSize);
    ~Pipeline();
    void StartServer();
    static DWORD WINAPI RunServer(LPVOID lpParam);
    static DWORD WINAPI ClientThread(LPVOID lpParam);
    bool ReadFromPipeWithTimeout(std::vector<char>& buffer, DWORD& bytesRead, DWORD timeoutMillis);
    bool ReadFromPipe(std::vector<char>& buffer, DWORD& bytesRead);
    bool WriteResponse(const std::string& response);
    void DisconnectAndClose();

private:
    HANDLE hPipe;
    std::wstring pipeName;
    size_t bufferSize;
    JavaAPI javaAPI;
    std::mutex mtx; // Mutex for thread-safety
    bool running;
};
