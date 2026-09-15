package net.perfectdreams.butterscotch.mizzle.routes.v1

import io.ktor.server.application.ApplicationCall
import io.ktor.server.response.respondText
import kotlinx.serialization.json.Json
import net.perfectdreams.butterscotch.mizzle.Mizzle
import net.perfectdreams.butterscotch.mizzle.tables.SampleGames
import net.perfectdreams.butterscotch.network.SampleGamesResponse
import net.perfectdreams.sequins.ktor.BaseRoute
import org.jetbrains.exposed.v1.core.eq
import org.jetbrains.exposed.v1.jdbc.selectAll

class SampleGamesRoute(val m: Mizzle) : APIv1Route("/samples") {
    override suspend fun onRequest(call: ApplicationCall) {
        val results = m.transaction {
            SampleGames.selectAll()
                .where {
                    SampleGames.enabled eq true
                }
                .orderBy(SampleGames.name)
                .toList()
        }

        call.respondText(
            Json.encodeToString(
                SampleGamesResponse(
                    results.map {
                        SampleGamesResponse.Game(
                            it[SampleGames.slug],
                            it[SampleGames.name],
                            it[SampleGames.description],
                            it[SampleGames.version],
                            it[SampleGames.author]
                        )
                    }
                ),
            )
        )
    }
}