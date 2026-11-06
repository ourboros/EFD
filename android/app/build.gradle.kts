import java.text.SimpleDateFormat
import java.util.Date
import java.util.Properties

plugins {
    id("com.android.application")
}

// 正式簽章：金鑰與密碼放在 android/keystore.properties (不進版本庫)。
// 不再發佈 debug 簽章的 APK：debug 金鑰人人可用，會被 Google Play 安全防護視為可疑。
val keystoreProperties = Properties().apply {
    val file = rootProject.file("keystore.properties")
    if (file.exists()) file.inputStream().use { load(it) }
}

// 版本識別：每次建置的 versionCode 取建置時間 (yyMMddHH，只會變大，同簽章可直接覆蓋安裝)，
// 並把 git commit 寫進 BuildConfig，錯誤回報才看得出手機上裝的是哪一版
val buildTime = Date()
val buildVersionCode = SimpleDateFormat("yyMMddHH").format(buildTime).toInt()
// 顯示用版本名稱：系統版本 2.3.1 加上建置時間 (精確到分鐘) (versionCode 為 int，只能到小時)，同一小時內的多次建置也分得出來
val buildVersionName = "2.3.1." + SimpleDateFormat("yyMMdd.HHmm").format(buildTime)
val gitCommit: String = try {
    val process = ProcessBuilder("git", "rev-parse", "--short", "HEAD")
        .directory(rootProject.projectDir)
        .redirectErrorStream(true)
        .start()
    val out = process.inputStream.bufferedReader().readText().trim()
    if (process.waitFor() == 0 && out.isNotEmpty()) out else "unknown"
} catch (e: Exception) {
    "unknown"
}

android {
    namespace = "com.efd.eyefatiguedetection"
    compileSdk = 35
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "com.efd.eyefatiguedetection"
        minSdk = 24
        targetSdk = 35
        versionCode = buildVersionCode
        versionName = buildVersionName
        buildConfigField("String", "GIT_COMMIT", "\"$gitCommit\"")

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++20"
                arguments += listOf("-DANDROID_STL=c++_static")
                abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
            }
        }
    }

    // 依 CPU 分包：每支手機只安裝自己 CPU 的原生程式庫 (MediaPipe 每種 CPU 約 7~12 MB)
    // 受試者主要發 app-arm64-v8a-*.apk；32 位元舊手機用 armeabi-v7a；x86_64 只給電腦上的模擬器
    splits {
        abi {
            isEnable = true
            reset()
            include("arm64-v8a", "armeabi-v7a", "x86_64")
            isUniversalApk = false
        }
    }

    signingConfigs {
        if (keystoreProperties.containsKey("storeFile")) {
            create("release") {
                storeFile = rootProject.file(keystoreProperties.getProperty("storeFile"))
                storePassword = keystoreProperties.getProperty("storePassword")
                keyAlias = keystoreProperties.getProperty("keyAlias")
                keyPassword = keystoreProperties.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        release {
            signingConfig = signingConfigs.findByName("release")
            // R8 精簡未使用的 Java 程式碼 (MediaPipe 的 Guava、Protobuf 等)；保留規則見 proguard-rules.pro
            isMinifyEnabled = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
        debug {
            isDebuggable = true
        }
    }

    buildFeatures {
        buildConfig = true
    }

    // MediaPipe 以記憶體映射讀取 assets 內的模型，壓縮後無法載入
    androidResources {
        noCompress += listOf("task", "tflite")
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
}

dependencies {
    constraints {
        implementation("org.jetbrains.kotlin:kotlin-stdlib-jdk7:1.8.22") {
            because("kotlin-stdlib-jdk7 is now part of kotlin-stdlib")
        }
        implementation("org.jetbrains.kotlin:kotlin-stdlib-jdk8:1.8.22") {
            because("kotlin-stdlib-jdk8 is now part of kotlin-stdlib")
        }
    }
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("androidx.core:core:1.13.1")
    implementation("androidx.camera:camera-core:1.3.4")
    implementation("androidx.camera:camera-camera2:1.3.4")
    implementation("androidx.camera:camera-lifecycle:1.3.4")
    implementation("androidx.camera:camera-view:1.3.4")
    implementation("androidx.lifecycle:lifecycle-service:2.8.5")
    implementation("androidx.lifecycle:lifecycle-common-java8:2.8.5")
    implementation("androidx.cardview:cardview:1.0.0")
    // 臉部 478 點特徵點與眨眼 blendshape (模型檔：src/main/assets/face_landmarker.task)
    // MediaPipe 內建的 Google 使用統計上傳後端已在 AndroidManifest.xml 移除 (統計事件不會送出)；
    // 函式庫本身無法排除：排除後 FaceLandmarker 建立時 NoClassDefFoundError (TransportRuntime)
    implementation("com.google.mediapipe:tasks-vision:0.10.35")
}

