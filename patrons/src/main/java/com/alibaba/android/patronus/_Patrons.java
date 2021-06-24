package com.alibaba.android.patronus;

import android.content.Context;
import android.os.Build;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStreamReader;
import java.util.Timer;
import java.util.TimerTask;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.regex.Pattern;

/**
 * Patrons real
 *
 * @author zhilong [Contact me.](mailto:zhilong.lzl@alibaba-inc.com)
 * @version 1.0
 * @since 4/16/21 10:23 AM
 */
public class _Patrons {
    private static final long KB = 1024;
    private static final long MB = 1024 * KB;
    private static final long GB = 1024 * MB;

    private static final int VERSION_CODES_R = 30;

    // 错误码和 native 层的状态不能冲突
    private static final int ERROR_READ_VSS_FAILED = 1001;
    private static final int ANDROID_VERSION_NOT_SUPPORT = 2001;
    private static final int HEAP_SIZE_IS_NOT_BIG_ENOUGH = 2002;
    private static final int LOWER_LIMIT_IS_TOO_SMALL = 2003;

    private static final long S = 1000;
    private static final int MAX_CHECK_OF_STRICT_MODE = 5;

    public static final String TAG = "Patrons";

    private static final String numRegEx = "[^0-9]";
    private static final Pattern numPattern = Pattern.compile(numRegEx);

    private static final float VSS_MAX_IN_V7A = 4f * GB;

    private static Patrons.PatronsConfig config = new Patrons.PatronsConfig();

    private static Timer autoCheckVssTimer = null;

    private static boolean NATIVE_LIB_LOADED = false;

    // 当前 Region Space 大小，单位 MB
    private static long currentRegionSpaces;

    // 频繁检查模式
    private static final AtomicInteger strictCount = new AtomicInteger(0);

    private _Patrons() {
    }

    protected static synchronized int init(Context context, Patrons.PatronsConfig config) {
        if (config != null) {
            _Patrons.config = config;
        }

        Log.i(TAG, "patrons start init, config = " + _Patrons.config.toString());

        // Java Init
        int code = __init();

        if (_Patrons.config.recordInitResult && null != context) {
            // Record Init Result
            asyncWriteInitResultToFile(context, code);
        }

        return code;
    }

    /**
     * Real java init
     */
    protected static synchronized int __init() {
        if (!isSupport()) {
            Log.e(TAG, "patrons init failed, android version or abi not match !");
            return ANDROID_VERSION_NOT_SUPPORT;
        }

        // native init.
        int resultCode = __init(true, _Patrons.config.debuggable, _Patrons.config.fixHuaweiBinderAbort);

        if (resultCode != 0) {
            Log.e(TAG, "patrons native init failed !");
            return resultCode;
        }

        // 拿到当前的 Region Space 大小，后续基于这个作为上限
        currentRegionSpaces = getCurrentRegionSpaceSize() / MB;

        // 简单的校验一下堆大小
        if (currentRegionSpaces <= 0 || currentRegionSpaces > 1024) {
            return HEAP_SIZE_IS_NOT_BIG_ENOUGH;
        }

        if (currentRegionSpaces < _Patrons.config.lowerLimit) {
            return LOWER_LIMIT_IS_TOO_SMALL;
        }

        if (_Patrons.config.auto) {
            // 开启自动模式，需要检查一下是否可以读取 vss，否则无法完成循环检查
            if (readVssSize() < 0) {
                Log.e(TAG, "patrons read vss failed !");
                return ERROR_READ_VSS_FAILED;
            }

            // 注册定时器每隔 30s 循环检查 vss
            toForeground();
        }

        Log.i(TAG, "patrons init finish, vss = " + readVssSize() / MB + " mb, heap = " + currentRegionSpaces + " mb");

        return 0;
    }

    /**
     * 读取当前进程的内存信息
     * File : /proc/self/status
     */
    static long readVssSize() {
        long vssSize = -1L;

        try {
            FileInputStream statusFileStream = new FileInputStream("/proc/" + android.os.Process.myPid() + "/status");
            BufferedReader bufferedReader = new BufferedReader(new InputStreamReader(statusFileStream));

            String content;
            while ((content = bufferedReader.readLine()) != null) {
                content = content.toLowerCase();

                if (content.contains("vmsize")) {
                    // current vss size
                    vssSize = Integer.parseInt(numPattern.matcher(content).replaceAll("").trim()) * KB;
                    break;
                }
            }
            statusFileStream.close();
            bufferedReader.close();
        } catch (Exception ex) {
            Log.e(TAG, "read current status failed.");
        }

        return vssSize;
    }

    /**
     * App 进入后台，停止检查
     */
    static void inBackground() {
        if (_Patrons.config.auto && autoCheckVssTimer != null) {
            autoCheckVssTimer.cancel();
            autoCheckVssTimer = null;
        }
    }

    /**
     * 回到前台的时候，如果有必要的话，启动循环检查，会清除掉频繁检查模式
     */
    static void toForeground() {
        strictCount.set(0);
        _start(_Patrons.config.periodOfCheck);
    }

