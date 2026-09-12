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
        minSdk = 24          // GLES2 + OpenSL ES baseline; arm64 needs 21+
        targetSdk = 35
        versionCode = 1
        versionName = "1.0-dx-port"

        ndk {
            abiFilters += "arm64-v8a"
        }

        externalNativeBuild {
            cmake {
                arguments += "-DANDROID_STL=c++_shared"
            }
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
