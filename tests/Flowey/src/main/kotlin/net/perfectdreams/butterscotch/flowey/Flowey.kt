package net.perfectdreams.butterscotch.flowey

import com.github.ajalt.clikt.core.CliktCommand
import com.github.ajalt.clikt.core.main
import com.github.ajalt.clikt.parameters.options.help
import com.github.ajalt.clikt.parameters.options.option
import com.github.ajalt.clikt.parameters.options.required
import com.github.ajalt.clikt.parameters.types.file
import com.typesafe.config.ConfigFactory
import kotlinx.serialization.hocon.Hocon
import kotlinx.serialization.hocon.decodeFromConfig
import java.io.File
import java.util.Collections
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread
import kotlin.system.exitProcess

class Flowey : CliktCommand() {
    val testSuite by option().file().required().help("Path to the test suite")
    val butterscotchPath by option().file().required().help("Path to Butterscotch")

    override fun run() {
        val testSuiteConfig = Hocon.decodeFromConfig<TestSuite>(ConfigFactory.parseFile(testSuite))
        val testResults = mutableMapOf<String, TestResult>()

        testLoop@for (test in testSuiteConfig.tests) {
            println("Executing \"${test.name}\"")
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

            val finished = process.waitFor(60L, TimeUnit.SECONDS)

            if (!finished) {
                process.destroyForcibly()
            }

            stdoutThread.join()
            stderrThread.join()

            val stdoutLines = stdoutBuilder.toString().lines()
            val stderrLines = stderrBuilder.toString().lines()

            val result = TestResult(process.exitValue(), stdoutLines, stderrLines)
            testResults[test.name] = result

            if (process.exitValue() != 0)
                continue@testLoop

            for (pack in test.expectedStdoutOutput) {
                if (stdoutLines.windowed(pack.size).indexOf(pack) == -1)
                    continue@testLoop
            }

            for (pack in test.expectedStderrOutput) {
                if (stderrLines.windowed(pack.size).indexOf(pack) == -1)
                    continue@testLoop
            }

            result.success = true
        }

        val failedTests = testResults.filter { !it.value.success }

        val summary = buildString {
            appendLine("# \uD83E\uDDEA Butterscotch Test Results")
            appendLine("## \uD83D\uDCC4 Tests Summary")
            appendLine("| Test | Status |")
            appendLine("| - | - |")
            for ((name, result) in testResults) {
                appendLine("| $name | ${if (result.success) "✅" else "🚫"} |")
            }

            if (failedTests.isNotEmpty()) {
                appendLine("## \uD83D\uDE2D Failed Tests")
                for ((name, result) in failedTests) {
                    appendLine()
                    appendLine("<details><summary>🚫 <code>${name}</code></summary>")
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

    class TestResult(
        var exitCode: Int,
        var stdoutLines: List<String>,
        var stderrLines: List<String>
    ) {
        var success = false
    }
}

fun main(args: Array<String>) = Flowey().main(args)