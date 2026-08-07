package net.perfectdreams.butterscotch.mizzle.tables

import org.jetbrains.exposed.v1.core.dao.id.LongIdTable
import org.jetbrains.exposed.v1.core.java.javaUUID
import org.jetbrains.exposed.v1.javatime.timestampWithTimeZone

object AndroidAnalyticsLaunchGame : LongIdTable() {
    val launchedAt = timestampWithTimeZone("launched_at").index()
    val identifier = text("identifier")
    val androidSdkVersion = integer("android_sdk_version")
    val androidDeviceManufacturer = text("android_device_manufacturer")
    val androidDeviceModel = text("android_device_model")
    val locale = text("locale")
    val isPlus = bool("is_plus")
    val gpuVendor = text("gpu_vendor")
    val gpuRenderer = text("gpu_renderer")
    val gpuVersion = text("gpu_version")
    val appVersionName = text("app_version_name")
    val appVersionCode = integer("app_version_code")
    val wadHash = long("wad_hash")
    val wadName = text("wad_name").nullable()
    val wadDisplayName = text("wad_display_name").nullable()
    val wadVersion = integer("wad_version")
    val wadGmsVersion = text("wad_gms_version")
    val wadDetectedGmsVersion = text("wad_detected_gms_version")
}