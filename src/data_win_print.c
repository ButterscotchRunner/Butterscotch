#include "data_win.h"

#include <stdio.h>
#include <string.h>

#include "utils.h"

void DataWin_printDebugSummary(DataWin* dataWin) {
    Log_logDebug("===== data.win Summary =====\n\n");

    // GEN8
    Gen8* g = &dataWin->gen8;
    Log_logDebug("-- GEN8 (General Info) --\n");
    Log_logDebug("  Game Name:        %s\n", g->name ? g->name : "(null)");
    Log_logDebug("  Display Name:     %s\n", g->displayName ? g->displayName : "(null)");
    Log_logDebug("  File Name:        %s\n", g->fileName ? g->fileName : "(null)");
    Log_logDebug("  Config:           %s\n", g->config ? g->config : "(null)");
    Log_logDebug("  WAD Version: %u\n", g->wadVersion);
    Log_logDebug("  Game ID:          %u\n", g->gameID);
    Log_logDebug("  Version:          %u.%u.%u.%u\n", g->major, g->minor, g->release, g->build);
    Log_logDebug("  Window Size:      %ux%u\n", g->defaultWindowWidth, g->defaultWindowHeight);
    Log_logDebug("  Steam App ID:     %d\n", g->steamAppID);
    Log_logDebug("  Room Order:       %u rooms\n", g->roomOrderCount);
    Log_logDebug("\n");

    // OPTN
    Log_logDebug("-- OPTN (Options) --\n");
    Log_logDebug("  Constants:        %u\n", dataWin->optn.constantCount);
    if (dataWin->optn.constantCount > 0) {
        uint32_t show = dataWin->optn.constantCount < 3 ? dataWin->optn.constantCount : 3;
        forEachIndexed(OptnConstant, constant, idx, dataWin->optn.constants, show) {
            Log_logDebug("    [%u] %s = %s\n", (unsigned int)idx, constant->name ? constant->name : "?", constant->value ? constant->value : "?");
        }
        if (dataWin->optn.constantCount > 3) Log_logDebug("    ... and %u more\n", dataWin->optn.constantCount - 3);
    }
    Log_logDebug("\n");

    // LANG
    Log_logDebug("-- LANG (Languages) --\n");
    Log_logDebug("  Languages:        %u\n", dataWin->lang.languageCount);
    Log_logDebug("  Entries:          %u\n", dataWin->lang.entryCount);
    Log_logDebug("\n");

    // EXTN
    Log_logDebug("-- EXTN (Extensions) --\n");
    Log_logDebug("  Extensions:       %u\n", dataWin->extn.count);
    forEachIndexed(Extension, ext, idx, dataWin->extn.extensions, dataWin->extn.count) {
        Log_logDebug("    [%u] %s (%u files)\n", (unsigned int)idx, ext->name ? ext->name : "?", ext->fileCount);
    }
    Log_logDebug("\n");

    // SOND
    Log_logDebug("-- SOND (Sounds) --\n");
    Log_logDebug("  Sounds:           %u\n", dataWin->sond.count);
    if (dataWin->sond.count > 0) {
        uint32_t show = dataWin->sond.count < 3 ? dataWin->sond.count : 3;
        forEachIndexed(Sound, snd, idx, dataWin->sond.sounds, show) {
            Log_logDebug("    [%u] %s (%s)\n", (unsigned int)idx, snd->name ? snd->name : "?", snd->type ? snd->type : "?");
        }
        if (dataWin->sond.count > 3) Log_logDebug("    ... and %u more\n", dataWin->sond.count - 3);
    }
    Log_logDebug("\n");

    // AGRP
    Log_logDebug("-- AGRP (Audio Groups) --\n");
    Log_logDebug("  Audio Groups:     %u\n", dataWin->agrp.count);
    forEachIndexed(AudioGroup, ag, idx, dataWin->agrp.audioGroups, dataWin->agrp.count) {
        Log_logDebug("    [%u] %s\n", (unsigned int)idx, ag->name ? ag->name : "?");
    }
    Log_logDebug("\n");

    // SPRT
    Log_logDebug("-- SPRT (Sprites) --\n");
    Log_logDebug("  Sprites:          %u\n", dataWin->sprt.count);
    if (dataWin->sprt.count > 0) {
        uint32_t show = dataWin->sprt.count < 3 ? dataWin->sprt.count : 3;
        forEachIndexed(Sprite, spr, idx, dataWin->sprt.sprites, show) {
            Log_logDebug("    [%u] %s (%ux%u, %u frames)\n", (unsigned int)idx, spr->name ? spr->name : "?", spr->width, spr->height, spr->textureCount);
        }
        if (dataWin->sprt.count > 3) Log_logDebug("    ... and %u more\n", dataWin->sprt.count - 3);
    }
    Log_logDebug("\n");

    // BGND
    Log_logDebug("-- BGND (Backgrounds) --\n");
    Log_logDebug("  Backgrounds:      %u\n", dataWin->bgnd.count);
    if (dataWin->bgnd.count > 0) {
        uint32_t show = dataWin->bgnd.count < 3 ? dataWin->bgnd.count : 3;
        forEachIndexed(Background, bg, idx, dataWin->bgnd.backgrounds, show) {
            Log_logDebug("    [%u] %s\n", (unsigned int)idx, bg->name ? bg->name : "?");
        }
        if (dataWin->bgnd.count > 3) Log_logDebug("    ... and %u more\n", dataWin->bgnd.count - 3);
    }
    Log_logDebug("\n");

    // PATH
    Log_logDebug("-- PATH (Paths) --\n");
    Log_logDebug("  Paths:            %u\n", dataWin->path.count);
    Log_logDebug("\n");

    // SCPT
    Log_logDebug("-- SCPT (Scripts) --\n");
    Log_logDebug("  Scripts:          %u\n", dataWin->scpt.count);
    if (dataWin->scpt.count > 0) {
        uint32_t show = dataWin->scpt.count < 3 ? dataWin->scpt.count : 3;
        forEachIndexed(Script, scr, idx, dataWin->scpt.scripts, show) {
            Log_logDebug("    [%u] %s -> code[%d]\n", (unsigned int)idx, scr->name ? scr->name : "?", scr->codeId);
        }
        if (dataWin->scpt.count > 3) Log_logDebug("    ... and %u more\n", dataWin->scpt.count - 3);
    }
    Log_logDebug("\n");

    // GLOB
    Log_logDebug("-- GLOB (Global Init Scripts) --\n");
    Log_logDebug("  Init Scripts:     %u\n", dataWin->glob.count);
    Log_logDebug("\n");

    // SHDR
    Log_logDebug("-- SHDR (Shaders) --\n");
    Log_logDebug("  Shaders:          %u\n", dataWin->shdr.count);
    forEachIndexed(Shader, shdr, idx, dataWin->shdr.shaders, dataWin->shdr.count) {
        Log_logDebug("    [%u] %s (version %d)\n", (unsigned int)idx, shdr->name ? shdr->name : "?", shdr->version);
    }
    Log_logDebug("\n");

    // FONT
    Log_logDebug("-- FONT (Fonts) --\n");
    Log_logDebug("  Fonts:            %u\n", dataWin->font.count);
    forEachIndexed(Font, fnt, idx, dataWin->font.fonts, dataWin->font.count) {
        Log_logDebug("    [%u] %s (%s, em=%u, %u glyphs)\n", (unsigned int)idx, fnt->name ? fnt->name : "?", fnt->displayName ? fnt->displayName : "?", fnt->emSize, fnt->glyphCount);
    }
    Log_logDebug("\n");

    // TMLN
    Log_logDebug("-- TMLN (Timelines) --\n");
    Log_logDebug("  Timelines:        %u\n", dataWin->tmln.count);
    Log_logDebug("\n");

    // OBJT
    Log_logDebug("-- OBJT (Game Objects) --\n");
    Log_logDebug("  Objects:          %u\n", dataWin->objt.count);
    if (dataWin->objt.count > 0) {
        uint32_t show = dataWin->objt.count < 3 ? dataWin->objt.count : 3;
        forEachIndexed(GameObject, obj, idx, dataWin->objt.objects, show) {
            uint32_t totalEvents = 0;
            repeat(OBJT_EVENT_TYPE_COUNT, e) {
                totalEvents += obj->eventLists[e].eventCount;
            }
            Log_logDebug("    [%u] %s (sprite=%d, depth=%d, %u events)\n", (unsigned int)idx, obj->name ? obj->name : "?", obj->spriteId, obj->depth, totalEvents);
        }
        if (dataWin->objt.count > 3) Log_logDebug("    ... and %u more\n", dataWin->objt.count - 3);
    }
    Log_logDebug("\n");

    // ROOM
    Log_logDebug("-- ROOM (Rooms) --\n");
    Log_logDebug("  Rooms:            %u\n", dataWin->room.count);
    if (dataWin->room.count > 0) {
        uint32_t show = dataWin->room.count < 3 ? dataWin->room.count : 3;
        forEachIndexed(Room, rm, idx, dataWin->room.rooms, show) {
            if (rm->payloadLoaded) {
                Log_logDebug("    [%u] %s (%ux%u, %u objects, %u tiles)\n", (unsigned int)idx, rm->name ? rm->name : "?", rm->width, rm->height, rm->gameObjectCount, rm->tileCount);
            } else {
                // Lazy room with payload not yet loaded: gameObjectCount/tileCount would be 0 and misleading.
                Log_logDebug("    [%u] %s (%ux%u, payload not loaded)\n", (unsigned int)idx, rm->name ? rm->name : "?", rm->width, rm->height);
            }
        }
        if (dataWin->room.count > 3) Log_logDebug("    ... and %u more\n", dataWin->room.count - 3);
    }
    Log_logDebug("\n");

    // TPAG
    Log_logDebug("-- TPAG (Texture Page Items) --\n");
    Log_logDebug("  Items:            %u\n", dataWin->tpag.count);
    Log_logDebug("\n");

    // CODE
    Log_logDebug("-- CODE (Code Entries) --\n");
    Log_logDebug("  Entries:          %u\n", dataWin->code.count);
    if (dataWin->code.count > 0) {
        uint32_t show = dataWin->code.count < 3 ? dataWin->code.count : 3;
        forEachIndexed(CodeEntry, entry, idx, dataWin->code.entries, show) {
            Log_logDebug("    [%u] %s (%u bytes, %u locals, %u args)\n", (unsigned int)idx, entry->name ? entry->name : "?", entry->length, entry->localsCount, entry->argumentsCount);
        }
        if (dataWin->code.count > 3) Log_logDebug("    ... and %u more\n", dataWin->code.count - 3);
    }
    Log_logDebug("\n");

    // VARI
    Log_logDebug("-- VARI (Variables) --\n");
    Log_logDebug("  Variables:        %u\n", dataWin->vari.variableCount);
    Log_logDebug("  Max Locals:       %u\n", dataWin->vari.maxLocalVarCount);
    if (dataWin->vari.variableCount > 0) {
        uint32_t show = dataWin->vari.variableCount < 3 ? dataWin->vari.variableCount : 3;
        forEachIndexed(Variable, var, idx, dataWin->vari.variables, show) {
            Log_logDebug("    [%u] %s (type=%d, id=%d, %u refs)\n", (unsigned int)idx, var->name ? var->name : "?", var->instanceType, var->varID, var->occurrences);
        }
        if (dataWin->vari.variableCount > 3) Log_logDebug("    ... and %u more\n", dataWin->vari.variableCount - 3);
    }
    Log_logDebug("\n");

    // FUNC
    Log_logDebug("-- FUNC (Functions) --\n");
    Log_logDebug("  Functions:        %u\n", dataWin->func.functionCount);
    Log_logDebug("  Code Locals:      %u\n", dataWin->func.codeLocalsCount);
    if (dataWin->func.functionCount > 0) {
        uint32_t show = dataWin->func.functionCount < 3 ? dataWin->func.functionCount : 3;
        forEachIndexed(Function, fn, idx, dataWin->func.functions, show) {
            Log_logDebug("    [%u] %s (%u refs)\n", (unsigned int)idx, fn->name ? fn->name : "?", fn->occurrences);
        }
        if (dataWin->func.functionCount > 3) Log_logDebug("    ... and %u more\n", dataWin->func.functionCount - 3);
    }
    Log_logDebug("\n");

    // STRG
    Log_logDebug("-- STRG (Strings) --\n");
    Log_logDebug("  Strings:          %u\n", dataWin->strg.count);
    if (dataWin->strg.count > 0) {
        uint32_t show = dataWin->strg.count < 5 ? dataWin->strg.count : 5;
        repeat(show, i) {
            const char* str = dataWin->strg.strings[i];
            // Truncate long strings for display
            if (str) {
                size_t len = strlen(str);
                if (len > 60) {
                    Log_logDebug("    [%u] \"%.60s...\" (%zu chars)\n", (unsigned int)i, str, len);
                } else {
                    Log_logDebug("    [%u] \"%s\"\n", (unsigned int)i, str);
                }
            } else {
                Log_logDebug("    [%u] (null)\n", (unsigned int)i);
            }
        }
        if (dataWin->strg.count > 5) Log_logDebug("    ... and %u more\n", dataWin->strg.count - 5);
    }
    Log_logDebug("\n");

    // TXTR
    Log_logDebug("-- TXTR (Textures) --\n");
    Log_logDebug("  Textures:         %u\n", dataWin->txtr.count);
    if (dataWin->txtr.count > 0) {
        forEachIndexed(Texture, tex, idx, dataWin->txtr.textures, dataWin->txtr.count) {
            Log_logDebug("    [%u] offset=0x%08X size=%u bytes\n", (unsigned int)idx, tex->blobOffset, tex->blobSize);
        }
    }
    Log_logDebug("\n");

    // AUDO
    Log_logDebug("-- AUDO (Audio) --\n");
    Log_logDebug("  Audio Entries:    %u\n", dataWin->audo.count);
    if (dataWin->audo.count > 0) {
        uint32_t show = dataWin->audo.count < 3 ? dataWin->audo.count : 3;
        forEachIndexed(AudioEntry, ae, idx, dataWin->audo.entries, show) {
            Log_logDebug("    [%u] offset=0x%08X size=%u bytes\n", (unsigned int)idx, ae->dataOffset, ae->dataSize);
        }
        if (dataWin->audo.count > 3) Log_logDebug("    ... and %u more\n", dataWin->audo.count - 3);
    }
    Log_logDebug("\n");

    Log_logDebug("-- Room Instances --\n");
    forEach(Room, room, dataWin->room.rooms, dataWin->room.count) {
        Log_logDebug("Room %s\n", room->name);

        if (!room->payloadLoaded) {
            Log_logDebug("  (payload not loaded)\n");
            continue;
        }

        forEachIndexed(RoomGameObject, roomGameObject, idx, room->gameObjects, room->gameObjectCount) {
            int32_t objectDefinitionId = roomGameObject->objectDefinition;
            GameObject* objectDefinition = &dataWin->objt.objects[objectDefinitionId];
            Log_logDebug("  Object %d (%s, x=%d, y=%d)\n", objectDefinitionId, objectDefinition->name, roomGameObject->x, roomGameObject->y);
        }
    }

    // Overall summary
    Log_logDebug("===== DataWin parse complete =====\n");
}
