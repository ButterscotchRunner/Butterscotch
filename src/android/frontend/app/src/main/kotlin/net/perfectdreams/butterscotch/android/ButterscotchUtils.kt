package net.perfectdreams.butterscotch.android

import io.ktor.client.HttpClient
import io.ktor.client.engine.cio.CIO
import io.ktor.client.plugins.defaultRequest

object ButterscotchUtils {
    // Shared HTTP client used across the application
    val http by lazy {
        HttpClient(CIO) {
            expectSuccess = false
            defaultRequest { url(BuildConfig.API_BASE_URL) }
        }
    }
}
