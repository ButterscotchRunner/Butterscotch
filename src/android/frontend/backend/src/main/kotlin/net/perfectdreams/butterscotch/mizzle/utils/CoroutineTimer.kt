package net.perfectdreams.butterscotch.mizzle.utils

import kotlinx.coroutines.CoroutineName
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import net.perfectdreams.harmony.logging.HarmonyLoggerFactory
import kotlin.time.Duration

/**
 * Schedules [action] to be executed on [scope] every [period] with a [initialDelay]
 */
fun scheduleCoroutineAtFixedRate(taskName: String, scope: CoroutineScope, period: Duration, initialDelay: Duration = Duration.ZERO, action: RunnableCoroutine) {
    val logger by HarmonyLoggerFactory.logger(taskName)

    scope.launch(CoroutineName("$taskName Scheduler")) {
        delay(initialDelay)

        val mutex = Mutex()

        while (true) {
            launch(CoroutineName("$taskName Task")) {
                logger.info { "Preparing to run task - Is mutex locked? ${mutex.isLocked}" }
                mutex.withLock {
                    logger.info { "Running task..." }
                    try {
                        action.run()
                    } catch (e: Throwable) {
                        logger.warn(e) { "Uncaught error when running the task!" }
                    }
                    logger.info { "Task has finished running!" }
                }
            }
            logger.info { "Waiting $period to execute next task..." }
            delay(period)
        }
    }
}

fun interface RunnableCoroutine {
    suspend fun run()
}