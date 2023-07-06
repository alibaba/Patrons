//
// Created by zhilong.lzl on 4/15/21.
//

#include "patrons_core.h"

#include <elf.h>
#include <dlfcn.h>
#include <regex.h>
#include <jni.h>
#include <android/log.h>
#include <android/dlext.h>
#include <zlib.h>
#include <inttypes.h>
#include <pthread.h>
#include <sys/syscall.h>

#include "xhook/xhook.h"

/**
 * 解决华为 ThreadDexHotfixMonitor Abort
 * 默认不开启，非必要能力
 */
FILE *fix_huawei_fopen(const char *filename, const char *mode) {
    // 先通过字符串长度判断提高点性能
    if (filename && strlen(filename) == 53 && strcmp(filename, kHwWhiteList) == 0) {
        LOGE("found device try to load %s, block it", kHwWhiteList);
        // 如果是在子线程打开，特征和华为的 ThreadDexHotfixMonitor 是匹配的
        if ((int) syscall(SYS_gettid) != (int) syscall(SYS_getpid)) {
            pthread_exit(NULL);
        }
    }

    return __fopen(filename, mode);
}

/**
 * 找出所需要的各种函数，这里仅利用了 xhook 的解析 elf 找符号的能力
 * 简单的修改了一下 xhook，仅找符号，不进行 hook
 */
void FindSymbol(bool sync, bool isFixHuaweiBinder) {
    // xhook 开启段保护
    xhook_enable_sigsegv_protection(1);

    // 找一下 dlopen 和 dlsym
    xhook_register(".*/libdl.so$", "__loader_dlopen", NULL, (void **) (&__loader_dlopen), NULL);
    xhook_register(".*/libdl.so$", "__loader_dlsym", NULL, (void **) (&__loader_dlsym), NULL);

    // 找到 libart 中调用的 libartbase.so 中的方法 GetCmdLine, 没有特殊含义，仅仅是用来找一个和 libart.so 同一命名空间的函数来伪装身份
    xhook_register(".*/libart.so$", "_ZN3art10GetCmdLineEv", NULL, (void **) (&stub_method_in_art),
                   NULL);

    if (isFixHuaweiBinder && IsHuaweiBugDevice()) {
        // Hook FOPEN 监听华为特定机型加载hotfix白名单的，荣耀10和荣耀v10有这个bug
        xhook_register(".*/libart.so$", "fopen", fix_huawei_fopen, (void **) (&__fopen), NULL);
    }

    xhook_refresh(!sync);
}

/**
 * ClampGrowthLimit method when less than Android P
 */
void ClampGrowthLimit__(void *region_space, size_t new_capacity) {
    ExclusiveLock((void *) ((size_t) region_space + 0x38), ThreadCurrent());

    size_t used = *non_free_region_index_limit_ * kRegionSize;
    if (new_capacity > used) {
        *num_regions_ = new_capacity / kRegionSize;
        *limit_ = *begin_ + new_capacity;
        if ((*end_ - *begin_) > new_capacity) {
            *end_ = *limit_;
        }

        SetHeapSize(*(void **) ((size_t) region_space + offset_space_bitmap_in_region_space),
                    new_capacity);
        SetSize(*(void **) ((size_t) region_space + 0x20), new_capacity);
    }

    ExclusiveUnlock((void *) ((size_t) region_space + 0x38), ThreadCurrent());
}

/**
 * native init;
 */