    private static void _start(int period) {
        if (_Patrons.config.auto) {
            // 先清理
            if (autoCheckVssTimer != null) {
                autoCheckVssTimer.cancel();
                autoCheckVssTimer = null;
            }

            // 再启动
            autoCheckVssTimer = new Timer();
            autoCheckVssTimer.schedule(new AutoCheckerTask(), period * S, period * S);
        }
    }

    /**
     * 初始化 Patrons native 逻辑，默认异步启动
     *
     * @param sync              同步初始化
     * @param debuggable        调试模式
     * @param isFixHuaweiBinder 修复华为的 libbinder bug
     */
    private static native int __init(boolean sync, boolean debuggable, boolean isFixHuaweiBinder);

    /**
     * 收缩 Region Space 大小
     *
     * @param newSize 收缩后的大小 (MB)
     */
    static native boolean shrinkRegionSpace(int newSize);

    /**
     * 获取当前 Region Space 大小 (B)
     */
    static native long getCurrentRegionSpaceSize();

    /**
     * Dump native 层的日志
     */
    static native String dumpLogs();

    private static void stop() {
        inBackground();
        _Patrons.config.auto = false;
    }

    static String dumpNativeLogs() {
        if (NATIVE_LIB_LOADED) {
            return dumpLogs();
        }

        return "can not dump logs without native libs";
    }

    public static class AutoCheckerTask extends TimerTask {
        @Override
        public void run() {
            if (strictCount.get() != 0 && strictCount.addAndGet(1) > MAX_CHECK_OF_STRICT_MODE) {
                strictCount.set(0);

                Log.i(TAG, "exit strict mode after check " + MAX_CHECK_OF_STRICT_MODE + " times");
                _start(_Patrons.config.periodOfCheck);
            }

            long beforeVmSize = readVssSize();
            float currentPeriod = beforeVmSize / VSS_MAX_IN_V7A;

            if ((currentRegionSpaces - config.shrinkStep) < config.lowerLimit) {
                Log.e(TAG, "vss has no space to resize, stop watching. current space = " + currentRegionSpaces);
                stop();
                return;
            }

            if (currentPeriod > config.periodOfShrink) {
                // 达到了缩容阈值，尝试缩小 region space 释放 vss
                Log.i(TAG, "vss has over the period, current vss = " + beforeVmSize / MB + "mb, period = " + currentPeriod);
                boolean resizeResult = shrinkRegionSpace((int) (currentRegionSpaces = currentRegionSpaces - config.shrinkStep));

                if (!resizeResult) {
                    Log.e(TAG, "vss resize failed, stop watching.");
                    stop();
                    return;
                }

                beforeVmSize = readVssSize();

                Log.i(TAG, "resize success" + ", step = " + config.shrinkStep + "mb, current vss = " + beforeVmSize / MB + "mb");

                // 经历了一次缩容，进行频繁检查周期，防止下次检查之前就炸了
                Log.i(TAG, "enter strict mode after resize");

                strictCount.set(1);

                _start(_Patrons.config.periodOfCheck / 2);
            } else if (getCurrentRegionSpaceSize() / MB < _Patrons.config.lowerLimit) {
                Log.e(TAG, "current heap size (" + getCurrentRegionSpaceSize() / MB + ") less than lower limit (" + _Patrons.config.lowerLimit + ") stop watching.");
                stop();
            } else {
                if (_Patrons.config.debuggable) {
                    Log.i(TAG, "[" + strictCount.get() + "] every thing is OK, vss = " + beforeVmSize / MB + " mb, current period = " + currentPeriod + ", heap = " + getCurrentRegionSpaceSize() / MB + " mb");
                }
            }
        }
    }

    /**
     * 是否需要 Patrons
     * 支持 Android 8 ~ 11 的 32 位应用
     */
    private static boolean isSupport() {
        return Build.VERSION.SDK_INT >= Build.VERSION_CODES.O &&
                Build.VERSION.SDK_INT <= VERSION_CODES_R &&
                !android.os.Process.is64Bit();
    }

    /**
     * 异步写入初始化结果
     */
    private static void asyncWriteInitResultToFile(final Context context, final int code) {
        new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    // 写入初始化内容到 data 目录，方便接入方判断是否接入成功
                    String patronsFilePath = context.getDir("patrons", Context.MODE_PRIVATE).getAbsolutePath() + File.separator;

                    stringToFile(String.valueOf(code), patronsFilePath + "code.txt");

                    if (code != 0) {
                        stringToFile(dumpNativeLogs(), patronsFilePath + "msg.txt");
                    }
                } catch (Exception ex) {
                    Log.e(TAG, "record init result failed, code = " + code, ex);
                }
            }
        }).start();
    }

    private static void stringToFile(String content, String targetPath) {
        try (FileOutputStream fileOutputStream = new FileOutputStream(new File(targetPath))) {
            fileOutputStream.write((content + "\n\n").getBytes());
        } catch (Exception ex) {
            Log.e(TAG, "write content to file: " + targetPath + " failed.", ex);
        }
    }

    static {
        if (isSupport()) {
            System.loadLibrary("patrons");
            NATIVE_LIB_LOADED = true;
        }
    }
}
