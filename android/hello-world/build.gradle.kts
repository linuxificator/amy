plugins {
    id("com.android.application")
}

android {
    namespace = "org.amy.hello"
    compileSdk = 36
    ndkVersion = "27.0.12077973"

    defaultConfig {
        applicationId = "org.amy.hello"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"

        ndk {
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
    }
}

dependencies {
    implementation(project(":amy-service"))
}
