//
// Created by zhilong.lzl on 4/15/21.
//

#ifndef ABORT_KILLER_PATRONS_CORE_H
#define ABORT_KILLER_PATRONS_CORE_H

#include <malloc.h>
#include <dlfcn.h>
#include <regex.h>
#include "string.h"
#include <jni.h>
#include <android/log.h>
#include <android/dlext.h>
#include <zlib.h>
#include <setjmp.h>
#include <errno.h>
#include <stdarg.h>
#include <elf.h>
#include <pthread.h>

// android api 定义
#define __ANDROID_API_R__ 30
#define __ANDROID_API_S__ 31

// patrons version 定义
#define __PATRONS_API_VERSION "1.1.0"

char *dump_logs[128] = {0};
char dump_cursor = 0;

void __log_dump(const char *fmt, ...) {
    char *buffer = malloc(256 * sizeof(char));

    va_list ap;
    va_start(ap, fmt);
    vsprintf(buffer, fmt, ap);
    va_end(ap);

    char **current = &dump_logs[dump_cursor++ % 128];
    if (*current != NULL) {
        free(*current);
    }

    *current = buffer;
}

// 日志定义
#define LOG_TAG "Patrons-Native"
#define LOGD(...) { __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__); __log_dump(__VA_ARGS__); }
#define LOGI(...) { __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__); __log_dump(__VA_ARGS__); }
#define LOGE(...) { __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__); __log_dump(__VA_ARGS__); }

// dl 族符号宏
#define DLOPEN(name) (__loader_dlopen ? __loader_dlopen(name, RTLD_LAZY, ((void *) stub_method_in_art) ? ((void *) stub_method_in_art) : (void *) zlibVersion) : (void*) NULL)
#define DLSYM(handler, name) (__loader_dlsym ? __loader_dlsym(handler, name, ((void *) stub_method_in_art) ? ((void *) stub_method_in_art) : (void *) zlibVersion) : (void*) NULL)

// 基本的空间单位
static const size_t KB = 1024;
static const size_t MB = KB * KB;

static const char *kRuntimeInstance = "_ZN3art7Runtime9instance_E";
static const char *kClampGrowthLimit = "_ZN3art2gc5space11RegionSpace16ClampGrowthLimitEj";
static const char *kSetHeapSize = "_ZN3art2gc10accounting11SpaceBitmapILj4096EE11SetHeapSizeEj";
static const char *kSetSize = "_ZN3art6MemMap7SetSizeEj";

static const char *kHwWhiteList = "/system/etc/hotfixchecker/hotfixchecker_whitelist.cfg";

// The region size.
static const size_t kRegionSize = 256 * KB;

// 错误码
enum Code {
    SUCCESS,
    ERROR_READ_VSS_FAILED __unused = 1001,
    SIGNAL_HANDLER_REG_ERROR,
    ANDROID_VERSION_NOT_SUPPORT = 2001,
    HEAP_SIZE_IS_NOT_BIG_ENOUGH __unused,
    LOWER_LIMIT_IS_TOO_SMALL __unused,
    ART_LOAD_FAILED = 3001,
    RUNTIME_LOAD_FAILED,
    HEAP_LOAD_FAILED,
    REGION_SPACE_LOAD_FAILED,
    LOADER_FUNCTION_NOT_FOUND = 4001,
    RESIZE_METHOD_NOT_FOUND,
    EXCLUSIVELOCK_METHOD_NOT_FOUND,
    EXCLUSIVEUNLOCK_METHOD_NOT_FOUND,
    THREADCURRENT_METHOD_NOT_FOUND,
    FINAL_CHECK_FAILED = 9001,
    NATIVE_INIT_SEGMENT_FAULT = 10001
};

// 是否开启调试开关
bool debuggable;

// 当前 API Level
int api_level;

// 当前机型
char brand[64] = {0};
char system_brand[64] = {0};
char device[128] = {0};
char heapsize[16] = {0};
char fingerprint[512] = {0};
char rom_version[128] = {0};

// 函数原型
typedef void *(*stub_method_in_art_)();

typedef void *(*__loader_dlopen_)(const char *filename, int flags, void *caller_addr);

typedef void *(*__loader_dlsym_)(void *__handle, const char *__symbol, void *caller_addr);

