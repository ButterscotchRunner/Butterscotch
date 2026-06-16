package net.perfectdreams.butterscotch.flowey

import kotlinx.serialization.Serializable

@Serializable
data class TestSuite(
    val tests: List<TestEntry>
) {
    @Serializable
    data class TestEntry(
        val name: String,
        val butterscotchArgs: List<String>,
        val expectedStdoutOutput: List<List<String>> = listOf(),
        val expectedStderrOutput: List<List<String>> = listOf()
    )
}