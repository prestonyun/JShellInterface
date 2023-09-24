#include "pch.h"
#include "JavaAPI.hpp"

void DisplayErrorMessage(const std::wstring& message) {
    MessageBoxW(NULL, message.c_str(), L"Error", MB_OK | MB_ICONERROR);
}

void checkAndClearException(JNIEnv* env) {
    // Check if an exception occurred
    if (env->ExceptionCheck()) {
        // Get the exception object
        jthrowable exception = env->ExceptionOccurred();

        // Clear the exception to be able to call further JNI methods
        env->ExceptionClear();

        // Retrieve the toString() representation of the exception
        jclass throwableClass = env->FindClass("java/lang/Throwable");
        jmethodID toStringMethod = env->GetMethodID(throwableClass, "toString", "()Ljava/lang/String;");
        jstring exceptionString = (jstring)env->CallObjectMethod(exception, toStringMethod);
        const char* exceptionCString = env->GetStringUTFChars(exceptionString, JNI_FALSE);

        // Display the exception message
        std::wstring message = L"JNI Exception: " + std::wstring(exceptionCString, exceptionCString + strlen(exceptionCString));
        MessageBox(NULL, message.c_str(), L"JNI Exception", MB_OK | MB_ICONERROR);

        // Clean up local references and release string memory
        env->ReleaseStringUTFChars(exceptionString, exceptionCString);
        env->DeleteLocalRef(exceptionString);
        env->DeleteLocalRef(throwableClass);
    }
}

JavaAPI::JavaAPI() {
    jvm = nullptr;
    env = nullptr;

    GetComponent = nullptr;

    injector = nullptr;
    client = nullptr;
    canvas = nullptr;
    shell = nullptr;
    eval = nullptr;
    clientHWND = nullptr;
    cache = new JniCache();

    jsize nVMs;
    jint ret = JNI_GetCreatedJavaVMs(&jvm, 1, &nVMs);
    if (ret != JNI_OK || nVMs == 0) {
        DisplayErrorMessage(L"No JVM found");
        exit(1);
    }
    ret = jvm->AttachCurrentThread((void**)&env, NULL);
    if (ret != JNI_OK) {
        DisplayErrorMessage(L"Failed to attach to JVM");
        exit(1);
    }

}

static BOOL CALLBACK GetHWNDCurrentPID(HWND WindowHandle, LPARAM lParam)
{
    auto handles = reinterpret_cast<std::vector<HWND>*>(lParam);
    DWORD currentPID = GetCurrentProcessId();
    DWORD windowPID;
    GetWindowThreadProcessId(WindowHandle, &windowPID);

    if (currentPID == windowPID)
    {
        handles->push_back(WindowHandle);
    }


    return TRUE;
}

HWND JavaAPI::FindWindowWithClassName(const std::vector<HWND>& windows, const wchar_t* className)
{
    wchar_t nameBuffer[128];

    for (auto window : windows)
    {
        GetClassNameW(window, nameBuffer, sizeof(nameBuffer) / sizeof(wchar_t));  // Using the Unicode version
        if (wcscmp(nameBuffer, className) == 0)  // Wide string comparison
            return window;
    }
    DisplayErrorMessage(L"Failed to find window with class name");
    return nullptr;
}

HWND JavaAPI::FindWindowWithTitle(const std::vector<HWND>& windows, const wchar_t* windowTitle)
{
    wchar_t titleBuffer[256]; // Adjust the size as needed

    for (auto window : windows)
    {
        GetWindowTextW(window, titleBuffer, sizeof(titleBuffer) / sizeof(wchar_t));  // Using the Unicode version
        if (wcscmp(titleBuffer, windowTitle) == 0)  // Wide string comparison
            return window;
    }
    return nullptr;
}

HWND JavaAPI::GetNestedCanvas(HWND parent, const wchar_t* className) {
    HWND currentChild = GetWindow(parent, GW_CHILD);
    while (currentChild) {
        if (FindWindowWithClassName({ currentChild }, className)) {
            HWND nestedChild = GetNestedCanvas(currentChild, className);
            if (nestedChild) {
                return nestedChild;
            }
        }
        currentChild = GetWindow(currentChild, GW_HWNDNEXT);
    }
    return nullptr;
}