typedef void *(*ClampGrowthLimit_)(void *, size_t);

typedef void *(*SetHeapSize_)(void *, size_t);

typedef void *(*SetSize_)(void *, size_t);

typedef void *(*ExclusiveLock_)(void *, void *);

typedef void *(*ExclusiveUnlock_)(void *, void *);

typedef void *(*ThreadCurrent_)();

typedef FILE *(*fopen_)(const char *__path, const char *__mode);

// method
ClampGrowthLimit_ ClampGrowthLimit;
__loader_dlopen_ __loader_dlopen;
__loader_dlsym_ __loader_dlsym;
stub_method_in_art_ stub_method_in_art;
SetHeapSize_ SetHeapSize;
SetSize_ SetSize;
ExclusiveLock_ ExclusiveLock;
ExclusiveUnlock_ ExclusiveUnlock;
ThreadCurrent_ ThreadCurrent;
fopen_ __fopen;

// instance space.
void *art_;
void *runtime_;
void *heap_;
void *region_space_;
size_t *non_free_region_index_limit_;

uint8_t **begin_;
uint8_t **end_;
uint8_t **limit_;
size_t *num_regions_;

// offset
size_t offset_heap_in_runtime;
size_t offset_region_space_in_heap;
size_t offset_region_limit_in_region_space;
size_t offset_num_regions_in_region_space;
size_t offset_space_bitmap_in_region_space;

pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

bool init_ = false;
bool has_exception_handle_ = false;

/**
 * 异常处理
 */
static struct sigaction sig_act_old = {0};
static volatile int i_want_handle_signal_flag = 0;
static sigjmp_buf time_machine;

/**
 * 判断是否是支持的 Android 版本
 */
bool IsAndroidVersionMatch() {
    if (api_level < __ANDROID_API_O__ || api_level > __ANDROID_API_S__) {
        LOGE("support android [8 - 12], but current api is %d", api_level);
        return false;
    }

    return true;
}

/**
 * 计算各个版本中的特定变量偏移
 *
 * offset_heap_in_runtime <<= art::Runtime::RunRootClinits() 第一行找到 class_linker_ 往前推10个指针
 * offset_region_space_in_heap <<= art::gc::Heap::TrimSpaces() 中间连续三个 if 的最后一个 if (涉及到 art::gc::space::RegionSpace::GetBytesAllocated())
 *
 * 1. > Android 8  : offset_region_limit_in_region_space <<= art::Heap::Space::RegionSpace::ClampGrowthLimit(), 找到和右移0x12的参数进行比较的部分
 * 2. <= Android 8 : offset_region_limit_in_region_space = current_region_ -4 <<= art::gc::space::RegionSpace::Clear() 倒数第二个赋值反推
 *
 * offset_num_regions_in_region_space = offset_region_limit_in_region_space - 4 * 3
 */