int NativeInit() {
    if (!init_) {
        LOGD("start init, sdk = %s, api = %d, debuggable = %d, protect = %d, heap size config = %s",
             __PATRONS_API_VERSION, api_level, debuggable, has_exception_handle_, heapsize);

        LOGD("[device] brand = %s", brand);
        LOGD("[device] system brand = %s", system_brand);
        LOGD("[device] device = %s", device);
        LOGD("[device] rom version = %s", rom_version);
        LOGD("[device] fingerprint = %s", fingerprint);

        // 判断 Android 版本
        if (!IsAndroidVersionMatch()) {
            return ANDROID_VERSION_NOT_SUPPORT;
        }

        // 0.1 判断预先查找的函数是否都有
        if (!(__loader_dlopen && __loader_dlsym)) {
            LOGE("key method not found, %p, %p, %p", __loader_dlopen, __loader_dlsym,
                 stub_method_in_art);
            return LOADER_FUNCTION_NOT_FOUND;
        }

        // 1. 不同 Android OS Level 中 libart.so 的位置不同
        const char *lib_art_path_ = GetArtPath();

        // 1.1 这里会涉及到版本的区分，平台影响不大，< Q 的时候直接使用 libz 的 api 作为替身即可
        art_ = DLOPEN(lib_art_path_);

        if (art_ == NULL) {
            LOGE("art is NULL, art = %s", lib_art_path_);
            return ART_LOAD_FAILED;
        }

        LOGD("[instance] a_ = %p, art = %s", art_, lib_art_path_);

        // 2. 开始获取 Runtime
        runtime_ = (void *) (*(void **) (DLSYM(art_, kRuntimeInstance)));

        if (runtime_ == NULL) {
            LOGE("r_ is NULL");
            return RUNTIME_LOAD_FAILED;
        }

        LOGD("[instance] r_ = %p", runtime_);

        // 3. 通过偏移拿到 heap 实例
        heap_ = *(void **) ((size_t) runtime_ + offset_heap_in_runtime);

        if (heap_ == NULL) {
            LOGE("h_ is NULL, offset = %d", offset_heap_in_runtime);
            return HEAP_LOAD_FAILED;
        }

        LOGD("[instance] h_ = %p", heap_);

        // 4. 通过偏移获取 RegionSpace 实例
        region_space_ = *(void **) ((size_t) heap_ + offset_region_space_in_heap);

        if (region_space_ == NULL) {
            LOGE("r2 is NULL, offset = %d", offset_region_space_in_heap);
            return REGION_SPACE_LOAD_FAILED;
        }

        LOGD("[instance] r2 = %p", region_space_);

        // 定义在 RegionSpace 的父类 ContinuousSpace 中
        begin_ = (uint8_t **) ((size_t) region_space_ + 0x14);  // <<- RegionSpace::Dump()
        end_ = (uint8_t **) ((size_t) region_space_ + 0x18);
        limit_ = (uint8_t **) ((size_t) region_space_ + 0x1c);

        LOGD("[instance] b = %p, e = %p, l = %p", begin_, end_, limit_);

        num_regions_ = (size_t *) ((size_t) region_space_ + offset_num_regions_in_region_space);

        LOGD("[instance] n2 = %p", num_regions_);

        // 5. 通过偏移获取 non_free_region_index_limit_
        non_free_region_index_limit_ = (size_t *) ((size_t) region_space_ +
                                                   offset_region_limit_in_region_space);

        LOGD("[instance] r3 = %p", non_free_region_index_limit_);

        SetHeapSize = (SetHeapSize_) DLSYM(art_, kSetHeapSize);
        SetSize = (SetSize_) DLSYM(art_, kSetSize);

        if (api_level >= __ANDROID_API_P__) {
            ClampGrowthLimit = (ClampGrowthLimit_) DLSYM(art_, kClampGrowthLimit);

            if (ClampGrowthLimit == NULL) {
                LOGE("resize method is NULL");
                return RESIZE_METHOD_NOT_FOUND;
            }
        } else {
            // < Android P, no this method.
            ClampGrowthLimit = (ClampGrowthLimit_) ClampGrowthLimit__;

            ExclusiveLock = (ExclusiveLock_) DLSYM(art_, "_ZN3art5Mutex13ExclusiveLockEPNS_6ThreadE");
            if (ExclusiveLock == NULL) {
                LOGE("ExclusiveLock method is NULL");
                return EXCLUSIVELOCK_METHOD_NOT_FOUND;
            }

            ExclusiveUnlock = (ExclusiveUnlock_) DLSYM(art_, "_ZN3art5Mutex15ExclusiveUnlockEPNS_6ThreadE");
            if (ExclusiveUnlock == NULL) {
                LOGE("ExclusiveUnlock method is NULL");
                return EXCLUSIVEUNLOCK_METHOD_NOT_FOUND;
            }

            ThreadCurrent = (ThreadCurrent_) DLSYM(art_, "_ZN3art6Thread14CurrentFromGdbEv");
            if (ThreadCurrent == NULL) {
                LOGE("ThreadCurrent method is NULL");
                return THREADCURRENT_METHOD_NOT_FOUND;
            }
        }

        LOGD("[instance] m_ = %p", ClampGrowthLimit);

        // 最终验证一下整体偏移结果是否正确, 分别通过不同的偏移拿到的结果如果匹配，则前序链路计算的没有问题
        // num_regions_ * kRegionSize = NonGrowthLimitCapacity()
        size_t real_num_regions_ = NonGrowthLimitCapacity() / kRegionSize;
        if (*num_regions_ != real_num_regions_) {
            LOGE("final check failed, m_ %d not match l_ %d", *num_regions_, real_num_regions_);
            return FINAL_CHECK_FAILED;
        } else {
            LOGI("region space is %d mb, has %d regions.", NonGrowthLimitCapacity() / MB,
                 *num_regions_);
        }

        init_ = true;

        LOGI("patrons native init success.");
    }

    return SUCCESS;
}