bool JavaAPI::AttachToThread(JNIEnv** Thread)
{
    if (this->jvm)
        if (this->jvm->GetEnv((void**)Thread, JNI_VERSION_1_6) == JNI_EDETACHED)
            this->jvm->AttachCurrentThread((void**)Thread, nullptr);
    return (*Thread);
}

bool JavaAPI::DetachThread(JNIEnv** Thread)
{
    if (*Thread)
        if (this->jvm)
            if (this->jvm->DetachCurrentThread() == JNI_OK)
                *Thread = nullptr;
    return !(*Thread);
}

HWND JavaAPI::GetCanvasHWND() {
    std::vector<HWND> matchedWindows;
    EnumWindows(GetHWNDCurrentPID, reinterpret_cast<LPARAM>(&matchedWindows));

    //HWND frameHandle = FindWindowWithClassName(matchedWindows, L"SunAwtFrame");
    HWND frameHandle = FindWindowWithTitle(matchedWindows, L"RuneLite");

    if (!frameHandle) {
        DisplayErrorMessage(L"Failed to find frame");
        return nullptr; // No parent frame found.
    }
    HWND canvasHandle = GetWindow(frameHandle, GW_CHILD);
    if (!canvasHandle) {
        DisplayErrorMessage(L"Failed to find canvas");
        return nullptr;
    }
    clientHWND = frameHandle;
    return canvasHandle;
}

jobject JavaAPI::getJShell() {
    // Get the ShellPanel class
    jclass shellPanelClass = env->FindClass("com/hydratech/jshell/ShellPanel");
    if (!shellPanelClass) {
        DisplayErrorMessage(L"Failed to find ShellPanel class");
        return nullptr;
    }

    // Get the shell field
    jfieldID shellFieldID = env->GetFieldID(shellPanelClass, "shell", "Ljdk/jshell/JShell;");
    if (!shellFieldID) {
        DisplayErrorMessage(L"Failed to find ShellPanel class");
        return nullptr;
    }

    // 3. Call the setAccessible method on the Field object
    // First, get the Field class and setAccessible method ID
    jclass fieldClass = env->FindClass("java/lang/reflect/Field");
    jmethodID setAccessibleMethod = env->GetMethodID(fieldClass, "setAccessible", "(Z)V");
    if (!setAccessibleMethod) {
        DisplayErrorMessage(L"Failed to find ShellPanel class");
        return nullptr;
    } // method not found


    // Get the INSTANCE of ShellPanel
    jfieldID instanceFieldID = env->GetStaticFieldID(shellPanelClass, "INSTANCE", "Lcom/hydratech/jshell/ShellPanel;");
    if (!instanceFieldID) {
        DisplayErrorMessage(L"Failed to find ShellPanel class");
        return nullptr;
    }

    jobject shellPanelInstance = env->GetStaticObjectField(shellPanelClass, instanceFieldID);
    if (!shellPanelInstance) {
        DisplayErrorMessage(L"Failed to find ShellPanel class");
        return nullptr;
    }

    if (this->injector == nullptr)
    {
        getClient();
    }
    jmethodID switchContext = env->GetMethodID(shellPanelClass, "switchContext", "(Lcom/google/inject/Injector;)V");
    if (!switchContext) {
        DisplayErrorMessage(L"Failed to find ShellPanel class");
        return nullptr;
    }
    env->CallVoidMethod(shellPanelInstance, switchContext, this->injector);
    checkAndClearException(env);

    // Get the JShell object
    this->shell = env->GetObjectField(shellPanelInstance, shellFieldID);
    checkAndClearException(env);

    jclass shellClass = env->GetObjectClass(shell);
    jmethodID eval = env->GetMethodID(shellClass, "eval", "(Ljava/lang/String;)Ljava/util/List;");
    this->eval = eval;

    return shell;
}

