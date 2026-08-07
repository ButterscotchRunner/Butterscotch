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
import net.perfectdreams.butterscotch.mizzle.tables.SampleGames
import net.perfectdreams.butterscotch.network.AndroidAnalyticsLaunchAppRequest
import net.perfectdreams.butterscotch.mizzle.utils.trueIp
import net.perfectdreams.butterscotch.network.SampleGamesResponse
import net.perfectdreams.sequins.ktor.BaseRoute
import org.jetbrains.exposed.v1.core.SortOrder
import org.jetbrains.exposed.v1.core.eq
import org.jetbrains.exposed.v1.jdbc.insert
import org.jetbrains.exposed.v1.jdbc.selectAll
import java.time.OffsetDateTime
import java.time.ZoneOffset
import java.util.Locale

class PostAndroidAnalyticsLaunchAppRoute(val m: Mizzle) : APIv1Route("/android/analytics/launch-app") {
    override suspend fun onRequest(call: ApplicationCall) {
        val request = call.receiveText().let { Json.decodeFromString<AndroidAnalyticsLaunchAppRequest>(it) }

        val ip = call.request.trueIp

        m.transaction {
            val salt = ActiveSalts.selectAll().orderBy(ActiveSalts.generatedAt, SortOrder.DESC).limit(1).first()[ActiveSalts.salt]

            val identifier = m.generateIdentifier(salt, ip, request.androidDeviceModel)

            AndroidAnalyticsLaunchApp.insert {
                it[AndroidAnalyticsLaunchApp.launchedAt] = OffsetDateTime.now(ZoneOffset.UTC)
                it[AndroidAnalyticsLaunchApp.identifier] = identifier

                it[AndroidAnalyticsLaunchApp.androidSdkVersion] = request.androidSdkVersion
                it[AndroidAnalyticsLaunchApp.androidDeviceManufacturer] = request.androidDeviceManufacturer
                it[AndroidAnalyticsLaunchApp.androidDeviceModel] = request.androidDeviceModel

                it[AndroidAnalyticsLaunchApp.locale] = request.locale

                it[AndroidAnalyticsLaunchApp.isPlus] = request.isPlus

                it[AndroidAnalyticsLaunchApp.appVersionName] = request.appVersionName
                it[AndroidAnalyticsLaunchApp.appVersionCode] = request.appVersionCode
            }
        }

        call.respond(HttpStatusCode.NoContent)
    }
}