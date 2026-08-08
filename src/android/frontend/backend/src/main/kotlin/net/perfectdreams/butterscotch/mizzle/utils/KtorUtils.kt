package net.perfectdreams.butterscotch.mizzle.utils

import io.ktor.server.request.ApplicationRequest
import io.ktor.server.request.header

/**
 * Returns the request "true IP"
 * If the "X-Forwarded-For" header is set, then the value of that header is used, if not, Jooby's [Request.ip()] is used
 */
val ApplicationRequest.trueIp: String get() {
    val forwardedForHeader = this.header("X-Forwarded-For")
    return forwardedForHeader?.split(",")?.map { it.trim() }?.first() ?: this.local.remoteHost
        .let {
            // TODO: When Ktor is updated to 2.2.0 this won't be needed: https://github.com/ktorio/ktor/pull/3122
            if (it == "kubernetes.docker.internal")
                "127.0.0.1"
            else
                it
        }
}