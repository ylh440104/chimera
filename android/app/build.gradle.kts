plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "dev.chimera.app"
    compileSdk = 34
    ndkVersion = "26.1.10909125"

    defaultConfig {
        applicationId = "dev.chimera.app"
        minSdk = 26
        targetSdk = 34
        versionCode = providers.environmentVariable("GITHUB_RUN_NUMBER").orNull?.toInt() ?: 1
        versionName = "0.1"
        ndk {
            abiFilters += "arm64-v8a"
        }
        externalNativeBuild {
            cmake {
                arguments += "-DANDROID_STL=none"
                cFlags += listOf("-std=gnu11", "-O2", "-Wall")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/jni/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    testOptions.unitTests.isIncludeAndroidResources = true

    kotlinOptions {
        jvmTarget = "17"
    }

    signingConfigs {
        create("chimera") {
            storeFile = file(providers.environmentVariable("CHIMERA_KEYSTORE_PATH").get())
            storePassword = providers.environmentVariable("CHIMERA_KEYSTORE_PASSWORD").get()
            keyAlias = "chimera"
            keyPassword = providers.environmentVariable("CHIMERA_KEYSTORE_PASSWORD").get()
            storeType = "PKCS12"
        }
    }

    buildTypes {
        debug {
            signingConfig = signingConfigs.getByName("chimera")
        }
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("chimera")
        }
    }
}

dependencies {
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("androidx.activity:activity-ktx:1.9.2")
    implementation("com.googlecode.plist:dd-plist:1.30")
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.robolectric:robolectric:4.14.1")
}