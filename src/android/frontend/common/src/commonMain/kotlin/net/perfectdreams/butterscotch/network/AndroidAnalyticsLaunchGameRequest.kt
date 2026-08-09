package net.perfectdreams.butterscotch.network

import kotlinx.serialization.Serializable
import net.perfectdreams.butterscotch.UUIDAsStringSerializer
import java.util.UUID

@Serializable
class AndroidAnalyticsLaunchGameRequest(
    val appVersionName: String,
    val appVersionCode: Int,
    val androidSdkVersion: Int,
    val androidDeviceManufacturer: String,
    val androidDeviceModel: String,
    val locale: String,
    val isPlus: Boolean,
    val gpuVendor: String,
    val gpuRenderer: String,
    val gpuVersion: String,
    val wadHash: Long,
    val wadName: String?,
    val wadDisplayName: String?,
    val wadVersion: Int,
    val wadGmsVersion: String,
    val wadDetectedGmsVersion: String
)