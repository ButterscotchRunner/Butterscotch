package net.perfectdreams.butterscotch.flowey

import com.github.ajalt.clikt.core.CliktCommand
import com.github.ajalt.clikt.core.main
import com.github.ajalt.clikt.parameters.options.help
import com.github.ajalt.clikt.parameters.options.option
import com.github.ajalt.clikt.parameters.options.required
import com.github.ajalt.clikt.parameters.types.boolean
import com.github.ajalt.clikt.parameters.types.file
import com.github.ajalt.clikt.parameters.types.int
import com.typesafe.config.ConfigFactory
import kotlinx.serialization.hocon.Hocon
import kotlinx.serialization.hocon.decodeFromConfig
import java.awt.Color
import java.io.File
import java.time.Duration
import java.time.Instant
import java.util.concurrent.TimeUnit
import javax.imageio.ImageIO
import kotlin.concurrent.thread
import kotlin.math.abs
import kotlin.system.exitProcess

class Flowey : CliktCommand() {
    val testSuite by option().file().required().help("Path to the test suite")
    val butterscotchPath by option().file().required().help("Path to Butterscotch")
    val skipCommercialGames by option().boolean().required().help("Skips tests that require commercial game WADs")

    override fun run() {
        val testSuiteConfig = Hocon.decodeFromConfig<TestSuite>(ConfigFactory.parseFile(testSuite))
        val testResults = mutableMapOf<String, TestResult>()

        testLoop@for (test in testSuiteConfig.tests) {
            if (test.commercialGame && skipCommercialGames) {
                testResults[test.name] = TestResult(Instant.now(), 0, emptyList(), emptyList()).apply { state = TestResult.State.Skipped }
                continue
            }
            println("Executing \"${test.name}\"")
            val startedAt = Instant.now()
            val process = ProcessBuilder(butterscotchPath.absolutePath, *test.butterscotchArgs.toTypedArray())
                .directory(testSuite.parentFile)
                .start()

            val stdoutBuilder = StringBuilder()
            val stderrBuilder = StringBuilder()

            val stdoutThread = thread {
                process.inputStream.bufferedReader().forEachLine { line ->
                    stdoutBuilder.appendLine(line)
                }
            }

            val stderrThread = thread {
                process.errorStream.bufferedReader().forEachLine { line ->
                    stderrBuilder.appendLine(line)
                }
            }

            val finished = process.waitFor(5L, TimeUnit.MINUTES)

            if (!finished) {
                process.destroyForcibly()
            }

            stdoutThread.join()
            stderrThread.join()

            val stdoutLines = stdoutBuilder.toString().lines()
            val stderrLines = stderrBuilder.toString().lines()

            val result = TestResult(startedAt, process.exitValue(), stdoutLines, stderrLines)
            result.endedAt = Instant.now()
            testResults[test.name] = result

            try {
                if (process.exitValue() != 0)
                    error("Exit Status is not 0!")

                for (pack in test.expectedStdoutOutput) {
                    if (stdoutLines.windowed(pack.size).indexOf(pack) == -1)
                        error("Stdout does not contain required lines!")
                }

                for (pack in test.expectedStderrOutput) {
                    if (stderrLines.windowed(pack.size).indexOf(pack) == -1)
                        error("Stderr does not contain required lines!")
                }

                for (pack in test.expectedScreenshots) {
                    val expected = File(testSuite.parentFile, pack.expected)
                    val actual = File(testSuite.parentFile, pack.actual)

                    if (!expected.exists() || !actual.exists())
                        error("Expected or actual image does not exist!")

                    val expectedImage = ImageIO.read(expected)
                    val actualImage = ImageIO.read(actual)

                    if (expectedImage.width != actualImage.width || expectedImage.height != actualImage.height)
                        error("Image size mismatch!")

                    for (y in 0 until expectedImage.height) {
                        for (x in 0 until expectedImage.width) {
                            val expectedPixel = expectedImage.getRGB(x, y)
                            val actualPixel = actualImage.getRGB(x, y)

                            val expectedColor = Color(expectedPixel)
                            val actualColor = Color(actualPixel)

                            val differenceR = abs(expectedColor.red - actualColor.red)
                            val differenceG = abs(expectedColor.green - actualColor.green)
                            val differenceB = abs(expectedColor.blue - actualColor.blue)

                            // Sometimes the image can be a *bit* different because of differences in GPU drivers
                            if (differenceR > 1 || differenceG > 1 || differenceB > 1)
                                error("Pixel ($x, $y) is different in ${pack.actual}! ${Color(expectedPixel)} != ${Color(actualPixel)} (difference: $differenceR, $differenceG, $differenceB)")
                        }
                    }
                }

                result.state = TestResult.State.Success
            } catch (e: IllegalStateException) {
                result.state = TestResult.State.Failure("${e.message}")
            }
        }

        val failedTests = testResults.filter { it.value.state is TestResult.State.Failure }

        val summary = buildString {
            appendLine("# \uD83E\uDDEA Butterscotch Test Results")
            appendLine("## \uD83D\uDCC4 Tests Summary")
            appendLine("| Test | Duration | Status |")
            appendLine("| - | - | - |")
            for ((name, result) in testResults.entries) {
                val emoji = when (result.state) {
                    TestResult.State.Success -> "✅"
                    is TestResult.State.Failure -> "🚫"
                    TestResult.State.Skipped -> "⚠️"
                    TestResult.State.Unknown -> "\uD83E\uDD37"
                }
                appendLine("| $name | ${result.endedAt?.let { formatDuration(result.startedAt, it) } ?: "N/A"} | $emoji |")
            }

            if (failedTests.isNotEmpty()) {
                appendLine("## \uD83D\uDE2D Failed Tests")
                for ((name, result) in failedTests) {
                    appendLine()
                    appendLine("<details><summary>🚫 <code>${name}</code></summary>")
                    appendLine()
                    appendLine("**Reason:** ${(result.state as TestResult.State.Failure).reason}")
                    appendLine()
                    appendLine("**Exit Code:** ${result.exitCode}")
                    appendLine()
                    appendLine("**stdout**")
                    appendLine("```")
                    result.stdoutLines.forEach { appendLine(it) }
                    appendLine("```")
                    appendLine("**stderr**")
                    appendLine("```")
                    result.stderrLines.forEach { appendLine(it) }
                    appendLine("```")
                    appendLine("</details>")
                }
            }
        }
        print(summary)
        System.getenv("GITHUB_STEP_SUMMARY")?.let { File(it).appendText(summary) }

        if (failedTests.isNotEmpty()) {
            exitProcess(1)
        } else {
            exitProcess(0)
        }
    }

    fun formatDuration(start: Instant, end: Instant): String {
        val d = Duration.between(start, end)
        return buildList {
            println(d)
            if (d.toHoursPart() > 0) add("${d.toHoursPart()}h")
            if (d.toMinutesPart() > 0) add("${d.toMinutesPart()}m")
            if (d.toSecondsPart() > 0) add("${d.toSecondsPart()}s")
            if (d.toMillisPart() > 0) add("${d.toMillisPart()}ms")
        }.joinToString(" ").ifEmpty { "0ms" }
    }

    class TestResult(
        val startedAt: Instant,
        var exitCode: Int,
        var stdoutLines: List<String>,
        var stderrLines: List<String>
    ) {
        var state: State = State.Unknown
        var endedAt: Instant? = null

        sealed class State {
            object Success : State()
            data class Failure(
                val reason: String
            ) : State()
            object Skipped : State()
            object Unknown : State()
        }
    }
}

fun main(args: Array<String>) = Flowey().main(args)