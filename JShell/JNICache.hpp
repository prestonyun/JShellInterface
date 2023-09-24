// JniCache.hpp
#pragma once
#include "pch.h"
#include <jni.h>
#include <unordered_map>
#include <string>

class JniCache {
public:
    // Cache for method IDs, using className::methodName as the key.
    std::unordered_map<std::string, jmethodID> methodCache;
    // Cache for class objects, using className as the key.
    std::unordered_map<std::string, jclass> classCache;

    // Method to get a class object from the cache, or find and add it to the cache.
    jclass getClass(JNIEnv* env, const std::string& name, jobject object) {
        auto it = classCache.find(name);
        if (it != classCache.end()) {
            return it->second;
        }

        jclass cls = env->GetObjectClass(object);
        classCache[name] = cls;
        return cls;
    }

    // Method to get a method ID from the cache, or find and add it to the cache.
    jmethodID getMethodID(JNIEnv* env, const std::string& key, jclass clazz, const char* name, const char* sig) {
        auto it = methodCache.find(key);
        if (it != methodCache.end()) {
            return it->second;
        }

        jmethodID methodID = env->GetMethodID(clazz, name, sig);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return nullptr;
        }

        methodCache[key] = methodID;
        return methodID;
    }

    // Constructor, can be used to preload some classes into the cache.
    JniCache() = default;

    // Destructor for cleanup.
    ~JniCache() {
        // Handle the cleanup, if needed.
        // Remember to delete global references, if any.
    }
};