void DefineOffset() {
    switch (api_level) {
        case __ANDROID_API_O__: // 8
            offset_heap_in_runtime = 0xF4;
            offset_region_space_in_heap = 0x1dc;
            offset_region_limit_in_region_space = 0x7c - 4;

            if (strcasecmp(brand, "samsung") == 0) {
                if (memcmp(device, "SM-C", 4) == 0 ||
                    memcmp(device, "SM-A7", 5) == 0 ||
                    memcmp(device, "SM-A5", 5) == 0 ||
                    memcmp(device, "SM-A3", 5) == 0 ||
                    memcmp(device, "SM-A605", 7) == 0 ||
                    memcmp(device, "SM-J", 4) == 0 ||
                    memcmp(device, "SM-G5", 5) == 0) {
                    // SM-C --
                    // SM-A7 -- SM-A750FN SM-A750F
                    // SM-A5 -- SM-A520W SM-A520F
                    // SM-A3 -- SM-A320F SM-A320FL
                    // SM-A605 -- SM-A605FN
                    // SM-J -- SM-J600G SM-J330FN SM-J810M SM-J810F SM-J600FN
                    // SM-G5 -- SM-G570F
                    offset_region_space_in_heap = 0x1d4;
                    offset_region_limit_in_region_space = 0x74 - 4;
                } else if (memcmp(device, "SM-N", 4) == 0 ||
                    memcmp(device, "SM-G9", 5) == 0 ||
                    memcmp(device, "SM-A600", 7) == 0) {
                    // SM-N -- SM-N950F SM-N950N SM-N950U
                    // SM-G9 -- SM-G965F SM-G965U SM-G9650 SM-G9600 SM-G950F SM-G955U SM-G955F SM-G930F SM-G935F
                    // SM-A600 -- SM-A600FN
                    offset_region_space_in_heap = 0x1dc;
                    offset_region_limit_in_region_space = 0x7c - 4;
                } else {
                    offset_region_space_in_heap = 0x1F4;
                    offset_region_limit_in_region_space = 0x94 - 4;
                }
            }

            offset_num_regions_in_region_space = offset_region_limit_in_region_space - 4 * 3;
            offset_space_bitmap_in_region_space = offset_region_limit_in_region_space + 52;

            break;
        case __ANDROID_API_O_MR1__: // 8.1
            offset_heap_in_runtime = 0xF4;
            offset_region_space_in_heap = 0x1e4;
            offset_region_limit_in_region_space = 0x7c - 4;

            if (strcasecmp(brand, "samsung") == 0) {
                if (memcmp(device, "SM-G6", 5) == 0) {
                    // SM-G6 -- SM-G6200
                    offset_region_space_in_heap = 0x1e4;
                } else if (memcmp(device, "SM-J", 4) == 0 ||
                    memcmp(device, "SM-G8", 5) == 0 ||
                    memcmp(device, "SM-N", 4) == 0) {
                    // SM-J -- SM-J530L SM-J710F SM-J730FM
                    // SM-G8 -- SM-G8870
                    // SM-N -- SM-N960F SM-N960U
                    offset_region_space_in_heap = 0x1dc;
                    offset_region_limit_in_region_space = 0x74 - 4;
                } else {
                    offset_region_space_in_heap = 0x1dc;
                }
            }

            offset_num_regions_in_region_space = offset_region_limit_in_region_space - 4 * 3;
            offset_space_bitmap_in_region_space = offset_region_limit_in_region_space + 52;

            break;
        case __ANDROID_API_P__: // 9
            offset_heap_in_runtime = 0x128;
            offset_region_space_in_heap = 0x1c4;
            offset_region_limit_in_region_space = 0x78;
            offset_num_regions_in_region_space = offset_region_limit_in_region_space - 4 * 5;

            break;
        default:
        case __ANDROID_API_Q__:  // 10
            offset_heap_in_runtime = 0x118 - 4 * 10;
            offset_region_space_in_heap = 0x1ec;
            offset_region_limit_in_region_space = 0x94;
            offset_num_regions_in_region_space = offset_region_limit_in_region_space - 4 * 5;

            break;
        case __ANDROID_API_R__: // 11
            offset_heap_in_runtime = 0x114 - 4 * 10;
            offset_region_space_in_heap = 0x208;
            offset_region_limit_in_region_space = 0x160;

            if (strcasecmp(brand, "meizu") == 0) {
                offset_heap_in_runtime = 0xF4;
            }

            if (strcasecmp(brand, "samsung") == 0) {
                offset_region_space_in_heap = 0x210;
            }

            // Android 11 多了一个 map
            offset_num_regions_in_region_space = offset_region_limit_in_region_space - 4 * 5 - 12;

            break;
        case __ANDROID_API_S__: // 12
            offset_heap_in_runtime = 0x120 - 4 * 10;
            offset_region_space_in_heap = 0x210;
            offset_region_limit_in_region_space = 0x16c;

            if (memcmp(device, "SM-S901N", 8) == 0) {
                offset_heap_in_runtime = 0x150 - 4 * 10;
                offset_region_space_in_heap = 0x218;
            }

            // Android 12 在 Android 11 的基础上多了 1 个 uint64_t 1 个 size_t
            offset_num_regions_in_region_space = offset_region_limit_in_region_space - 4 * 5 - 12 - 8 - 4;

            break;
    }
}

/**
 * 获取不同版本的 libart.so 的路径，其实最好通过读 /proc/maps 来确定
 * 不过这个没听说有厂商修改的，直接写死也不是不行
 */
