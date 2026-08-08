package net.perfectdreams.butterscotch.android

import android.content.Intent
import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.core.splashscreen.SplashScreen.Companion.installSplashScreen
import com.google.android.play.core.appupdate.AppUpdateManager
import com.google.android.play.core.appupdate.AppUpdateManagerFactory
import com.google.android.play.core.appupdate.AppUpdateOptions
import com.google.android.play.core.appupdate.testing.FakeAppUpdateManager
import com.google.android.play.core.install.model.AppUpdateType
import com.google.android.play.core.install.model.UpdateAvailability
import net.perfectdreams.butterscotch.android.theme.ButterscotchAndroidTheme
import java.util.UUID

class MainActivity : ComponentActivity() {
    lateinit var appUpdateManager: AppUpdateManager
    val updateLauncher = registerForActivityResult(
        ActivityResultContracts.StartIntentSenderForResult()
    ) { result ->
        if (result.resultCode != RESULT_OK) {
            Toast.makeText(this.applicationContext, "You cancelled the update :(", Toast.LENGTH_SHORT).show()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        val splashScreen = installSplashScreen()
        super.onCreate(savedInstanceState)

        val gameLibrary = Libraries.loadGameLibrary(this.applicationContext)
        val layoutLibrary = Libraries.loadLayoutLibrary(this.applicationContext)
        val settingsStore = Libraries.loadSettingsStore(this.applicationContext)

        ButterscotchUtils.fireAppLaunchEvent(true)

        if (intent?.action == ACTION_LAUNCH_GAME) {
            val gameIdAsString = intent.getStringExtra(GameActivity.EXTRA_GAME_ID)
            // Clear so a config change / recreate doesn't re-trigger the forward.
            intent.action = null
            intent.removeExtra(GameActivity.EXTRA_GAME_ID)
            if (gameIdAsString != null) {
                val gameId = UUID.fromString(gameIdAsString)

                if (gameLibrary.findById(gameId) != null) {
                    startActivity(Intent(this, GameActivity::class.java).apply {
                        putExtra(GameActivity.EXTRA_GAME_ID, gameId.toString())
                    })
                    finish()
                }
            }
            // Unknown/stale id: fall through to the normal launcher UI.
        }

        appUpdateManager = AppUpdateManagerFactory.create(this.applicationContext)
        // appUpdateManager = FakeAppUpdateManager(this.applicationContext)
        // appUpdateManager.setUpdateAvailable(100000, AppUpdateType.IMMEDIATE)

        var updateAvailableClickCallback by mutableStateOf<(() -> (Unit))?>(null)

        appUpdateManager.appUpdateInfo.addOnSuccessListener { appUpdateInfo ->
            if (appUpdateInfo.updateAvailability() == UpdateAvailability.UPDATE_AVAILABLE && appUpdateInfo.isUpdateTypeAllowed(AppUpdateType.IMMEDIATE)) {
                updateAvailableClickCallback = {
                    // Re-refresh the update
                    appUpdateManager.appUpdateInfo
                        .addOnSuccessListener { freshInfo ->
                            if (freshInfo.updateAvailability() == UpdateAvailability.UPDATE_AVAILABLE && freshInfo.isUpdateTypeAllowed(AppUpdateType.IMMEDIATE)) {
                                appUpdateManager.startUpdateFlowForResult(
                                    freshInfo,
                                    updateLauncher,
                                    AppUpdateOptions.newBuilder(AppUpdateType.IMMEDIATE).build()
                                )
                            } else {
                                Toast.makeText(this.applicationContext, "Whoops, looks like there isn't an update available for you anymore :(", Toast.LENGTH_SHORT).show()
                            }
                        }
                        .addOnFailureListener {
                            Toast.makeText(this.applicationContext, "Something went wrong when trying to start the update :(", Toast.LENGTH_SHORT).show()
                        }
                }
            }
        }

        enableEdgeToEdge(
            // Make the button background not LIGHT
            navigationBarStyle = SystemBarStyle.dark(
                scrim = android.graphics.Color.TRANSPARENT
            )
        )

        setContent {
            ButterscotchAndroidTheme {
                var splashGone by rememberSaveable { mutableStateOf(false) }
                Box(Modifier.fillMaxSize()) {
                    ButterscotchApp(gameLibrary, layoutLibrary, settingsStore, updateAvailableClickCallback)
                    if (!splashGone) {
                        SplashReveal(onFinished = { splashGone = true })
                    }
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        // Resume an immediate update that was interrupted while the app was backgrounded
        appUpdateManager.appUpdateInfo.addOnSuccessListener { appUpdateInfo ->
            if (appUpdateInfo.updateAvailability() == UpdateAvailability.DEVELOPER_TRIGGERED_UPDATE_IN_PROGRESS) {
                appUpdateManager.startUpdateFlowForResult(appUpdateInfo, updateLauncher, AppUpdateOptions.newBuilder(AppUpdateType.IMMEDIATE).build())
            }
        }
    }

    companion object {
        const val ACTION_LAUNCH_GAME = "net.perfectdreams.butterscotch.android.action.LAUNCH_GAME"
    }
}
