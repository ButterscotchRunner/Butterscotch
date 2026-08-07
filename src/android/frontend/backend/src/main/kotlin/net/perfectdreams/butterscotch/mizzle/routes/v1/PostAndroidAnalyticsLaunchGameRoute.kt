package net.perfectdreams.butterscotch.mizzle.routes.v1

import io.ktor.http.HttpStatusCode
import io.ktor.server.application.ApplicationCall
import io.ktor.server.request.receiveText
import io.ktor.server.response.respond
import io.ktor.server.response.respondText
import kotlinx.serialization.json.Json
import net.perfectdreams.butterscotch.mizzle.Mizzle
import net.perfectdreams.butterscotch.mizzle.tables.ActiveSalts
import net.perfectdreams.butterscotch.mizzle.tables.AndroidAnalyticsLaunchApp
import net.perfectdreams.butterscotch.mizzle.tables.AndroidAnalyticsLaunchGame
import net.perfectdreams.butterscotch.mizzle.tables.SampleGames
import net.perfectdreams.butterscotch.network.AndroidAnalyticsLaunchAppRequest
import net.perfectdreams.butterscotch.mizzle.utils.trueIp
import net.perfectdreams.butterscotch.network.AndroidAnalyticsLaunchGameRequest
import net.perfectdreams.butterscotch.network.SampleGamesResponse
import net.perfectdreams.sequins.ktor.BaseRoute
import org.jetbrains.exposed.v1.core.SortOrder
import org.jetbrains.exposed.v1.core.eq
import org.jetbrains.exposed.v1.jdbc.insert
import org.jetbrains.exposed.v1.jdbc.selectAll
import java.time.OffsetDateTime
import java.time.ZoneOffset

class PostAndroidAnalyticsLaunchGameRoute(val m: Mizzle) : APIv1Route("/android/analytics/launch-game") {
    override suspend fun onRequest(call: ApplicationCall) {
        val request = call.receiveText().let { Json.decodeFromString<AndroidAnalyticsLaunchGameRequest>(it) }

        val ip = call.request.trueIp

        m.transaction {
            val salt = ActiveSalts.selectAll().orderBy(ActiveSalts.generatedAt, SortOrder.DESC).limit(1).first()[ActiveSalts.salt]

            val identifier = m.generateIdentifier(salt, ip, request.androidDeviceModel)

            AndroidAnalyticsLaunchGame.insert {
                it[AndroidAnalyticsLaunchGame.launchedAt] = OffsetDateTime.now(ZoneOffset.UTC)
                it[AndroidAnalyticsLaunchGame.identifier] = identifier

                it[AndroidAnalyticsLaunchGame.androidSdkVersion] = request.androidSdkVersion
                it[AndroidAnalyticsLaunchGame.androidDeviceManufacturer] = request.androidDeviceManufacturer
                it[AndroidAnalyticsLaunchGame.androidDeviceModel] = request.androidDeviceModel

                it[AndroidAnalyticsLaunchGame.locale] = request.locale

                it[AndroidAnalyticsLaunchGame.isPlus] = request.isPlus

                it[AndroidAnalyticsLaunchGame.gpuVendor] = request.gpuVendor
                it[AndroidAnalyticsLaunchGame.gpuRenderer] = request.gpuRenderer
                it[AndroidAnalyticsLaunchGame.gpuVersion] = request.gpuVersion

                it[AndroidAnalyticsLaunchGame.appVersionName] = request.appVersionName
                it[AndroidAnalyticsLaunchGame.appVersionCode] = request.appVersionCode

                it[AndroidAnalyticsLaunchGame.wadHash] = request.wadHash
                it[AndroidAnalyticsLaunchGame.wadName] = request.wadName
                it[AndroidAnalyticsLaunchGame.wadDisplayName] = request.wadDisplayName
                it[AndroidAnalyticsLaunchGame.wadVersion] = request.wadVersion
                it[AndroidAnalyticsLaunchGame.wadGmsVersion] = request.wadGmsVersion
                it[AndroidAnalyticsLaunchGame.wadDetectedGmsVersion] = request.wadDetectedGmsVersion
            }
        }

        call.respond(HttpStatusCode.NoContent)
    }
}