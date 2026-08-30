plugins {
    id("com.android.library")
}

android {
    namespace = "org.amy.audio"
    compileSdk = 36
    ndkVersion = "27.0.12077973"

    defaultConfig {
        minSdk = 26

        ndk {
            abiFilters += listOf("arm64-v8a")
        }

        externalNativeBuild {
            cmake {
                arguments += "-DANDROID_STL=c++_shared"
                cppFlags += "-std=c++17"
            }
        }
    }

    buildFeatures {
        prefab = true
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    packaging {
        jniLibs {
            useLegacyPackaging = false
        }
    }
}

dependencies {
    implementation("com.google.oboe:oboe:1.10.0")
}
