#include "data_win.h"

#include <stdio.h>
#include <string.h>

#include "utils.h"

void DataWin_printDebugSummary(DataWin* dataWin) {
    Log_log("===== data.win Summary =====\n\n");

    // GEN8
    Gen8* g = &dataWin->gen8;
    Log_log("-- GEN8 (General Info) --\n");
    Log_log("  Game Name:        %s\n", g->name ? g->name : "(null)");
    Log_log("  Display Name:     %s\n", g->displayName ? g->displayName : "(null)");
    Log_log("  File Name:        %s\n", g->fileName ? g->fileName : "(null)");
    Log_log("  Config:           %s\n", g->config ? g->config : "(null)");
    Log_log("  WAD Version: %u\n", g->wadVersion);
    Log_log("  Game ID:          %u\n", g->gameID);
    Log_log("  Version:          %u.%u.%u.%u\n", g->major, g->minor, g->release, g->build);
    Log_log("  Window Size:      %ux%u\n", g->defaultWindowWidth, g->defaultWindowHeight);
    Log_log("  Steam App ID:     %d\n", g->steamAppID);
    Log_log("  Room Order:       %u rooms\n", g->roomOrderCount);
    Log_log("\n");

    // OPTN
    Log_log("-- OPTN (Options) --\n");
    Log_log("  Constants:        %u\n", dataWin->optn.constantCount);
    if (dataWin->optn.constantCount > 0) {
        uint32_t show = dataWin->optn.constantCount < 3 ? dataWin->optn.constantCount : 3;
        forEachIndexed(OptnConstant, constant, idx, dataWin->optn.constants, show) {
            Log_log("    [%u] %s = %s\n", (unsigned int)idx, constant->name ? constant->name : "?", constant->value ? constant->value : "?");
        }
        if (dataWin->optn.constantCount > 3) Log_log("    ... and %u more\n", dataWin->optn.constantCount - 3);
    }
    Log_log("\n");

    // LANG
    Log_log("-- LANG (Languages) --\n");
    Log_log("  Languages:        %u\n", dataWin->lang.languageCount);
    Log_log("  Entries:          %u\n", dataWin->lang.entryCount);
    Log_log("\n");

    // EXTN
    Log_log("-- EXTN (Extensions) --\n");
    Log_log("  Extensions:       %u\n", dataWin->extn.count);
    forEachIndexed(Extension, ext, idx, dataWin->extn.extensions, dataWin->extn.count) {
        Log_log("    [%u] %s (%u files)\n", (unsigned int)idx, ext->name ? ext->name : "?", ext->fileCount);
    }
    Log_log("\n");

    // SOND
    Log_log("-- SOND (Sounds) --\n");
    Log_log("  Sounds:           %u\n", dataWin->sond.count);
    if (dataWin->sond.count > 0) {
        uint32_t show = dataWin->sond.count < 3 ? dataWin->sond.count : 3;
        forEachIndexed(Sound, snd, idx, dataWin->sond.sounds, show) {
            Log_log("    [%u] %s (%s)\n", (unsigned int)idx, snd->name ? snd->name : "?", snd->type ? snd->type : "?");
        }
        if (dataWin->sond.count > 3) Log_log("    ... and %u more\n", dataWin->sond.count - 3);
    }
    Log_log("\n");

    // AGRP
    Log_log("-- AGRP (Audio Groups) --\n");
    Log_log("  Audio Groups:     %u\n", dataWin->agrp.count);
    forEachIndexed(AudioGroup, ag, idx, dataWin->agrp.audioGroups, dataWin->agrp.count) {
        Log_log("    [%u] %s\n", (unsigned int)idx, ag->name ? ag->name : "?");
    }
    Log_log("\n");

    // SPRT
    Log_log("-- SPRT (Sprites) --\n");
    Log_log("  Sprites:          %u\n", dataWin->sprt.count);
    if (dataWin->sprt.count > 0) {
        uint32_t show = dataWin->sprt.count < 3 ? dataWin->sprt.count : 3;
        forEachIndexed(Sprite, spr, idx, dataWin->sprt.sprites, show) {
            Log_log("    [%u] %s (%ux%u, %u frames)\n", (unsigned int)idx, spr->name ? spr->name : "?", spr->width, spr->height, spr->textureCount);
        }
        if (dataWin->sprt.count > 3) Log_log("    ... and %u more\n", dataWin->sprt.count - 3);
    }
    Log_log("\n");

    // BGND
    Log_log("-- BGND (Backgrounds) --\n");
    Log_log("  Backgrounds:      %u\n", dataWin->bgnd.count);
    if (dataWin->bgnd.count > 0) {
        uint32_t show = dataWin->bgnd.count < 3 ? dataWin->bgnd.count : 3;
        forEachIndexed(Background, bg, idx, dataWin->bgnd.backgrounds, show) {
            Log_log("    [%u] %s\n", (unsigned int)idx, bg->name ? bg->name : "?");
        }
        if (dataWin->bgnd.count > 3) Log_log("    ... and %u more\n", dataWin->bgnd.count - 3);
    }
    Log_log("\n");

    // PATH
    Log_log("-- PATH (Paths) --\n");
    Log_log("  Paths:            %u\n", dataWin->path.count);
    Log_log("\n");

    // SCPT
    Log_log("-- SCPT (Scripts) --\n");
    Log_log("  Scripts:          %u\n", dataWin->scpt.count);
    if (dataWin->scpt.count > 0) {
        uint32_t show = dataWin->scpt.count < 3 ? dataWin->scpt.count : 3;
        forEachIndexed(Script, scr, idx, dataWin->scpt.scripts, show) {
            Log_log("    [%u] %s -> code[%d]\n", (unsigned int)idx, scr->name ? scr->name : "?", scr->codeId);
        }
        if (dataWin->scpt.count > 3) Log_log("    ... and %u more\n", dataWin->scpt.count - 3);
    }
    Log_log("\n");

    // GLOB
    Log_log("-- GLOB (Global Init Scripts) --\n");
    Log_log("  Init Scripts:     %u\n", dataWin->glob.count);
    Log_log("\n");

    // SHDR
    Log_log("-- SHDR (Shaders) --\n");
    Log_log("  Shaders:          %u\n", dataWin->shdr.count);
    forEachIndexed(Shader, shdr, idx, dataWin->shdr.shaders, dataWin->shdr.count) {
        Log_log("    [%u] %s (version %d)\n", (unsigned int)idx, shdr->name ? shdr->name : "?", shdr->version);
    }
    Log_log("\n");

    // FONT
    Log_log("-- FONT (Fonts) --\n");
    Log_log("  Fonts:            %u\n", dataWin->font.count);
    forEachIndexed(Font, fnt, idx, dataWin->font.fonts, dataWin->font.count) {
        Log_log("    [%u] %s (%s, em=%u, %u glyphs)\n", (unsigned int)idx, fnt->name ? fnt->name : "?", fnt->displayName ? fnt->displayName : "?", fnt->emSize, fnt->glyphCount);
    }
    Log_log("\n");

    // TMLN
    Log_log("-- TMLN (Timelines) --\n");
    Log_log("  Timelines:        %u\n", dataWin->tmln.count);
    Log_log("\n");

    // OBJT
    Log_log("-- OBJT (Game Objects) --\n");
    Log_log("  Objects:          %u\n", dataWin->objt.count);
    if (dataWin->objt.count > 0) {
        uint32_t show = dataWin->objt.count < 3 ? dataWin->objt.count : 3;
        forEachIndexed(GameObject, obj, idx, dataWin->objt.objects, show) {
            uint32_t totalEvents = 0;
            repeat(OBJT_EVENT_TYPE_COUNT, e) {
                totalEvents += obj->eventLists[e].eventCount;
            }
            Log_log("    [%u] %s (sprite=%d, depth=%d, %u events)\n", (unsigned int)idx, obj->name ? obj->name : "?", obj->spriteId, obj->depth, totalEvents);
        }
        if (dataWin->objt.count > 3) Log_log("    ... and %u more\n", dataWin->objt.count - 3);
    }
    Log_log("\n");

    // ROOM
    Log_log("-- ROOM (Rooms) --\n");
    Log_log("  Rooms:            %u\n", dataWin->room.count);
    if (dataWin->room.count > 0) {
        uint32_t show = dataWin->room.count < 3 ? dataWin->room.count : 3;
        forEachIndexed(Room, rm, idx, dataWin->room.rooms, show) {
            if (rm->payloadLoaded) {
                Log_log("    [%u] %s (%ux%u, %u objects, %u tiles)\n", (unsigned int)idx, rm->name ? rm->name : "?", rm->width, rm->height, rm->gameObjectCount, rm->tileCount);
            } else {
                // Lazy room with payload not yet loaded: gameObjectCount/tileCount would be 0 and misleading.
                Log_log("    [%u] %s (%ux%u, payload not loaded)\n", (unsigned int)idx, rm->name ? rm->name : "?", rm->width, rm->height);
            }
        }
        if (dataWin->room.count > 3) Log_log("    ... and %u more\n", dataWin->room.count - 3);
    }
    Log_log("\n");

    // TPAG
    Log_log("-- TPAG (Texture Page Items) --\n");
    Log_log("  Items:            %u\n", dataWin->tpag.count);
    Log_log("\n");

    // CODE
    Log_log("-- CODE (Code Entries) --\n");
    Log_log("  Entries:          %u\n", dataWin->code.count);
    if (dataWin->code.count > 0) {
        uint32_t show = dataWin->code.count < 3 ? dataWin->code.count : 3;
        forEachIndexed(CodeEntry, entry, idx, dataWin->code.entries, show) {
            Log_log("    [%u] %s (%u bytes, %u locals, %u args)\n", (unsigned int)idx, entry->name ? entry->name : "?", entry->length, entry->localsCount, entry->argumentsCount);
        }
        if (dataWin->code.count > 3) Log_log("    ... and %u more\n", dataWin->code.count - 3);
    }
    Log_log("\n");

    // VARI
    Log_log("-- VARI (Variables) --\n");
    Log_log("  Variables:        %u\n", dataWin->vari.variableCount);
    Log_log("  Max Locals:       %u\n", dataWin->vari.maxLocalVarCount);
    if (dataWin->vari.variableCount > 0) {
        uint32_t show = dataWin->vari.variableCount < 3 ? dataWin->vari.variableCount : 3;
        forEachIndexed(Variable, var, idx, dataWin->vari.variables, show) {
            Log_log("    [%u] %s (type=%d, id=%d, %u refs)\n", (unsigned int)idx, var->name ? var->name : "?", var->instanceType, var->varID, var->occurrences);
        }
        if (dataWin->vari.variableCount > 3) Log_log("    ... and %u more\n", dataWin->vari.variableCount - 3);
    }
    Log_log("\n");

    // FUNC
    Log_log("-- FUNC (Functions) --\n");
    Log_log("  Functions:        %u\n", dataWin->func.functionCount);
    Log_log("  Code Locals:      %u\n", dataWin->func.codeLocalsCount);
    if (dataWin->func.functionCount > 0) {
        uint32_t show = dataWin->func.functionCount < 3 ? dataWin->func.functionCount : 3;
        forEachIndexed(Function, fn, idx, dataWin->func.functions, show) {
            Log_log("    [%u] %s (%u refs)\n", (unsigned int)idx, fn->name ? fn->name : "?", fn->occurrences);
        }
        if (dataWin->func.functionCount > 3) Log_log("    ... and %u more\n", dataWin->func.functionCount - 3);
    }
    Log_log("\n");

    // STRG
    Log_log("-- STRG (Strings) --\n");
    Log_log("  Strings:          %u\n", dataWin->strg.count);
    if (dataWin->strg.count > 0) {
        uint32_t show = dataWin->strg.count < 5 ? dataWin->strg.count : 5;
        repeat(show, i) {
            const char* str = dataWin->strg.strings[i];
            // Truncate long strings for display
            if (str) {
                size_t len = strlen(str);
                if (len > 60) {
                    Log_log("    [%u] \"%.60s...\" (%zu chars)\n", (unsigned int)i, str, len);
                } else {
                    Log_log("    [%u] \"%s\"\n", (unsigned int)i, str);
                }
            } else {
                Log_log("    [%u] (null)\n", (unsigned int)i);
            }
        }
        if (dataWin->strg.count > 5) Log_log("    ... and %u more\n", dataWin->strg.count - 5);
    }
    Log_log("\n");

    // TXTR
    Log_log("-- TXTR (Textures) --\n");
    Log_log("  Textures:         %u\n", dataWin->txtr.count);
    if (dataWin->txtr.count > 0) {
        forEachIndexed(Texture, tex, idx, dataWin->txtr.textures, dataWin->txtr.count) {
            Log_log("    [%u] offset=0x%08X size=%u bytes\n", (unsigned int)idx, tex->blobOffset, tex->blobSize);
        }
    }
    Log_log("\n");

    // AUDO
    Log_log("-- AUDO (Audio) --\n");
    Log_log("  Audio Entries:    %u\n", dataWin->audo.count);
    if (dataWin->audo.count > 0) {
        uint32_t show = dataWin->audo.count < 3 ? dataWin->audo.count : 3;
        forEachIndexed(AudioEntry, ae, idx, dataWin->audo.entries, show) {
            Log_log("    [%u] offset=0x%08X size=%u bytes\n", (unsigned int)idx, ae->dataOffset, ae->dataSize);
        }
        if (dataWin->audo.count > 3) Log_log("    ... and %u more\n", dataWin->audo.count - 3);
    }
    Log_log("\n");

    Log_log("-- Room Instances --\n");
    forEach(Room, room, dataWin->room.rooms, dataWin->room.count) {
        Log_log("Room %s\n", room->name);

        if (!room->payloadLoaded) {
            Log_log("  (payload not loaded)\n");
            continue;
        }

        forEachIndexed(RoomGameObject, roomGameObject, idx, room->gameObjects, room->gameObjectCount) {
            int32_t objectDefinitionId = roomGameObject->objectDefinition;
            GameObject* objectDefinition = &dataWin->objt.objects[objectDefinitionId];
            Log_log("  Object %d (%s, x=%d, y=%d)\n", objectDefinitionId, objectDefinition->name, roomGameObject->x, roomGameObject->y);
        }
    }

    // Overall summary
    Log_log("===== DataWin parse complete =====\n");
}