jobject JavaAPI::getClient() {
    jclass runeLiteClass = env->FindClass("net/runelite/client/RuneLite");
    if (env->ExceptionCheck()) {
        MessageBoxW(NULL, L"Failed to find RuneLite class", L"Error", MB_OK | MB_ICONERROR);
        env->ExceptionClear();
        return nullptr; // or handle the error as appropriate
    }

    jfieldID injectorField = env->GetStaticFieldID(runeLiteClass, "injector", "Lcom/google/inject/Injector;");
    if (env->ExceptionCheck()) {
        MessageBoxW(NULL, L"Failed to find injector field", L"Error", MB_OK | MB_ICONERROR);
        env->ExceptionClear();
        return nullptr; // or handle the error as appropriate
    }

    jobject injector = env->GetStaticObjectField(runeLiteClass, injectorField);
    if (env->ExceptionCheck()) {
        MessageBoxW(NULL, L"Failed to find injector object", L"Error", MB_OK | MB_ICONERROR);
        env->ExceptionClear();
        return nullptr; // or handle the error as appropriate
    }
    this->injector = injector;
    jclass injectorClass = env->GetObjectClass(injector);
    if (env->ExceptionCheck()) {
        MessageBoxW(NULL, L"Failed to find injector class", L"Error", MB_OK | MB_ICONERROR);
        env->ExceptionClear();
        return nullptr; // or handle the error as appropriate
    }

    jmethodID getInstanceMethod = env->GetMethodID(injectorClass, "getInstance", "(Ljava/lang/Class;)Ljava/lang/Object;");
    if (env->ExceptionCheck()) {
        MessageBoxW(NULL, L"Failed to find injector instance", L"Error", MB_OK | MB_ICONERROR);
        env->ExceptionClear();
        return nullptr; // or handle the error as appropriate
    }

    jobject runeLiteClient = env->CallObjectMethod(injector, getInstanceMethod, runeLiteClass);
    if (env->ExceptionCheck()) {
        MessageBoxW(NULL, L"Failed to call injector method", L"Error", MB_OK | MB_ICONERROR);
        env->ExceptionClear();
        return nullptr; // or handle the error as appropriate
    }
    jclass runeLiteClientClass = env->GetObjectClass(runeLiteClient);
    if (env->ExceptionCheck()) {
        MessageBoxW(NULL, L"Failed to find client class", L"Error", MB_OK | MB_ICONERROR);
        env->ExceptionClear();
        return nullptr; // or handle the error as appropriate
    }

    jfieldID clientField = env->GetFieldID(runeLiteClientClass, "client", "Lnet/runelite/api/Client;");
    if (env->ExceptionCheck()) {
        MessageBoxW(NULL, L"Failed to find client field", L"Error", MB_OK | MB_ICONERROR);
        env->ExceptionClear();
        return nullptr; // or handle the error as appropriate
    }

    jobject client = env->GetObjectField(runeLiteClient, clientField);
    if (env->ExceptionCheck()) {
        MessageBoxW(NULL, L"Failed to find client object field", L"Error", MB_OK | MB_ICONERROR);
        env->ExceptionClear();
        return nullptr; // or handle the error as appropriate
    }

    jclass clientClass = env->GetObjectClass(client);
    if (env->ExceptionCheck()) {
        MessageBoxW(NULL, L"Failed to find client object class", L"Error", MB_OK | MB_ICONERROR);
        env->ExceptionClear();
        return nullptr; // or handle the error as appropriate
    }
    checkAndClearException(env);
    this->client = env->NewGlobalRef(client);
    return client;
}


