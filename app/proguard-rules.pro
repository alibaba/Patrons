# Add project specific ProGuard rules here.
# You can control the set of applied configuration files using the
# proguardFiles setting in build.gradle.
#
# For more details, see
#   http://developer.android.com/guide/developing/tools/proguard.html

# If your project uses WebView with JS, uncomment the following
# and specify the fully qualified class name to the JavaScript interface
# class:
#-keepclassmembers class fqcn.of.javascript.interface.for.webview {
#   public *;
#}

# Uncomment this to preserve the line number information for
# debugging stack traces.
#-keepattributes SourceFile,LineNumberTable

# If you keep the line number information, uncomment this to
# hide the original source file name.
#-renamesourcefileattribute SourceFile

-optimizationpasses 1
-optimizations code/removal/simple,code/removal/advanced,code/removal/variable,code/removal/exception,code/simplification/branch,code/simplification/field,code/simplification/cast,code/simplification/arithmetic,code/simplification/variable
#-optimizations code/removal/simple,code/removal/advanced,method/removal/parameter,method/inlining/short,method/inlining/tailrecursion
# ignoring warnings, that gets the scala app to work, but is a bit dangerous...
-ignorewarnings
-target 1.8
# -dontobfuscate

-keep public class * extends android.app.Activity
-keep public class * extends android.app.Application
-keep public class * extends android.app.Service
-keep public class * extends android.content.BroadcastReceiver
-keep public class * extends android.content.ContentProvider
-keep public class * extends android.app.backup.BackupAgentHelper
-keep public class * extends android.preference.Preference

-keepclassmembernames class me.zhilong.tools.abortkiller.demo.MainActivity {
    private *;
}