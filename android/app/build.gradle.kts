plugins {
    id("com.android.application")
}

android {
    namespace = "net.dgate.ewdx"
    compileSdk = 35
    // NDK present on build host: C:\Android\android-sdk\ndk\27.3.13750724
    ndkVersion = "27.3.13750724"

    defaultConfig {
        applicationId = "net.dgate.ewdx"
        // v27.8: 21 is the floor for NDK r27; lowering from 24 adds
        // Android 5.0-6.0 handsets, which is where most surviving
        // 32-bit ARM devices actually sit.
        minSdk = 21
        targetSdk = 35
        versionCode = 1
        versionName = "1.0-dx-port"

        ndk {
            // v27.8: 64- and 32-bit ARM in one universal APK. armeabi-v7a
            // covers older 32-bit handsets; HSP64 is gated per ABI in
            // src/main/cpp/CMakeLists.txt (it sizes HSP pointers).
            abiFilters += listOf("arm64-v8a", "armeabi-v7a")
        }

        externalNativeBuild {
            cmake {
                arguments += "-DANDROID_STL=c++_shared"
            }
        }
    }

    signingConfigs {
        // v27.8: sign with APK Signature Scheme v1 + v2 + v3 explicitly.
        // v1 (JAR) is what pre-Android-7 devices verify; v2/v3 are what
        // 7.0+ and 9.0+ prefer. Shipping all three means no installer is
        // left without a scheme it understands.
        getByName("debug") {
            enableV1Signing = true
            enableV2Signing = true
            enableV3Signing = true
            enableV4Signing = false   // needs a side-car .idsig; not for sideload
        }
    }

    buildTypes {
        getByName("debug") {
            isDebuggable = true
            isJniDebuggable = true
        }
        getByName("release") {
            isMinifyEnabled = false
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1+"
        }
    }

    sourceSets {
        // Step 3 wiring: the extracted game tree is bundled here at assemble time.
        //   <project>/android/app/src/main/assets/data/{map,mold,mot,music,pic,se}/
        //   <project>/android/app/src/main/assets/save.dat
        // Runtime copies assets -> filesDir on first launch so the script's
        // relative paths (data\pic\..., save.dat) resolve unchanged.
        getByName("main") {
            assets.srcDirs("src/main/assets")
            // SDL2's Java activity (SDLActivity + audio/input/HID managers).
            // The manifest's launcher activity is org.libsdl.app.SDLActivity;
            // without these sources classes.dex has no activity and the app
            // dies instantly with ClassNotFoundException (seen in bugreport).
            // SDL2 is a sibling checkout (see ewdx-port/refs.md).
            java.srcDirs("../../../SDL2/android-project/app/src/main/java")
        }
    }
}
