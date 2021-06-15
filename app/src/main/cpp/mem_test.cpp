//
// Created by zhilong.lzl on 3/17/21.
//

#include <jni.h>
#include <android/log.h>
#include "string"
#include <map>
#include "vector"

using namespace std;

static constexpr size_t KB = 1024;
static constexpr size_t MB = KB * KB;

void *tryMalloc(size_t size) {
    void *stub = malloc(size * MB);

    if (stub == nullptr) {
        return tryMalloc(size / 2);
    } else {
        __android_log_print(ANDROID_LOG_INFO, "patrons", "malloc success, stub = %p, size = %d",
                            stub, size);

        return stub;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_me_zhilong_tools_abortkiller_demo_MainActivity_native_1alloc(JNIEnv *env, jobject thiz) {
    tryMalloc(100);
}

extern "C" JNIEXPORT void JNICALL
Java_me_zhilong_tools_abortkiller_demo_MainActivity_abort(JNIEnv *env, jobject thiz) {
//    abort();
    char *p = NULL;
    *p = '1';
}