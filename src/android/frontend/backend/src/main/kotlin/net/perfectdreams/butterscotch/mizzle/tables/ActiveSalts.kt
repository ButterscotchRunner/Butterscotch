package net.perfectdreams.butterscotch.mizzle.tables

import org.jetbrains.exposed.v1.core.dao.id.java.UUIDTable
import org.jetbrains.exposed.v1.javatime.timestampWithTimeZone

object ActiveSalts : UUIDTable() {
    val salt = text("salt")
    val generatedAt = timestampWithTimeZone("generated_at")
}