JNIEXPORT jint JNICALL
Java_com_alibaba_android_patronus__1Patrons__1_1init(__unused JNIEnv *env, __unused jclass clazz,
                                                     jboolean sync,
                                                     jboolean _debuggable,
                                                     jboolean isFixHuaweiBinder) {
    // 1. Flag 设置
    debuggable = _debuggable;

    if (debuggable) {
        LOGE("[warning] debuggable is enable, will disable sgev protection, MUST CLOSE it before release.");
    }

    // 清理日志dump区
    CleanLogBuffer();

    // 2. 基础环境初始化
    InitEnv();

    // 3. 设置各个版本的偏移
    DefineOffset();

    // 4. 找一下第一层的函数
    FindSymbol(sync, isFixHuaweiBinder);

    // 5. 注册异常信号处理
    LOGD("register signal handler");
    if (HandleSignal(SIGSEGV) != 0) {
        LOGE("signal handler reg failed.");
    } else {
        has_exception_handle_ = true;
        LOGI("signal handler reg success, old handler = %p", &sig_act_old);
    }

    int initCode;

    if (has_exception_handle_ && !debuggable) {
        i_want_handle_signal_flag = 1;
        if (0 == sigsetjmp(time_machine, 1)) {
            // 6. real init
            initCode = NativeInit();
        } else {
            LOGE("native init failed, found exception signal.");
            initCode = NATIVE_INIT_SEGMENT_FAULT;
        }

        i_want_handle_signal_flag = 0;
    } else {
        initCode = NativeInit();
    }

    return initCode;
}

/**
 * 调整 Region Space 大小
 * @param new_size 调整后的大小
 */
bool ResizeRegionSpace(jint new_size) {
    if (new_size <= 0) {
        LOGD("target size (%d) is too small!", new_size);
        return false;
    }

    size_t used = *non_free_region_index_limit_ * kRegionSize;
    size_t nonGrowLimit = NonGrowthLimitCapacity();

    LOGD("current has %d regions, used = %d mb, max = %d mb, target = %d mb",
         *non_free_region_index_limit_,
         used / MB,
         nonGrowLimit / MB,
         new_size / MB);

    if (new_size > nonGrowLimit) {
        LOGE("can not grow region space, new size = %d, but limit size = %d", new_size / MB,
             nonGrowLimit / MB);
        return false;
    }

    if (new_size > used) {
        ClampGrowthLimit(region_space_, new_size);

        LOGI("it has been resize into %zu mb.", new_size / MB);
    } else {
        LOGE("resize failed, new size (%d) mb bellow current used size (%d) mb", new_size / MB,
             used / MB);

        return false;
    }

    return true;
}

JNIEXPORT jboolean JNICALL
Java_com_alibaba_android_patronus__1Patrons_shrinkRegionSpace(__unused JNIEnv *env,
                                                              __unused jclass clazz,
                                                              jint new_size) {
    if (!IsAndroidVersionMatch()) {
        return false;
    }

    if (!init_) {
        LOGE("init patrons first!");
        return false;
    }

    bool ret = false;
    if (ClampGrowthLimit && region_space_) {
        if (has_exception_handle_ && !debuggable) {
            i_want_handle_signal_flag = 1;
            if (0 == sigsetjmp(time_machine, 1)) {
                ret = ResizeRegionSpace(new_size * MB);
            } else {
                LOGE("resize failed, found exception signal.");
                return false;
            }

            i_want_handle_signal_flag = 0;
        } else {
            ret = ResizeRegionSpace(new_size * MB);
        }
    } else {
        LOGE("resize failed, key param is NULL, instance = %p, method = %p.",
             region_space_,
             ClampGrowthLimit);

        return false;
    }

    return ret;
}

JNIEXPORT jlong JNICALL
Java_com_alibaba_android_patronus__1Patrons_getCurrentRegionSpaceSize(__unused JNIEnv *env,
                                                                      __unused jclass clazz) {
    if (!init_) {
        LOGE("init patrons first!");
        return -1;
    }

    return NonGrowthLimitCapacity();
}

JNIEXPORT jstring JNICALL
Java_com_alibaba_android_patronus__1Patrons_dumpLogs(JNIEnv *env, jclass clazz,
                                                     jboolean cleanAfterDump) {
    pthread_mutex_lock(&log_lock);

    char current_cursor = dump_cursor;

    jstring logs;

    if (current_cursor <= 0) {
        logs = (*env)->NewStringUTF(env, "the native log buffer is empty");
    } else {
        char *tmp = malloc(current_cursor * 256 * sizeof(char));
        memset(tmp, 0, current_cursor * 256 * sizeof(char));
        strcat(tmp, "\nPatrons Core Dump: ");
        strcat(tmp, __PATRONS_API_VERSION);
        strcat(tmp, "↵\n");

        // 按行拼接日志
        for (int i = 0; i < current_cursor; i++) {
            if (dump_logs[i] != NULL) {
                strcat(tmp, dump_logs[i]);
                strcat(tmp, "↵\n");
            }
        }

        strcat(tmp, "\n");

        logs = (*env)->NewStringUTF(env, tmp);
        free(tmp);

        // Dump 完成后清理缓冲区
        if (cleanAfterDump) {
            CleanLogBuffer();
        }
    }

    pthread_mutex_unlock(&log_lock);

    return logs;
}