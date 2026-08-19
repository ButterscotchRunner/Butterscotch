import java.util.Properties
import org.gradle.api.DefaultTask
import org.gradle.api.file.DirectoryProperty
import org.gradle.api.tasks.Input
import org.gradle.api.tasks.OutputDirectory
import org.gradle.api.tasks.TaskAction

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
    alias(libs.plugins.kotlin.serialization)
}

// Signing settings
val keystorePropertiesFile = rootProject.file("keystore.properties")
val keystoreProperties = Properties().apply {
    if (keystorePropertiesFile.exists()) keystorePropertiesFile.inputStream().use { load(it) }
}

val butterscotchRepoDir = file("../../../../")

val ccachePath = System.getenv("PATH")
    .orEmpty()
    .split(File.pathSeparator)
    .map { File(it, "ccache") }
    .firstOrNull { it.canExecute() }

android {
    namespace = "net.perfectdreams.butterscotch.android"
    compileSdk {
        version = release(36) {
            minorApiLevel = 1
        }
    }

    defaultConfig {
        applicationId = "net.perfectdreams.butterscotch"
        minSdk = 24
        targetSdk = 36
        versionCode = 32
        versionName = "2026.06.21-1"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DPLATFORM=android",
                    "-DENABLE_MODERN_GL=ON",
                    "-DENABLE_LEGACY_GL=OFF",
                    "-DAUDIO_BACKEND=miniaudio"
                )
                if (ccachePath != null) {
                    arguments += listOf("-DCMAKE_C_COMPILER_LAUNCHER=${ccachePath.absolutePath}", "-DCMAKE_CXX_COMPILER_LAUNCHER=${ccachePath.absolutePath}")
                }
                cppFlags += "-std=c++17"
                cFlags += "-std=gnu99"
            }
        }
    }

    signingConfigs {
        create("release") {
            if (keystorePropertiesFile.exists()) {
                storeFile = file(keystoreProperties["storeFile"] as String)
                storePassword = keystoreProperties["storePassword"] as String
                keyAlias = keystoreProperties["keyAlias"] as String
                keyPassword = keystoreProperties["keyPassword"] as String
            }
        }
    }

    buildTypes {
        release {
            buildConfigField("boolean", "FORCE_BUTTERSCOTCH_PLUS", "false")
            buildConfigField("String", "API_BASE_URL", "\"https://mizzle.butterscotch.gg\"")
            buildConfigField("String", "API_VERSION", "\"v1\"")

            isMinifyEnabled = true
            isShrinkResources = true
            signingConfig = if (keystorePropertiesFile.exists()) signingConfigs.getByName("release") else null

            // Upload native symbol tables so Play Console can symbolicate crashes inside the Butterscotch runtime
            ndk {
                debugSymbolLevel = "SYMBOL_TABLE"
                abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
            }

            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }

        debug {
            buildConfigField("boolean", "FORCE_BUTTERSCOTCH_PLUS", "true")
            buildConfigField("String", "API_BASE_URL", "\"http://192.168.15.125:8080\"")
            buildConfigField("String", "API_VERSION", "\"v1\"")

            ndk {
                abiFilters += listOf("arm64-v8a")
            }
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    buildFeatures {
        compose = true
        buildConfig = true
    }

    externalNativeBuild {
        cmake {
            // Point straight at the Butterscotch repo's root CMakeLists.txt.
            // Adjust if your checkout layout differs.
            path = File(butterscotchRepoDir, "CMakeLists.txt")
            version = "3.22.1"
        }
    }

}

abstract class GenerateContributorsTask : DefaultTask() {
    @get:Input
    abstract val repoPath: Property<String>

    @get:OutputDirectory
    abstract val outputDir: DirectoryProperty

    @TaskAction
    fun generate() {
        val repoDir = File(repoPath.get())
        if (!repoDir.resolve(".git").exists())
            error("The Butterscotch repository is not a real git repository!")

        val process = ProcessBuilder(
            "git", "-C", repoDir.absolutePath, "log", "--format=%aN"
        ).redirectErrorStream(true).start()

        val rawAuthors = process.inputStream.bufferedReader().use { it.readLines() }
        process.waitFor()

        val contributors = if (process.exitValue() != 0) emptyList() else
            rawAuthors.asSequence()
                .map { it.trim() }
                .filter { it.isNotEmpty() }
                .filter { !it.equals("MrPowerGamerBR", ignoreCase = true) }
                .distinct()
                .sortedBy { it.lowercase() }
                .toList()

        val rawDir = outputDir.get().asFile.resolve("raw").apply { mkdirs() }
        rawDir.resolve("contributors.txt")
            .writeText(contributors.joinToString("\n"))
    }
}

androidComponents {
    onVariants { variant ->
        val taskName = "generate${variant.name.replaceFirstChar { it.uppercase() }}Contributors"
        val generateTask = tasks.register(taskName, GenerateContributorsTask::class.java) {
            repoPath.set(butterscotchRepoDir.absolutePath)
        }
        variant.sources.res?.addGeneratedSourceDirectory(
            generateTask, GenerateContributorsTask::outputDir
        )
    }
}

dependencies {
    implementation(project(":common"))
    implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.11.0")

    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.compose.material.icons.core)
    implementation(libs.androidx.compose.material.icons.extended)
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.graphics)
    implementation(libs.androidx.compose.ui.tooling.preview)
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.core.splashscreen)
    implementation(libs.androidx.documentfile)
    implementation(libs.androidx.navigation.compose)
    implementation(libs.kotlinx.serialization.json)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.11.0")
    implementation("io.ktor:ktor-client-cio:3.5.0")
    implementation("com.google.android.play:app-update:2.1.0")
    implementation("com.google.android.play:app-update-ktx:2.1.0")
    implementation("androidx.fragment:fragment-ktx:1.8.5")
    testImplementation(libs.junit)
    androidTestImplementation(platform(libs.androidx.compose.bom))
    androidTestImplementation(libs.androidx.compose.ui.test.junit4)
    androidTestImplementation(libs.androidx.espresso.core)
    androidTestImplementation(libs.androidx.junit)
    debugImplementation(libs.androidx.compose.ui.test.manifest)
    debugImplementation(libs.androidx.compose.ui.tooling)
}