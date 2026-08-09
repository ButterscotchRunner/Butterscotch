package net.perfectdreams.butterscotch.mizzle

import io.ktor.client.HttpClient
import io.ktor.client.engine.java.Java
import io.ktor.client.request.get
import io.ktor.client.request.post
import io.ktor.client.request.setBody
import io.ktor.client.statement.bodyAsText
import io.ktor.http.ContentType
import io.ktor.http.contentType
import io.ktor.server.engine.EngineConnectorBuilder
import io.ktor.server.engine.embeddedServer
import io.ktor.server.http.content.staticFiles
import io.ktor.server.netty.Netty
import io.ktor.server.response.respondText
import io.ktor.server.routing.get
import io.ktor.server.routing.localPort
import io.ktor.server.routing.routing
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.runBlocking
import kotlinx.serialization.json.addJsonObject
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.put
import kotlinx.serialization.json.putJsonArray
import kotlinx.serialization.json.putJsonObject
import net.perfectdreams.butterscotch.mizzle.config.MizzleConfig
import net.perfectdreams.butterscotch.mizzle.routes.v1.PostAndroidAnalyticsLaunchAppRoute
import net.perfectdreams.butterscotch.mizzle.routes.v1.PostAndroidAnalyticsLaunchGameRoute
import net.perfectdreams.butterscotch.mizzle.routes.v1.SampleGamesRoute
import net.perfectdreams.butterscotch.mizzle.tables.ActiveSalts
import net.perfectdreams.butterscotch.mizzle.tables.AndroidAnalyticsLaunchApp
import net.perfectdreams.butterscotch.mizzle.tables.AndroidAnalyticsLaunchGame
import net.perfectdreams.butterscotch.mizzle.tables.AndroidTrackedAppVersions
import net.perfectdreams.butterscotch.mizzle.tables.SampleGames
import net.perfectdreams.butterscotch.mizzle.utils.RunnableCoroutine
import net.perfectdreams.butterscotch.mizzle.utils.scheduleCoroutineAtFixedRate
import net.perfectdreams.harmony.logging.HarmonyLoggerFactory
import org.jetbrains.exposed.v1.core.SortOrder
import org.jetbrains.exposed.v1.core.less
import org.jetbrains.exposed.v1.jdbc.Database
import org.jetbrains.exposed.v1.jdbc.JdbcTransaction
import org.jetbrains.exposed.v1.jdbc.SchemaUtils
import org.jetbrains.exposed.v1.jdbc.deleteWhere
import org.jetbrains.exposed.v1.jdbc.insert
import org.jetbrains.exposed.v1.jdbc.selectAll
import org.jetbrains.exposed.v1.jdbc.transactions.suspendTransaction
import java.io.File
import java.security.MessageDigest
import java.security.SecureRandom
import java.time.Instant
import java.time.LocalDate
import java.time.LocalDateTime
import java.time.LocalTime
import java.time.OffsetDateTime
import java.time.ZoneOffset
import java.util.Base64
import kotlin.time.Duration.Companion.days
import kotlin.time.Duration.Companion.milliseconds
import kotlin.time.Duration.Companion.minutes

class Mizzle(val config: MizzleConfig, val database: Database) {
    companion object {
        private const val PACKAGE_ID = "net.perfectdreams.butterscotch"
        private val PLAY_STORE_VERSION_REGEX = Regex("\\[\\[\\[\"([0-9A-z-.]+)\"\\]\\],\\[\\[\\[36\\]\\]")
        private val logger by HarmonyLoggerFactory.logger {}
    }

    val http = HttpClient(Java) {
        expectSuccess = false
    }

    val routes = listOf(
        SampleGamesRoute(this),
        PostAndroidAnalyticsLaunchAppRoute(this),
        PostAndroidAnalyticsLaunchGameRoute(this)
    )

