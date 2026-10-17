plugins {
    id("com.android.application")
}

android {
    namespace = "com.daniel.voxelcraft"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.daniel.voxelcraft"
        minSdk = 24        // Vulkan 1.0 está disponível a partir do Android 7.0
        targetSdk = 35
        versionCode = 1
        versionName = "0.1"
        ndk {
            abiFilters += listOf("arm64-v8a")
        }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_static")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../CMakeLists.txt")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            // Assinado com a chave de debug para poder instalar direto no aparelho.
            signingConfig = signingConfigs.getByName("debug")
        }
    }
}
