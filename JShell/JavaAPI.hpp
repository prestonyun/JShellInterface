#pragma once
#include "pch.h"
#include <string>
#include <sstream>
#include <jni.h>
#include <vector>

typedef int (*ptr_GCJavaVMs)(JavaVM** vmBuf, jsize bufLen, jsize* nVMs);
typedef jobject(JNICALL* ptr_GetComponent)(JNIEnv* env, void* platformInfo);

enum ReturnType {
    VOID_TYPE = 'V',
    BOOLEAN_TYPE = 'Z',
    INT_TYPE = 'I',
    OBJECT_TYPE = 'L',
    LONG_TYPE = 'J',
    STRING_TYPE = 'S',
    JSTRING_TYPE = 's',
    ERROR_TYPE = -1,
    EXCEPTION_TYPE = -2,
    // Add other types as needed
};

struct JavaReturnValue {
    union {
        jobject objectValue;
        jboolean booleanValue;
        jint intValue;
        jdouble doubleValue;
        jlong longValue;
        jstring jStringValue;
        // Add other JNI types as needed
    };
    ReturnType type; // A enum value indicating the type of the value
    std::string statement = "";
};

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
