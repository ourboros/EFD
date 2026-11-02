# R8 精簡規則 (正式版 isMinifyEnabled = true)

# MediaPipe Tasks：原生程式以 JNI 呼叫 Java 類別並解析 protobuf 設定，類別與成員名稱不可被改名或移除
-keep class com.google.mediapipe.** { *; }
-keep class com.google.protobuf.** { *; }
-dontwarn com.google.mediapipe.**
-dontwarn com.google.protobuf.**

# MediaPipe 使用 Flogger 記錄：Flogger 以類別名稱比對呼叫堆疊找出呼叫端，被改名會在 Graph 靜態初始化時拋出
# IllegalStateException("no caller found on the stack")，導致 FaceLandmarker 無法建立
-keep class com.google.common.flogger.** { *; }
-dontwarn com.google.common.flogger.**

# Google 使用統計 (datatransport)：上傳後端已在 AndroidManifest.xml 移除，事件不會送出
-dontwarn com.google.android.datatransport.**

# MediaPipe 相依的編譯期註解
-dontwarn javax.annotation.**
-dontwarn com.google.auto.value.**
-dontwarn com.google.errorprone.annotations.**

# 本專案 JNI 介面：native 方法名稱需與 C++ 符號 Java_com_efd_..._EfdNativeBridge_* 一致
-keep class com.efd.eyefatiguedetection.EfdNativeBridge { *; }
