package com.alibaba.android.patronus;

import android.content.Context;

/**
 * Patrons Facade
 *
 * @author zhilong [Contact me.](mailto:zhilong.lzl@alibaba-inc.com)
 * @version 1.0
 * @since 4/15/21 6:15 PM
 */
public final class Patrons {
    private volatile static boolean hasInit = false;

    private Patrons() {
    }

    /**
     * Patrons 入口
     *
     * @param context android context 用来获取文件目录用的，可以不传，就不会本地记录初始化结果了
     * @param config  自定义配置 (如无指导直接传null)
     * @return code, 错误码，无异常则返回 0
     */
    public static int init(Context context, PatronsConfig config) {
        if (!hasInit) {
            int resultCode = _Patrons.init(context, config);
            hasInit = resultCode == 0;

            return resultCode;
        }

        return 0;
    }

    public static void inBackground() {
        if (hasInit) {
            _Patrons.inBackground();
        }
    }

    public static void toForeground() {
        if (hasInit) {
            _Patrons.toForeground();
        }
    }

    public static long readVssSize() {
        return _Patrons.readVssSize();
    }

    public static long getRegionSpaceSize() {
        if (!hasInit) {
            return -1;
        }

        return _Patrons.getCurrentRegionSpaceSize();
    }

    /**
     * 提取 native 日志，未初始化成功也可以拿到
     */
    public static String dumpNativeLogs() {
        return _Patrons.dumpNativeLogs(true);
    }

    /**
     * 收缩 Region Space 大小，用之前最好想清楚你在做什么
     */
    public static boolean shrinkRegionSpace(int newSize) {
        if (hasInit) {
            return _Patrons.shrinkRegionSpace(newSize);
        }

        return false;
    }

    /**
     * Patrons 配置，没有明确指导不能瞎调
     */
    public static class PatronsConfig {
        /**
         * 调试模式
         */
        public boolean debuggable = false;
        /**
         * 自动保护
         */
        public boolean auto = true;
        /**
         * 收缩阈值
         */
        public float periodOfShrink = 0.76f;
        /**
         * 收缩步长
         */
        public int shrinkStep = 125;
        /**
         * 检查周期，触发缩容之后，会进入 10 个周期的频繁检查(periodOfCheck / 2)，如果没有再次缩容，则恢复设置的检查间隔
         */
        public int periodOfCheck = 30;
        /**
         * 最低内存限制
         */
        public int lowerLimit = 384;
        /**
         * 是否顺便修复华为的 libbinder bug
         * art::ThreadDexHotfixMonitor(void*)+252
         */
        public boolean fixHuaweiBinderAbort = false;
        /**
         * 是否记录初始化结果
         */
        public boolean recordInitResult = true;

        @Override
        public String toString() {
            return "{ " +
                    "debuggable=" + debuggable +
                    ", auto=" + auto +
                    ", periodOfShrink=" + periodOfShrink +
                    ", shrinkStep=" + shrinkStep +
                    ", periodOfCheck=" + periodOfCheck +
                    ", lowerLimit=" + lowerLimit +
                    ", recordInitResult=" + recordInitResult +
                    " }";
        }
    }
}
