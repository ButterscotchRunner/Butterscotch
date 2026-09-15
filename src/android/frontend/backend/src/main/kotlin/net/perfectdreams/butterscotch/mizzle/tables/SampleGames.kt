package net.perfectdreams.butterscotch.mizzle.tables

import org.jetbrains.exposed.v1.core.Table

object SampleGames : Table() {
    val slug = text("slug")
    val name = text("name")
    val description = text("description")
    val version = text("version")
    val author = text("author")
    val enabled = bool("enabled").default(true)

    override val primaryKey = PrimaryKey(slug)
}