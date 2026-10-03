package net.perfectdreams.butterscotch.mizzle.routes.v1

import net.perfectdreams.sequins.ktor.BaseRoute

abstract class APIv1Route(val nonVersionedPath: String) : BaseRoute("/api/v1$nonVersionedPath")