    val secureRandom = SecureRandom()
    val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())

    fun start() {
        runBlocking {
            transaction {
                SchemaUtils.createMissingTablesAndColumns(
                    ActiveSalts,
                    SampleGames,
                    AndroidAnalyticsLaunchApp,
                    AndroidAnalyticsLaunchGame,
                    AndroidTrackedAppVersions
                )
            }

            // Make sure there's a salt before the server starts accepting requests
            ensureFreshSalt()
        }

        scheduleCoroutineEveryDayAtSpecificHour("Salt Rotation", LocalTime.MIDNIGHT) {
            ensureFreshSalt()
        }

        scheduleCoroutineAtFixedRate("QueryPlayStoreVersion", scope, 15.minutes) {
            val scrappedVersionFromPlayStore = scrapPlayStoreAppVersion(PACKAGE_ID)

            if (scrappedVersionFromPlayStore != null) {
                val scrappedVersion = ButterscotchVersion.parse(scrappedVersionFromPlayStore)

                logger.info { "Latest Version on the Play Store is $scrappedVersion" }

                val isNewer = transaction {
                    val trackedVersionRow = AndroidTrackedAppVersions
                        .selectAll()
                        .orderBy(AndroidTrackedAppVersions.detectedAt, SortOrder.DESC)
                        .limit(1)
                        .firstOrNull()

                    val trackedVersion = trackedVersionRow?.let { ButterscotchVersion.parse(it[AndroidTrackedAppVersions.versionName]) }

                    if (trackedVersion == null || scrappedVersion > trackedVersion) {
                        AndroidTrackedAppVersions.insert {
                            it[AndroidTrackedAppVersions.detectedAt] = OffsetDateTime.now(ZoneOffset.UTC)
                            it[AndroidTrackedAppVersions.versionName] = scrappedVersionFromPlayStore
                        }
                        return@transaction true
                    } else {
                        return@transaction false
                    }
                }

                if (isNewer) {
                    http.post(config.discordWebhookUrl + "?with_components=true") {
                        contentType(ContentType.Application.Json)
                        setBody(
                            buildJsonObject {
                                put(
                                    "content",
                                    buildString {
                                        appendLine("<:butterscotch:1514351490515603659><:android_logo:1515489886692577332> **Version `${scrappedVersion.prettify()}` Released!** <@&${config.discordUpdateRoleId}>")
                                    }
                                )

                                putJsonArray("components") {
                                    addJsonObject {
                                        put("type", 1)
                                        putJsonArray("components") {
                                            addJsonObject {
                                                put("type", 2)
                                                put("style", 5)
                                                put("label", "Google Play")
                                                put("url", "https://play.google.com/store/apps/details?id=net.perfectdreams.butterscotch&referrer=utm_source%3Ddiscord%26utm_medium%3Dreferral%26utm_content%3Dupdate-announcement")
                                                putJsonObject("emoji") {
                                                    put("id", "1515498288856436897")
                                                    put("name", "google_play_logo")
                                                    put("animated", false)
                                                }
                                            }
                                        }
                                    }
                                }

                            }.toString()
                        )
                    }
                }
            } else {
                logger.warn { "Could not find latest version on the Play Store!" }
            }
        }

        val server = embeddedServer(
            Netty,
            configure = {
                connectors.add(EngineConnectorBuilder().apply {
                    host = "0.0.0.0"
                    port = 8080
                })
            }
        ) {
            routing {
                localPort(8080) {
                    get("/") {
                        call.respondText("ButterscotchRunner (Mizzle Backend) - Howdy! Loritta is so cute!!")
                    }

                    for (route in routes) {
                        route.register(this)
                    }
                }

                staticFiles("/samples", File(config.samplesPath))

                staticFiles("/flowey", File(config.floweyPath))
            }
        }
        server.start(wait = true)
    }

    private fun scheduleCoroutineEveryDayAtSpecificHour(taskName: String, time: LocalTime, action: RunnableCoroutine) {
        val now = Instant.now()
        val today = LocalDate.now(ZoneOffset.UTC)
        val todayAtTime = LocalDateTime.of(today, time)
        val gonnaBeScheduledAtTime =  if (now > todayAtTime.toInstant(ZoneOffset.UTC)) {
            // If today at time is larger than today, then it means that we need to schedule it for tomorrow
            todayAtTime.plusDays(1)
        } else todayAtTime

        val diff = gonnaBeScheduledAtTime.toInstant(ZoneOffset.UTC).toEpochMilli() - System.currentTimeMillis()

        scheduleCoroutineAtFixedRate(
            taskName,
            scope,
            1.days,
            diff.milliseconds,
            action
        )
    }

    fun generateSalt(): String {
        val bytes = ByteArray(16)
        secureRandom.nextBytes(bytes)
        return Base64.getEncoder().encodeToString(bytes)
    }

    // The secret rotating salt is what makes this private, without it the small IP + device input space could be brute forced to deanonymize visitors
    fun generateIdentifier(salt: String, ip: String, deviceModel: String): String {
        val input = "$salt|$ip|$deviceModel"
        return MessageDigest.getInstance("SHA-256").digest(input.toByteArray(Charsets.UTF_8)).joinToString("") { "%02x".format(it) }
    }

    // Rotates the salt daily so visitor identifiers can't be correlated across days, once a salt is deleted its identifiers can't be reconstructed
    suspend fun ensureFreshSalt() {
        transaction {
            val now = OffsetDateTime.now(ZoneOffset.UTC)

            val newestSaltDay = ActiveSalts.selectAll()
                .orderBy(ActiveSalts.generatedAt, SortOrder.DESC)
                .limit(1)
                .firstOrNull()
                ?.get(ActiveSalts.generatedAt)
                ?.toLocalDate()

            if (newestSaltDay != now.toLocalDate()) {
                ActiveSalts.insert {
                    it[ActiveSalts.salt] = generateSalt()
                    it[ActiveSalts.generatedAt] = now
                }
            }

            ActiveSalts.deleteWhere { ActiveSalts.generatedAt less now.minusHours(24) }
        }
    }

    suspend fun <T> transaction(statement: suspend JdbcTransaction.() -> (T)): T {
        return suspendTransaction(database) {
            statement()
        }
    }

    suspend fun scrapPlayStoreAppVersion(packageId: String): String? {
        val response = http.get("https://play.google.com/store/apps/details?id=$packageId")
            .bodyAsText(Charsets.UTF_8)

        return PLAY_STORE_VERSION_REGEX.find(response)?.groupValues[1]
    }
}