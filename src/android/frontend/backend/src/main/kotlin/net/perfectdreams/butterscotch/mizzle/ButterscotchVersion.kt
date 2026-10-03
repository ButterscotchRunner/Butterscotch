package net.perfectdreams.butterscotch.mizzle

data class ButterscotchVersion(
    val year: Int,
    val month: Int,
    val day: Int,
    val build: Int
) : Comparable<ButterscotchVersion> {
    companion object {
        private val BUTTERSCOTCH_VERSION_REGEX = Regex("([0-9]+)\\.([0-9]+)\\.([0-9]+)-([0-9]+)")

        fun parse(text: String): ButterscotchVersion {
            val match = BUTTERSCOTCH_VERSION_REGEX.matchEntire(text) ?: error("Invalid version $text!")

            return ButterscotchVersion(
                match.groupValues[1].toInt(),
                match.groupValues[2].toInt(),
                match.groupValues[3].toInt(),
                match.groupValues[4].toInt(),
            )
        }
    }

    fun prettify(): String {
        return "${year.toString().padStart(4, '0')}.${month.toString().padStart(2, '0')}.${day.toString().padStart(2, '0')}-${build}"
    }

    override fun compareTo(other: ButterscotchVersion): Int {
        if (year != other.year) return year - other.year
        if (month != other.month) return month - other.month
        if (day != other.day) return day - other.day
        return build - other.build
    }
}