#pragma once
#include "pch.h"
#include <string>
#include <sstream>
#include <jni.h>
#include <vector>
#include <unordered_map>
#include "JNICache.hpp"

typedef int (*ptr_GCJavaVMs)(JavaVM** vmBuf, jsize bufLen, jsize* nVMs);
typedef jobject(JNICALL* ptr_GetComponent)(JNIEnv* env, void* platformInfo);

struct AWTRectangle
{
    int x, y, width, height;
};

class JavaAPI {
public:
    JavaAPI();
    std::string ProcessInstruction(const std::string& instruction);
    jobject GrabCanvas();
    HWND GetCanvasHWND();
    HWND FindWindowWithClassName(const std::vector<HWND>& windows, const wchar_t* className);
    HWND FindWindowWithTitle(const std::vector<HWND>& windows, const wchar_t* windowTitle);
    HWND GetNestedCanvas(HWND parent, const wchar_t* className);
    std::string getComponentBounds(JNIEnv* env, jobject component);
    bool AttachToThread(JNIEnv** Thread);
    bool DetachThread(JNIEnv** Thread);
    jobject getClient();
    jobject getJShell();

    // Initialize cache in JavaAPI constructor
    JniCache* cache;

private:
    JavaVM* jvm;
    JNIEnv* env;
    ptr_GetComponent GetComponent;

    jobject injector;
    jobject client;
    jobject canvas;
    jobject shell;
    jmethodID eval;
    HWND clientHWND;
};
