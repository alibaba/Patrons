# Patrons

[![Download](https://maven-badges.herokuapp.com/maven-central/com.alibaba/patrons/badge.svg)](https://maven-badges.herokuapp.com/maven-central/com.alibaba/patrons)

`🎉 A framework for improving android 32bit app stability. (Alleviate crashes caused by insufficient virtual memory)`

一行代码解决 Android 32位应用因虚拟内存不足导致的 libc:abort(signal 6)

## 一、背景
目前国内的 Android App 大多数还是32位架构，仅提供了 arm-v7a 的动态链接库，市面上大多数手机都是64位的 CPU，App 通常都运行在兼容模式下，可以使用完整的 4GB 虚拟内存，但是国内应用一般都是集万千功能于一身，随着业务越来越复杂(内置webview、小程序、高清大图、短视频等等)，以及部分内存泄漏，4GB 的内存越来越不够用了。

从去年(2020)开始，各大头部应用的 Native Crash 开始暴增，通常 Top1 都是 `libc:abort`，通过上报的 maps 可见，虚拟内存地址空间大部分接近了 4GB，console logs 中也有大量的 `GL Errors: Out of memory(12)`。

针对此问题，一般首先能想到的就是排查内存泄漏问题，但往往收效甚微，多半是因为随着业务的发展，确实是需要这么多虚拟内存。诚然通过升级64位架构可以把地址空间上限扩充到512GB，但是因为各种原因(包大小、维护成本等等)，目前大部分应用尚未完成升级，所以在这里提供一种新的思路。

## 二、原理
通过一系列技术手段实现运行期间动态调整`Region Space`预分配的地址空间，释放出最多`900MB`(根据实际情况调整参数)虚拟内存给到 libc:malloc，增加了接近30%的地址上限，大幅度给应用续命。

详细介绍：[阿里开源 Patrons：大型 32 位 Android 应用稳定性提升 50% 的“黑科技”](https://www.infoq.cn/article/bvbf3iwjztvem4szamvw)

## 三、使用方式
编译`patrons`模块 or 使用以下中心仓库的坐标，主工程依赖该模块产物，在合适的时机进行初始化：

```groovy
   repositories {
        mavenCentral()
   }
   dependencies {
         implementation 'com.alibaba:patrons:1.0.6.5'
   }
```

```java
    com.alibaba.android.patronus.Patrons.init(context, null);
```

##### [→ 测试 Demo 下载](https://github.com/alibaba/Patrons/blob/develop/demo/patrons-demo-1.0.6.2.apk)

## 四、Q & A

1. SDK 本身会带来多少接入成本(包大小、稳定性)：包大小增加20k左右，可以忽略不计；关键逻辑中会有多层保护，不会引发新的崩溃。

2. SDK 兼容性怎么样：在 Android 8、8.1、9、10、11 共 5 个主流版本生效，覆盖率接近 99.9%。在未兼容机型中不会生效，亦不会产生新的崩溃。

3. 使用后就能根治 Abort 么：肯定不能，因为 Abort 的成因很多，虽然32位应用多半是因为虚拟内存不足，但是也可能存在其他问题，适配性还是要具体情况具体分析。