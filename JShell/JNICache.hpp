// JniCache.hpp
#pragma once
#include "pch.h"
#include <jni.h>
#include <unordered_map>
#include <string>

class JniCache {
public:
    static JniCache& getInstance() {
        static JniCache instance; // Guaranteed to be lazy initialized and destroyed correctly
        return instance;
    }

    JniCache(const JniCache&) = delete; // Prevent copy
    void operator=(const JniCache&) = delete; // Prevent assignment

    // Cache for method IDs, using className::methodName as the key.
    std::unordered_map<std::string, jmethodID> methodCache;
    // Cache for class objects, using className as the key.
    std::unordered_map<std::string, jclass> classCache;
    std::unordered_map<std::string, jobject> objectCache;
    std::unordered_map<std::string, jfieldID> fieldCache;

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

    // Method to get a jobject from the cache, or find and add it to the cache.
    jobject getObject(JNIEnv* env, const std::string& key, jclass clazz, const char* name, const char* sig) {
		auto it = objectCache.find(key);
        if (it != objectCache.end()) {
			return it->second;
		}

		jobject object = env->GetStaticObjectField(clazz, env->GetStaticFieldID(clazz, name, sig));
        if (env->ExceptionCheck()) {
			env->ExceptionClear();
			return nullptr;
		}

		objectCache[key] = object;
		return object;
	}

    // Method to get a jfieldID from the cache, or find and add it to the cache.
    jfieldID getFieldID(JNIEnv* env, const std::string& key, jclass clazz, const char* name, const char* sig) {
		auto it = fieldCache.find(key);
        if (it != fieldCache.end()) {
			return it->second;
		}

		jfieldID fieldID = env->GetFieldID(clazz, name, sig);
        if (env->ExceptionCheck()) {
			env->ExceptionClear();
			return nullptr;
		}

		fieldCache[key] = fieldID;
		return fieldID;
	}

    // Destructor for cleanup.
    ~JniCache() {
        // Handle the cleanup, if needed.
        // Remember to delete global references, if any.
    }

private:
    // Constructor, can be used to preload some classes into the cache.
    JniCache() = default;
};
