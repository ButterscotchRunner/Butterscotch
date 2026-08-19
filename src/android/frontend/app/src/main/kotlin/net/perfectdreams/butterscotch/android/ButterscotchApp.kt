package net.perfectdreams.butterscotch.android

import androidx.compose.animation.AnimatedContentTransitionScope.SlideDirection
import androidx.compose.animation.core.tween
import androidx.compose.runtime.Composable
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import androidx.navigation.toRoute
import net.perfectdreams.butterscotch.android.layouts.LayoutLibrary
import net.perfectdreams.butterscotch.android.library.GameLibrary
import net.perfectdreams.butterscotch.android.settings.SettingsStore
import net.perfectdreams.butterscotch.android.screens.AboutScreen
import net.perfectdreams.butterscotch.android.screens.GameLogsScreen
import net.perfectdreams.butterscotch.android.screens.GameSettingsScreen
import net.perfectdreams.butterscotch.android.screens.GeneralSettingsScreen
import net.perfectdreams.butterscotch.android.screens.ImportScreen
import net.perfectdreams.butterscotch.android.screens.LauncherScreen
import net.perfectdreams.butterscotch.android.screens.LayoutManagerScreen
import net.perfectdreams.butterscotch.android.screens.LicensesScreen
import net.perfectdreams.butterscotch.android.screens.SaveSlotDetailScreen
import net.perfectdreams.butterscotch.android.screens.SaveSlotListScreen
import net.perfectdreams.butterscotch.android.screens.SettingsScreen
import java.util.UUID

/**
 * The entry point of the app!
 */
@Composable
fun ButterscotchApp(gameLibrary: GameLibrary, layoutLibrary: LayoutLibrary, settingsStore: SettingsStore, updateAvailableClickCallback: (() -> (Unit))?) {
    val nav = rememberNavController()

    NavHost(
        navController = nav,
        startDestination = Route.Launcher,
        // Changes the default fade to a horizontal right -> left slide animation
        enterTransition = { slideIntoContainer(SlideDirection.Start, tween(250)) },
        exitTransition = { slideOutOfContainer(SlideDirection.Start, tween(250)) },
        popEnterTransition = { slideIntoContainer(SlideDirection.End, tween(250)) },
        popExitTransition = { slideOutOfContainer(SlideDirection.End, tween(250)) },
    ) {
        composable<Route.Launcher> {
            LauncherScreen(library = gameLibrary, nav = nav, updateAvailableClickCallback = updateAvailableClickCallback)
        }
        composable<Route.ImportGame> {
            ImportScreen(library = gameLibrary, layoutLibrary = layoutLibrary, nav = nav)
        }
        composable<Route.About> {
            AboutScreen(nav = nav)
        }
        composable<Route.Licenses> {
            LicensesScreen(nav = nav)
        }
        composable<Route.GeneralSettings> {
            GeneralSettingsScreen(gameLibrary = gameLibrary, layoutLibrary = layoutLibrary, settingsStore = settingsStore, nav = nav)
        }
        composable<Route.LayoutManager> {
            LayoutManagerScreen(gameLibrary = gameLibrary, layoutLibrary = layoutLibrary, nav = nav)
        }
        composable<Route.GameOverview> { backStackEntry ->
            val args = backStackEntry.toRoute<Route.GameOverview>()
            SettingsScreen(
                library = gameLibrary,
                gameId = UUID.fromString(args.gameId),
                nav = nav
            )
        }
        composable<Route.GameSettings> { backStackEntry ->
            val args = backStackEntry.toRoute<Route.GameSettings>()
            GameSettingsScreen(
                gameLibrary = gameLibrary,
                layoutLibrary = layoutLibrary,
                gameId = UUID.fromString(args.gameId),
                nav = nav,
            )
        }
        composable<Route.SaveSlotList> { backStackEntry ->
            val args = backStackEntry.toRoute<Route.SaveSlotList>()
            SaveSlotListScreen(
                library = gameLibrary,
                gameId = UUID.fromString(args.gameId),
                nav = nav,
            )
        }
        composable<Route.SaveSlotDetail> { backStackEntry ->
            val args = backStackEntry.toRoute<Route.SaveSlotDetail>()
            SaveSlotDetailScreen(
                library = gameLibrary,
                gameId = UUID.fromString(args.gameId),
                slotId = args.slotId,
                nav = nav,
            )
        }
        composable<Route.GameLogs> { backStackEntry ->
            val args = backStackEntry.toRoute<Route.SaveSlotList>()
            GameLogsScreen(
                library = gameLibrary,
                gameId = UUID.fromString(args.gameId),
                nav = nav,
            )
        }
    }
}
