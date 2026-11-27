#include "discord_presence.h"

#ifdef ENABLE_DISCORD_RPC

#include "discord_rpc.h"
#include "gettime.h"
#include "utils.h"
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

typedef struct {
#ifdef _WIN32
    HMODULE library;
#else
    void* library;
#endif
    void (*initialize)(const char*, DiscordEventHandlers*, int, const char*);
    void (*shutdown)(void);
    void (*runCallbacks)(void);
    void (*updatePresence)(const DiscordRichPresence*);
    void (*clearPresence)(void);
    bool initialized;
    uint64_t nextUpdate;
    char gameName[128];
    DiscordRichPresence presence;
} DiscordPresenceState;

static DiscordPresenceState discord;

static void* findSymbol(const char* name) {
#ifdef _WIN32
    return (void*)GetProcAddress(discord.library, name);
#else
    return dlsym(discord.library, name);
#endif
}

static void onReady(MAYBE_UNUSED const DiscordUser* user) {
    discord.updatePresence(&discord.presence);
}

void DiscordPresence_stop(void) {
    if (discord.initialized) {
        discord.clearPresence();
        discord.shutdown();
    }
    if (discord.library != nullptr) {
#ifdef _WIN32
        FreeLibrary(discord.library);
#else
        dlclose(discord.library);
#endif
    }
    ZERO_STRUCT(discord);
}

void DiscordPresence_start(const char* gameName) {
    DiscordPresence_stop();
#ifdef _WIN32
    discord.library = LoadLibraryA("discord-rpc.dll");
#elif defined(__APPLE__)
    discord.library = dlopen("@executable_path/libdiscord-rpc.dylib", RTLD_NOW | RTLD_LOCAL);
#else
    discord.library = dlopen("libdiscord-rpc.so", RTLD_NOW | RTLD_LOCAL);
#endif
    if (discord.library == nullptr) {
        logWarn("Discord RPC library unavailable; continuing without Rich Presence\n");
        return;
    }

    discord.initialize = (void (*)(const char*, DiscordEventHandlers*, int, const char*))findSymbol("Discord_Initialize");
    discord.shutdown = (void (*)(void))findSymbol("Discord_Shutdown");
    discord.runCallbacks = (void (*)(void))findSymbol("Discord_RunCallbacks");
    discord.updatePresence = (void (*)(const DiscordRichPresence*))findSymbol("Discord_UpdatePresence");
    discord.clearPresence = (void (*)(void))findSymbol("Discord_ClearPresence");
    if (discord.initialize == nullptr || discord.shutdown == nullptr || discord.runCallbacks == nullptr ||
        discord.updatePresence == nullptr || discord.clearPresence == nullptr) {
        logWarn("Discord RPC library is incompatible; continuing without Rich Presence\n");
        DiscordPresence_stop();
        return;
    }

    if (gameName == nullptr || gameName[0] == '\0') gameName = "GameMaker game";
    size_t length = strlen(gameName);
    if (length >= sizeof(discord.gameName)) {
        length = sizeof(discord.gameName) - 1;
        while (length > 0 && ((unsigned char)gameName[length] & 0xC0) == 0x80) length--;
    }
    memcpy(discord.gameName, gameName, length);
    discord.gameName[length] = '\0';
    discord.presence.details = discord.gameName;
    discord.presence.startTimestamp = (int64_t)time(nullptr);
    discord.presence.largeImageKey = "butterscotch";
    discord.presence.largeImageText = "Butterscotch";

    DiscordEventHandlers handlers = {0};
    handlers.ready = onReady;
    discord.initialize("1566648448462360587", &handlers, 0, nullptr);
    discord.initialized = true;
}

void DiscordPresence_update(void) {
    if (!discord.initialized) return;
    uint64_t now = nowNanos();
    if (now < discord.nextUpdate) return;
    discord.nextUpdate = now + 250000000;
    discord.runCallbacks();
}

#endif
