package net.perfectdreams.butterscotch.mizzle.tables

import org.jetbrains.exposed.v1.core.dao.id.LongIdTable
import org.jetbrains.exposed.v1.javatime.timestampWithTimeZone

object AndroidTrackedAppVersions : LongIdTable() {
    val detectedAt = timestampWithTimeZone("detected_at").index()
    val versionName = text("version_name")
}