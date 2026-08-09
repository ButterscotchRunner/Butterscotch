package net.perfectdreams.butterscotch.network

import kotlinx.serialization.Serializable
import net.perfectdreams.butterscotch.UUIDAsStringSerializer
import java.util.UUID

@Serializable
class AndroidAnalyticsLaunchAppRequest(
    val appVersionName: String,
    val appVersionCode: Int,
    val androidSdkVersion: Int,
    val androidDeviceManufacturer: String,
    val androidDeviceModel: String,
    val locale: String,
    val isPlus: Boolean,
)