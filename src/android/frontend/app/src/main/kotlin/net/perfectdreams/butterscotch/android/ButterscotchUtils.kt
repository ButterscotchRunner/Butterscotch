package net.perfectdreams.butterscotch.android

import android.content.Context
import android.os.Build
import android.util.Log
import io.ktor.client.HttpClient
import io.ktor.client.engine.cio.CIO
import io.ktor.client.plugins.defaultRequest
import io.ktor.client.request.post
import io.ktor.client.request.setBody
import kotlinx.coroutines.GlobalScope
import kotlinx.coroutines.launch
import kotlinx.serialization.json.Json
import net.perfectdreams.butterscotch.network.AndroidAnalyticsLaunchAppRequest
import net.perfectdreams.butterscotch.network.AndroidAnalyticsLaunchGameRequest
import java.util.Locale

object ButterscotchUtils {
    const val TAG = "ButterscotchUtils"
    private var hasLaunchedOnThisSession = false

    val http by lazy {
        HttpClient(CIO) {
            expectSuccess = false
            defaultRequest { url(BuildConfig.API_BASE_URL) }
        }
    }

    fun fireAppLaunchEvent(isPlus: Boolean) {
        // This can ONLY happen if the MainActivity gets destroyed for some reason
        if (this.hasLaunchedOnThisSession)
            return

        this.hasLaunchedOnThisSession = true

        GlobalScope.launch {
            try {
                val response = http.post("/api/${BuildConfig.API_VERSION}/android/analytics/launch-app") {
                    setBody(
                        Json.encodeToString<AndroidAnalyticsLaunchAppRequest>(
                            AndroidAnalyticsLaunchAppRequest(
                                BuildConfig.VERSION_NAME,
                                BuildConfig.VERSION_CODE,
                                Build.VERSION.SDK_INT,
                                Build.MANUFACTURER,
                                Build.MODEL,
                                Locale.getDefault().toLanguageTag(),
                                isPlus
                            )
                        )
                    )
                }

                Log.i(TAG, "Sent analytics message! Status code: ${response.status}")
            } catch (e: Exception) {
                // Fire and forget...
                Log.w(TAG, "Failed to log analytics message!", e)
            }
        }
    }

    fun fireGameLaunchEvent(wadHash: Long, name: String?, displayName: String?, wadVersion: Int, gmsVersion: String, detectedGmsVersion: String, gpuVendor: String, gpuRenderer: String, gpuVersion: String, isPlus: Boolean) {
        GlobalScope.launch {
            try {
                val response = http.post("/api/${BuildConfig.API_VERSION}/android/analytics/launch-game") {
                    setBody(
                        Json.encodeToString(
                            AndroidAnalyticsLaunchGameRequest(
                                BuildConfig.VERSION_NAME,
                                BuildConfig.VERSION_CODE,
                                Build.VERSION.SDK_INT,
                                Build.MANUFACTURER,
                                Build.MODEL,
                                Locale.getDefault().toLanguageTag(),
                                isPlus,
                                gpuVendor,
                                gpuRenderer,
                                gpuVersion,
                                wadHash,
                                name,
                                displayName,
                                wadVersion,
                                gmsVersion,
                                detectedGmsVersion
                            )
                        )
                    )
                }

                Log.i(TAG, "Sent analytics message! Status code: ${response.status}")
            } catch (e: Exception) {
                // Fire and forget...
                Log.w(TAG, "Failed to log analytics message!", e)
            }
        }
    }
}