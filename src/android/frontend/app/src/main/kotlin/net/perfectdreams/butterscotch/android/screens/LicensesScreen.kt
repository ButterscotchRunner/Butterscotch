package net.perfectdreams.butterscotch.android.screens

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.navigation.NavHostController
import net.perfectdreams.butterscotch.android.R
import net.perfectdreams.butterscotch.android.components.ButterscotchBackButton
import net.perfectdreams.butterscotch.android.components.ButterscotchTopBar

// Keep this in sync when a dependency is added or removed!
private enum class LicenseDoc(val displayName: String, val rawResId: Int) {
    MPL_2_0("Mozilla Public License 2.0", R.raw.license_mpl_2_0),
    APACHE_2_0("Apache License 2.0", R.raw.license_apache_2_0),
    MIT("MIT License", R.raw.license_mit),
    BZIP2("bzip2 License", R.raw.license_bzip2),
    UNLICENSE("Unlicense", R.raw.license_unlicense),
    SHA1("Public Domain", R.raw.license_sha1),
    BASE64("Public Domain", R.raw.license_base64),
    RSA_MD5("RSA Data Security MD5 License", R.raw.license_rsa_md5),
}

private data class LicenseEntry(val name: String, val doc: LicenseDoc)

private val LICENSES = listOf(
    LicenseEntry("Butterscotch", LicenseDoc.MPL_2_0),
    LicenseEntry("GameMaker-HTML5", LicenseDoc.APACHE_2_0),
    LicenseEntry("miniaudio", LicenseDoc.UNLICENSE),
    LicenseEntry("stb", LicenseDoc.MIT),
    LicenseEntry("bzip2 / libbzip2", LicenseDoc.BZIP2),
    LicenseEntry("md5", LicenseDoc.RSA_MD5),
    LicenseEntry("sha1", LicenseDoc.SHA1),
    LicenseEntry("base64", LicenseDoc.BASE64),
    LicenseEntry("AndroidX, Jetpack Compose & Material 3", LicenseDoc.APACHE_2_0),
    LicenseEntry("Kotlin, kotlinx.serialization & kotlinx.coroutines", LicenseDoc.APACHE_2_0),
)

@Composable
fun LicensesScreen(nav: NavHostController) {
    var selected by remember { mutableStateOf<LicenseEntry?>(null) }

    Scaffold(
        topBar = { ButterscotchTopBar({ Text("Licenses") }, nav, navigationIcon = { ButterscotchBackButton(nav) }) },
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .verticalScroll(rememberScrollState()),
        ) {
            LICENSES.forEach { entry ->
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable { selected = entry }
                        .padding(horizontal = 24.dp, vertical = 12.dp),
                ) {
                    Text(entry.name, style = MaterialTheme.typography.bodyLarge)
                    Text(entry.doc.displayName, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
            }
        }
    }

    selected?.let { entry ->
        LicenseDialog(entry, onDismiss = { selected = null })
    }
}

@Composable
private fun LicenseDialog(entry: LicenseEntry, onDismiss: () -> Unit) {
    val resources = LocalResources.current
    val licenseText = remember(entry.doc) {
        resources.openRawResource(entry.doc.rawResId).bufferedReader().use { it.readText() }
    }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(entry.name) },
        text = {
            Column(modifier = Modifier.heightIn(max = 400.dp).verticalScroll(rememberScrollState())) {
                Text(licenseText, style = MaterialTheme.typography.bodySmall, fontFamily = FontFamily.Monospace)
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) { Text("Close") }
        },
    )
}