jobject JavaAPI::GrabCanvas() {
    HMODULE jvmDLL = GetModuleHandle(L"jvm.dll");
    if (!jvmDLL) {
        MessageBoxW(NULL, L"get jvm.ll failure", L"", MB_OK | MB_ICONERROR);
        return nullptr;
    }

    ptr_GCJavaVMs getJVMs = (ptr_GCJavaVMs)GetProcAddress(jvmDLL, "JNI_GetCreatedJavaVMs");
    if (!getJVMs) {
        MessageBoxW(NULL, L"get jvm failure", L"", MB_OK | MB_ICONERROR);
        return nullptr;
    }
    JNIEnv* thread = nullptr;

    do {
        getJVMs(&(this->jvm), 1, nullptr);
        if (!this->jvm) {
            MessageBoxW(NULL, L"get jvm failure2", L"", MB_OK | MB_ICONERROR);
            break;
        }

        this->AttachToThread(&env);

        HMODULE awtDLL = GetModuleHandle(L"awt.dll");
        if (!awtDLL) {
            MessageBoxW(NULL, L"get awt dll failure", L"", MB_OK | MB_ICONERROR);
            break;
        }

        const char* awtFuncName = (sizeof(void*) == 8) ? "DSGetComponent" : "_DSGetComponent@8";
        this->GetComponent = (ptr_GetComponent)GetProcAddress(awtDLL, awtFuncName);
        if (!env || !this->GetComponent) {
            MessageBoxW(NULL, L"get component failure", L"", MB_OK | MB_ICONERROR);
            break;
        }

        HWND canvasHWND = GetCanvasHWND();
        if (!canvasHWND) {
            MessageBoxW(NULL, L"get handle failure", L"", MB_OK | MB_ICONERROR);
            break;
        }
        jobject tempCanvas = this->GetComponent(env, (void*)canvasHWND);
        if (!tempCanvas) {
            MessageBoxW(NULL, L"get component failure", L"", MB_OK | MB_ICONERROR);
            break;
        }

        jclass canvasClass = env->GetObjectClass(tempCanvas);
        if (!canvasClass) {
            MessageBoxW(NULL, L"canvas object class failure", L"", MB_OK | MB_ICONERROR);
            break;
        }

        jmethodID canvas_getParent = env->GetMethodID(canvasClass, "getParent", "()Ljava/awt/Container;");
        if (!canvas_getParent) {
            MessageBoxW(NULL, L"get parent failure", L"", MB_OK | MB_ICONERROR);
            break;
        }

        jobject tempClient = env->CallObjectMethod(tempCanvas, canvas_getParent);
        if (tempClient) {
            this->canvas = env->NewGlobalRef(tempClient);
            env->DeleteLocalRef(tempClient);
            return this->canvas;
        }
        else {
            MessageBoxW(NULL, L"get client failure", L"", MB_OK | MB_ICONERROR);
            break;
        }
        checkAndClearException(env);
        this->canvas = env->NewGlobalRef(tempClient);
        return this->canvas;
    } while (false);
    return nullptr;
}