const char *GetArtPath() {
    const char *lib_art_path_;

    switch (api_level) {
        default:
        case __ANDROID_API_O__:
        case __ANDROID_API_O_MR1__:
        case __ANDROID_API_P__:
            lib_art_path_ = "/system/lib/libart.so";
            break;
        case __ANDROID_API_Q__:
            lib_art_path_ = "/apex/com.android.runtime/lib/libart.so";
            break;
        case __ANDROID_API_R__:
        case __ANDROID_API_S__:
            lib_art_path_ = "/apex/com.android.art/lib/libart.so";
            break;
    }

    return lib_art_path_;
}

/**
 * 初始化环境信息，android 版本和机型等等，用于后期适配
 */
void InitEnv() {
    // 厂商
    __system_property_get("ro.product.brand", brand);
    // 厂商品牌
    __system_property_get("ro.product.system.brand", system_brand);
    // 型号
    __system_property_get("ro.product.model", device);
    // 默认堆大小
    __system_property_get("dalvik.vm.heapsize", heapsize);
    // ROM 信息
    __system_property_get("ro.build.fingerprint", fingerprint);

    // 拼接自定义 ROM Version 的配置名
    char custom_rom_config_name[128] = {0};
    strcat(custom_rom_config_name, "ro.build.version.");
    strcat(custom_rom_config_name, system_brand);
    strcat(custom_rom_config_name, "rom");

    // ROM Version
    __system_property_get(custom_rom_config_name, rom_version);

    api_level = android_get_device_api_level();
}

/**
 * 自定义异常处理函数
 */
static void CustomSignalHandler(int signum, siginfo_t* siginfo, void* context) {
    if (i_want_handle_signal_flag) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "found exception signal %d", signum);

        // reset flag.
        i_want_handle_signal_flag = 0;

        siglongjmp(time_machine, 1);
    } else {
        // use raw log method, LOGE not thread safe.
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "found exception signal %d, but not my business.", signum);

        // sigaction(sig, &sig_act_old, NULL);
        if (sig_act_old.sa_flags & SA_SIGINFO) {
            sig_act_old.sa_sigaction(signum, siginfo, context);
        } else {
            if (SIG_DFL == sig_act_old.sa_handler) {
                // If the previous handler was the default handler, cause a core dump.
                signal(signum, SIG_DFL);
                raise(signum);
            } else if (SIG_IGN == sig_act_old.sa_handler) {
                return;
            } else {
                sig_act_old.sa_handler(signum);
            }
        }
    }
}

/**
 * 覆盖特定信号的处理函数
 */
static int HandleSignal(int sig) {
    struct sigaction act = {0};

    if (0 != sigemptyset(&act.sa_mask))
        return (0 == errno ? SIGNAL_HANDLER_REG_ERROR : errno);

    act.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
    act.sa_sigaction = CustomSignalHandler;

    if (0 != sigaction(sig, &act, &sig_act_old))
        return (0 == errno ? SIGNAL_HANDLER_REG_ERROR : errno);

    return 0;
}

inline size_t NonGrowthLimitCapacity() {
    return *limit_ - *begin_;
}

inline void CleanLogBuffer() {
    dump_cursor = 0;
    memset(dump_logs, 0, 128);
}

/**
 * 判断是不是华为有特定 bug 的机型
 */
bool IsHuaweiBugDevice() {
    // Android 10
    if (api_level == __ANDROID_API_Q__) {
        // 华为和荣耀
        if (strcmp(brand, "HUAWEI") == 0 || strcmp(brand, "HONOR") == 0) {
            // 特定的 TOP 10
            if (strcmp(device, "COL-AL10" /* 荣耀 10 */) == 0 ||
                strcmp(device, "BKL-AL20" /* 荣耀 V10 */) == 0 ||
                strcmp(device, "BKL-AL00") == 0 ||
                strcmp(device, "STK-AL00" /* 华为 畅享10 Plus */) == 0 ||
                strcmp(device, "TAS-AL00") == 0 ||
                strcmp(device, "TAS-AN00" /* 华为 Mate30 */) == 0 ||
                strcmp(device, "COL-TL10") == 0 ||
                strcmp(device, "BKL-TL10") == 0 ||
                strcmp(device, "VCE-AL00") == 0) {
                return true;
            }
        }
    }
    return false;
}

#endif //ABORT_KILLER_PATRONS_CORE_H
