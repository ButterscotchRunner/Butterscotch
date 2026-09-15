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

# JNI boundary: Keep ButterscotchNative
-keep class net.perfectdreams.butterscotch.android.ButterscotchNative { *; }

# kotlinx-serialization: keep the generated $serializer classes and the serializer() accessors
-keepattributes RuntimeVisibleAnnotations,AnnotationDefault
-keepclassmembers class net.perfectdreams.butterscotch.android.** {
    *** Companion;
}
-keepclasseswithmembers class net.perfectdreams.butterscotch.android.** {
    kotlinx.serialization.KSerializer serializer(...);
}
-keep,includedescriptorclasses class net.perfectdreams.butterscotch.android.**$$serializer { *; }

# Required for AdMob for SOME REASON
-keepclassmembers class * extends androidx.room.RoomDatabase {
    <init>();
}