std::string JavaAPI::ProcessInstruction(const std::string& instruction) {
    std::string result = "";
    if (!this->env) {
        GrabCanvas();
    }
    if (!this->shell) {
        getJShell();
    }
    if (this->shell) {
        jstring jString = env->NewStringUTF(instruction.c_str());
        jobject snippetList = env->CallObjectMethod(shell, eval, jString);
        if (snippetList == nullptr) {
            DisplayErrorMessage(L"Failed to get snippet list");
            return result;
        }
        checkAndClearException(env);
        jclass listClass = this->cache->getClass(env, "SnippetList", snippetList);//env->GetObjectClass(snippetList);
        if (listClass == nullptr) {
            DisplayErrorMessage(L"Failed to get list class");
            return result;
        }
        checkAndClearException(env);
        jmethodID sizeMethod = this->cache->getMethodID(env, "SnippetList_size", listClass, "size", "()I");
        if (sizeMethod == nullptr) {
            DisplayErrorMessage(L"Failed to get size method");
            return result;
        }
        checkAndClearException(env);
        jint listSize = env->CallIntMethod(snippetList, sizeMethod);
        if (listSize == 0) {
            DisplayErrorMessage(L"List size is 0");
            return result;
        }
        checkAndClearException(env);
        jmethodID getMethod = this->cache->getMethodID(env, "SnippetList_get", listClass, "get", "(I)Ljava/lang/Object;");
        if (getMethod == nullptr) {
            DisplayErrorMessage(L"Failed to get get method");
            return result;
        }
        checkAndClearException(env);
        std::string resultString = "";
        for (jint i = 0; i < listSize; i++) {
            jobject snippet = env->CallObjectMethod(snippetList, getMethod, i);
            jclass snippetClass = this->cache->getClass(env, "Snippet", snippet);
            jmethodID valueMethod = this->cache->getMethodID(env, "ValueMethod", snippetClass, "value", "()Ljava/lang/String;");
            jstring valueString = (jstring)env->CallObjectMethod(snippet, valueMethod);
            if (valueString == nullptr) {
                jmethodID exception = env->GetMethodID(snippetClass, "exception", "()Ljdk/jshell/JShellException;");
                jobject exceptionObject = env->CallObjectMethod(snippet, exception);
                if (exceptionObject == nullptr) {
                    jmethodID toString = env->GetMethodID(snippetClass, "toString", "()Ljava/lang/String;");
                    jstring toStringString = (jstring)env->CallObjectMethod(snippet, toString);
                    if (toStringString != nullptr) {
                        resultString += (std::string)env->GetStringUTFChars(toStringString, NULL);
                        continue;
                    }
                }
                else {
                    jclass exceptionClass = env->GetObjectClass(exceptionObject);
                    jmethodID getMessage = env->GetMethodID(exceptionClass, "getMessage", "()Ljava/lang/String;");

                    jstring message = (jstring)env->CallObjectMethod(exceptionObject, getMessage);
                    if (message == nullptr) {
                        break;
                    }
                    checkAndClearException(env);
                    const char* utf8Chars = env->GetStringUTFChars(message, NULL);
                    resultString += (std::string)utf8Chars;
                    break;
                }
            }
            checkAndClearException(env);

            const char* utf8Chars = env->GetStringUTFChars(valueString, NULL);
            resultString += (std::string)utf8Chars;
        }

        return resultString;
    }
    else {
        DisplayErrorMessage(L"Failed to get shell");
        return result;
    }
}

std::string JavaAPI::getComponentBounds(JNIEnv* env, jobject component)
{
    // Get the component's class
    jclass componentClass = env->GetObjectClass(component);

    // Get the method IDs
    jmethodID getPointLoc = env->GetMethodID(componentClass, "getLocationOnScreen", "()Ljava/awt/Point;");
    jobject point = env->CallObjectMethod(component, getPointLoc);
    jclass pointClass = env->GetObjectClass(point);
    if (!getPointLoc || !point || !pointClass)
    {
        return "";
    }
    jmethodID getXMethod = env->GetMethodID(pointClass, "getX", "()I");
    jmethodID getYMethod = env->GetMethodID(pointClass, "getY", "()I");
    if (!getXMethod || !getYMethod)
    {
        getXMethod = env->GetMethodID(pointClass, "getX", "()D");
        getYMethod = env->GetMethodID(pointClass, "getY", "()D");
    }
    jmethodID getWidth = env->GetMethodID(componentClass, "getWidth", "()I");
    jmethodID getHeight = env->GetMethodID(componentClass, "getHeight", "()I");
    if (!getXMethod || !getYMethod || !getWidth || !getHeight) return ""; // Error handling

    // Call the methods
    double x = env->CallDoubleMethod(point, getXMethod);
    double y = env->CallDoubleMethod(point, getYMethod);
    int w = env->CallIntMethod(component, getWidth);
    int h = env->CallIntMethod(component, getHeight);
    if (!x || !y || !w || !h) return "";

    // Optionally, handle exceptions if the method calls throw any
    if (env->ExceptionCheck())
    {
        env->ExceptionDescribe();  // prints exception description to stderr
        env->ExceptionClear();
        // handle the exception, e.g., by returning a default Rectangle or by other means
    }
    std::stringstream ss;
    ss << x << " " << y << " " << w << " " << h;
    return ss.str();
}
