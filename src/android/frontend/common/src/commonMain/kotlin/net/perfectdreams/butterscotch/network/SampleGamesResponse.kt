package net.perfectdreams.butterscotch.network

import kotlinx.serialization.Serializable

@Serializable
data class SampleGamesResponse(val games: List<Game>) {
    @Serializable
    data class Game(
        val slug: String,
        val name: String,
        val description: String,
        val version: String,
        val author: String
    )
}