package net.perfectdreams.butterscotch.mizzle.tables

import org.jetbrains.exposed.v1.core.dao.id.LongIdTable
import org.jetbrains.exposed.v1.core.java.javaUUID
import org.jetbrains.exposed.v1.javatime.timestampWithTimeZone

object AndroidAnalyticsLaunchApp : LongIdTable() {
    val launchedAt = timestampWithTimeZone("launched_at").index()
    val identifier = text("identifier")
    val androidSdkVersion = integer("android_sdk_version").nullable()
    val androidDeviceManufacturer = text("android_device_manufacturer").nullable()
    val androidDeviceModel = text("android_device_model").nullable()
    val locale = text("locale")
    val isPlus = bool("is_plus")
    val appVersionName = text("app_version_name")
    val appVersionCode = integer("app_version_code")
}