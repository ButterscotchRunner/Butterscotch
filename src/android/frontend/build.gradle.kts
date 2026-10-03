// Top-level build file where you can add configuration options common to all sub-projects/modules.
plugins {
    alias(libs.plugins.android.application) apply false
    alias(libs.plugins.kotlin.compose) apply false
    alias(libs.plugins.kotlin.serialization) apply false
    kotlin("jvm") version "2.2.10" apply false
    kotlin("multiplatform") version "2.2.10" apply false
}

allprojects {
    repositories {
        google()
        mavenCentral()
        maven("https://repo.perfectdreams.net/")
    }
}