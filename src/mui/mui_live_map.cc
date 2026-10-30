#include "mui/mui_pages.h"

#include <math.h>
#include <stdio.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "game/critter.h"
#include "game/item.h"
#include "game/map.h"
#include "game/object.h"
#include "game/party.h"
#include "game/proto.h"
#include "game/proto_types.h"
#include "game/scripts.h"
#include "game/tile.h"
#include "mobile_contracts.h"
#include "mui/mui_gameskin.h"
#include "mui/mui_screens.h"
#include "mui/mui_skin.h"

namespace fallout {

// The live map: the explored-or-not floor plan is drawn from the objects the
// game holds on dude's elevation (walls as joined lines, doors, items,
// containers, stairs, exits, critters), in the same hex geometry the top
// screen shows. Read-only; a tap on open ground sends a Walk command.

namespace {

    enum Kind {
        KindDoor,
        KindContainer,
        KindItem,
        KindLevel,
        KindExit,
        KindCritter,
        KindParty,
        KindCorpse,
    };

    struct Mark {
        float x;
        float y;
        Kind kind;
        int rotation;
    };

    struct Label {
        float x;
        float y;
        std::u32string text;
    };

    struct Line {
        float x1, y1, x2, y2;
    };

    struct World {
        std::vector<Line> walls;
        std::vector<Mark> marks;
        std::vector<Label> labels;
    };

    World gWorld;
    int gKeyMap = -2;
    int gKeyElevation = -2;
    unsigned int gBuilt = 0;

    MuiPanZoom gView;
    bool gViewReady = false;
    unsigned int gTouched = 0;
    bool gLegend = false;
    int gDestination = -1;
    unsigned int gNoticeUntil = 0;
    constexpr unsigned int kRefreshMs = 500;
    constexpr unsigned int kHoldMs = 4000;

    // Tile to a flat plane in the game's hex layout (as `tile_coord` with the
    // view at the origin, so it doesn't change when the top screen scrolls).
    void tileXY(int tile, float* x, float* y)
    {
        int gx = HEX_GRID_WIDTH - 1 - tile % HEX_GRID_WIDTH;
        int gy = tile / HEX_GRID_WIDTH;
        *x = static_cast<float>(48 * (gx / 2) + ((gx & 1) ? 32 : 0) + 16 * gy);
        *y = static_cast<float>(-12 * (gx / 2) + 12 * gy);
    }

    std::u32string u(const char* text)
    {
        return muiDecodeUtf8(text);
    }

    struct Palette {
        MuiColor wall, door, container, item, level, exit, critter, party, corpse, dude;
    };

    Palette palette()
    {
        if (muiSkinPipboy()) {
            return { muiRgb(0x39FF14), muiRgb(0xBFFF00), muiRgb(0x7FFF00), muiRgb(0x1F8C0D), muiRgb(0xB8FF3C), muiRgb(0xE8FF99), muiRgb(0xFF7A3C), muiRgb(0x39FF14), muiRgb(0x1F8C0D), muiRgb(0xFFFFFF) };
        }
        return { muiRgb(0x56C85A), muiRgb(0xD8B060), muiRgb(0xE0C060), muiRgb(0x6AB0F0), muiRgb(0xFFD060), muiRgb(0xFFB000), muiRgb(0xFF6A4A), muiRgb(0x60FF60), muiRgb(0x8C704A), muiRgb(0xFFFFFF) };
    }

    void rebuild(unsigned int now)
    {
        gWorld = World();
        gBuilt = now != 0 ? now : 1;
        gKeyMap = map_get_index_number();
        gKeyElevation = map_elevation;
        if (obj_dude == nullptr) {
            return;
        }

        std::unordered_set<int> walls;
        std::unordered_map<int, bool> exitsSeen;
        for (Object* object = obj_find_first_at(map_elevation); object != nullptr; object = obj_find_next_at()) {
            if (object == obj_dude || (object->flags & OBJECT_HIDDEN) != 0 || object->tile < 0) {
                continue;
            }
            float x, y;
            tileXY(object->tile, &x, &y);
            switch (FID_TYPE(object->fid)) {
            case OBJ_TYPE_WALL:
                walls.insert(object->tile);
                break;
            case OBJ_TYPE_SCENERY: {
                Proto* proto;
                if (proto_ptr(object->pid, &proto) != -1) {
                    switch (proto->scenery.type) {
                    case SCENERY_TYPE_DOOR:
                        gWorld.marks.push_back({ x, y, KindDoor, 0 });
                        break;
                    case SCENERY_TYPE_STAIRS:
                    case SCENERY_TYPE_ELEVATOR:
                    case SCENERY_TYPE_LADDER_UP:
                    case SCENERY_TYPE_LADDER_DOWN:
                        gWorld.marks.push_back({ x, y, KindLevel, 0 });
                        break;
                    default:
                        break;
                    }
                }
                break;
            }
            case OBJ_TYPE_MISC:
                if (object->pid >= PROTO_ID_0x5000010 && object->pid <= PROTO_ID_0x5000017) {
                    gWorld.marks.push_back({ x, y, KindExit, 0 });
                    int destination = object->data.misc.map;
                    if (destination > 0 && !exitsSeen[destination]) {
                        exitsSeen[destination] = true;
                        const char* name = map_get_short_name(destination);
                        if (name != nullptr && name[0] != '\0') {
                            gWorld.labels.push_back({ x, y, muiDecodeGameText(name) });
                        }
                    }
                }
                break;
            case OBJ_TYPE_ITEM:
                gWorld.marks.push_back({ x, y, item_get_type(object) == ITEM_TYPE_CONTAINER ? KindContainer : KindItem, 0 });
                break;
            case OBJ_TYPE_CRITTER:
                if (critter_is_dead(object)) {
                    gWorld.marks.push_back({ x, y, KindCorpse, 0 });
                } else if (isPartyMember(object) || object->data.critter.combat.team == obj_dude->data.critter.combat.team) {
                    gWorld.marks.push_back({ x, y, KindParty, 0 });
                } else {
                    gWorld.marks.push_back({ x, y, KindCritter, 0 });
                }
                break;
            default:
                break;
            }
        }

        // Walls: neighbours joined, so they read as lines.
        for (int tile : walls) {
            float x, y;
            tileXY(tile, &x, &y);
            bool joined = false;
            for (int rotation = 0; rotation < 6; rotation++) {
                int next = tile_num_in_direction(tile, rotation, 1);
                if (next < 0 || next == tile || walls.find(next) == walls.end()) {
                    continue;
                }
                joined = true;
                if (next > tile) {
                    float nx, ny;
                    tileXY(next, &nx, &ny);
                    gWorld.walls.push_back({ x, y, nx, ny });
                }
            }
            if (!joined) {
                gWorld.walls.push_back({ x, y, x, y });
            }
        }
    }

    // The plane position the map's rect shows at screen ([px], [py]).
    void toWorld(const MuiRect& rect, const MuiPanZoom& view, float px, float py, float* x, float* y)
    {
        *x = view.centerX + (px - rect.centerX()) / view.zoom;
        *y = view.centerY + (py - rect.centerY()) / view.zoom;
    }

    // The walkable tile nearest to a plane position, -1 if none is close.
    int pickTile(float x, float y)
    {
        int best = -1;
        float bestDistance = 28.0f * 28.0f;
        for (int tile = 0; tile < HEX_GRID_SIZE; tile++) {
            float tx, ty;
            tileXY(tile, &tx, &ty);
            if (fabsf(tx - x) > 30.0f || fabsf(ty - y) > 30.0f) {
                continue;
            }
            float distance = (tx - x) * (tx - x) + (ty - y) * (ty - y);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = tile;
            }
        }
        return best;
    }

    void drawMark(const Mark& mark, float x, float y, float radius, const Palette& colors)
    {
        MuiColor outline = muiRgb(0x000000, 170);
        switch (mark.kind) {
        case KindDoor:
            muiFillRect({ x - radius - 1.0f, y - radius - 1.0f, radius * 2.0f + 2.0f, radius * 2.0f + 2.0f }, outline);
            muiFillRect({ x - radius, y - radius, radius * 2.0f, radius * 2.0f }, colors.door);
            break;
        case KindContainer:
            muiFillConvex({ { x, y - radius - 1.5f }, { x + radius + 1.5f, y }, { x, y + radius + 1.5f }, { x - radius - 1.5f, y } }, outline);
            muiFillConvex({ { x, y - radius }, { x + radius, y }, { x, y + radius }, { x - radius, y } }, colors.container);
            break;
        case KindItem: {
            float small = radius * 0.7f;
            muiFillConvex({ { x, y - small }, { x + small, y }, { x, y + small }, { x - small, y } }, colors.item);
            break;
        }
        case KindLevel:
            muiFillConvex({ { x, y - radius * 1.15f - 1.5f }, { x + radius + 1.5f, y + radius * 0.8f + 1.0f }, { x - radius - 1.5f, y + radius * 0.8f + 1.0f } }, outline);
            muiFillConvex({ { x, y - radius * 1.15f }, { x + radius, y + radius * 0.8f }, { x - radius, y + radius * 0.8f } }, colors.level);
            break;
        case KindExit:
            muiFillConvex({ { x, y - radius - 2.0f }, { x + radius + 2.0f, y }, { x, y + radius + 2.0f }, { x - radius - 2.0f, y } }, outline);
            muiFillConvex({ { x, y - radius }, { x + radius, y }, { x, y + radius }, { x - radius, y } }, colors.exit);
            break;
        case KindCritter:
            muiFillCircle(x, y, radius * 2.0f, colors.critter.withAlpha(50));
            muiFillCircle(x, y, radius, colors.critter);
            break;
        case KindParty:
            muiStrokeCircle(x, y, radius, std::max(radius * 0.45f, 1.5f), colors.party);
            break;
        case KindCorpse:
            muiDrawLine(x - radius * 0.6f, y - radius * 0.6f, x + radius * 0.6f, y + radius * 0.6f, 1.5f, colors.corpse);
            muiDrawLine(x - radius * 0.6f, y + radius * 0.6f, x + radius * 0.6f, y - radius * 0.6f, 1.5f, colors.corpse);
            break;
        }
    }

    void drawDude(MuiContext& ui, float x, float y)
    {
        float pulse = static_cast<float>(ui.now % 1600) / 1600.0f;
        MuiColor ring = muiTheme().accent;
        muiStrokeCircle(x, y, ui.dp(8.0f) + ui.dp(14.0f) * pulse, ui.dp(2.0f), ring.withAlpha(static_cast<Uint8>(200.0f * (1.0f - pulse))));
        float angle = (obj_dude != nullptr ? obj_dude->rotation * 60.0f + 30.0f : 0.0f) * static_cast<float>(M_PI) / 180.0f;
        float radius = ui.dp(7.0f);
        auto at = [&](float a, float b) {
            return SDL_FPoint { x + a * cosf(angle) - b * sinf(angle), y + a * sinf(angle) + b * cosf(angle) };
        };
        std::vector<SDL_FPoint> outer = { at(0, -radius * 2.0f), at(radius * 1.2f, radius * 1.2f), at(-radius * 1.2f, radius * 1.2f) };
        std::vector<SDL_FPoint> inner = { at(0, -radius * 1.7f), at(radius * 0.9f, radius * 0.9f), at(-radius * 0.9f, radius * 0.9f) };
        muiFillConvex(outer, muiRgb(0x000000, 200));
        muiFillConvex(inner, palette().dude);
    }

    void legend(MuiContext& ui, const MuiRect& area, const MuiRect& avoid)
    {
        struct Key {
            Kind kind;
            const char* text;
        };
        const Key keys[] = { { KindDoor, "Door" }, { KindContainer, "Container" }, { KindItem, "Item" }, { KindLevel, "Stairs" }, { KindExit, "Exit" }, { KindCritter, "Critter" }, { KindParty, "Party" } };
        Palette colors = palette();
        float size = ui.dp(13.0f);
        float height = ui.dp(26.0f);
        float gap = ui.dp(6.0f);
        float left = area.x + ui.dp(6.0f);
        float x = left;
        float y = area.bottom() - height - ui.dp(6.0f);
        auto rowRight = [&](float rowY) {
            float right = area.right() - ui.dp(6.0f);
            if (avoid.w > 0.0f && rowY < avoid.bottom() && rowY + height > avoid.y) {
                right = std::min(right, avoid.x - ui.dp(6.0f));
            }
            return right;
        };
        for (const Key& key : keys) {
            std::u32string name = u(key.text);
            float width = height + muiTextWidth(name, size) + ui.dp(8.0f);
            if (x + width > rowRight(y) && x > left) {
                x = left;
                y -= height + ui.dp(4.0f);
            }
            if (x + width > rowRight(y) || y < area.y) {
                break;
            }
            MuiRect pill = { x, y, width, height };
            muiFillRoundRect(pill, height / 2.0f, muiRgb(0x000000, 190));
            Mark mark = { 0, 0, key.kind, 0 };
            drawMark(mark, pill.x + height / 2.0f, pill.centerY(), ui.dp(4.0f), colors);
            muiDrawTextAligned(name, { pill.x + height, pill.y, width - height - ui.dp(4.0f), height }, size, muiTheme().text, MuiAlign::Start, MuiAlign::Center);
            x += width + gap;
        }
    }

} // namespace

void muiLiveMapPage(MuiContext& ui, bool* open)
{
    const MuiTheme& theme = muiTheme();
    bool back = false;
    MuiRect content = muiScreenFrame(ui, U"LIVE MAP", &back);
    if (back) {
        *open = false;
        return;
    }

    unsigned int now = ui.now;
    if (gKeyMap != map_get_index_number() || gKeyElevation != map_elevation || gBuilt == 0 || now - gBuilt >= kRefreshMs) {
        bool changed = gKeyMap != map_get_index_number() || gKeyElevation != map_elevation;
        rebuild(now);
        if (changed) {
            gViewReady = false;
            gDestination = -1;
        }
    }

    float playerX = 0.0f;
    float playerY = 0.0f;
    if (obj_dude != nullptr) {
        tileXY(obj_dude->tile, &playerX, &playerY);
    }

    // Header: where, level, clock, and the way to the game's own automap.
    float gap = ui.dp(10.0f);
    float rowHeight = ui.dp(44.0f);
    float autoWidth = ui.dp(140.0f);
    MuiRect row = { content.x, content.y, content.w, rowHeight };
    MuiRect info = { row.x, row.y, row.w - autoWidth - gap, rowHeight };
    muiSkinPlate(ui, info, false);
    MuiRect well = info.inset(ui.dp(5.0f));
    muiSkinWell(ui, well);
    std::u32string place;
    int mapNumber = map_get_index_number();
    if (mapNumber >= 0) {
        place = muiDecodeGameText(map_get_short_name(mapNumber));
    }
    if (place.empty()) {
        place = u("Wasteland");
    }
    char tail[48];
    int hour = game_time_hour();
    snprintf(tail, sizeof(tail), "LEVEL %d  %02d:%02d", map_elevation + 1, hour / 100, hour % 100);
    std::u32string right = u(tail);
    float rightWidth = muiTextWidth(right, ui.dp(15.0f)) + ui.dp(12.0f);
    MuiRect inner = well.inset(ui.dp(10.0f), 0.0f);
    muiDrawTextAligned(right, inner, ui.dp(15.0f), theme.text, MuiAlign::End, MuiAlign::Center);
    MuiRect placeRect = { inner.x, inner.y, inner.w - rightWidth, inner.h };
    muiDrawTextAligned(place, placeRect, muiFitTextSize(place, placeRect.w, ui.dp(18.0f), ui.dp(11.0f)), theme.accent, MuiAlign::Start, MuiAlign::Center);
    if (ui.button("map.automap", { row.right() - autoWidth, row.y, autoWidth, rowHeight }, U"AUTOMAP")) {
        MobileCommand command;
        command.generation = mobileSnapshot().generation;
        command.type = MobileCommandType::OpenScreen;
        command.id = static_cast<int>(MobileScreen::Map);
        mobileQueueCommand(command);
    }

    MuiRect area = { content.x, row.bottom() + gap, content.w, content.bottom() - row.bottom() - gap };
    muiSkinPlate(ui, area, false);
    MuiRect map = area.inset(ui.dp(7.0f));
    muiSkinWell(ui, map);
    muiFillRect(map, theme.well.withAlpha(120));

    if (!gViewReady) {
        gView = MuiPanZoom();
        gView.zoom = ui.dp(0.6f);
        gView.centerX = playerX;
        gView.centerY = playerY;
        gViewReady = true;
        gTouched = 0;
    }
    gView.minZoom = ui.dp(0.15f);
    gView.maxZoom = ui.dp(2.2f);

    // CENTER and LEGEND sit over the map's right edge; a touch that begins on
    // them is theirs (the map would claim it first).
    float buttonWidth = ui.dp(104.0f);
    float buttonHeight = ui.dp(40.0f);
    MuiRect center = { map.right() - buttonWidth - ui.dp(8.0f), map.y + ui.dp(8.0f), buttonWidth, buttonHeight };
    MuiRect legendButton = { center.x, center.bottom() + ui.dp(8.0f), buttonWidth, buttonHeight };
    bool onButtons = ui.pointerPressed() && (center.contains(ui.pointerStartX(), ui.pointerStartY()) || legendButton.contains(ui.pointerStartX(), ui.pointerStartY()));

    bool tapped = false;
    MuiPanZoom pickView = gView;
    bool touched = onButtons ? false : ui.panZoom("map.view", map, &gView, &tapped);
    if (touched) {
        gTouched = now != 0 ? now : 1;
    }
    gView.zoom = std::clamp(gView.zoom, gView.minZoom, gView.maxZoom);
    // Follows dude until a finger moved it, and again a few seconds after.
    if (gTouched == 0 || (!ui.pointerDown() && now - gTouched >= kHoldMs)) {
        gTouched = 0;
        gView.centerX = playerX;
        gView.centerY = playerY;
    }

    if (tapped && map.contains(ui.pointerX(), ui.pointerY())) {
        float x, y;
        toWorld(map, pickView, ui.pointerX(), ui.pointerY(), &x, &y);
        int tile = pickTile(x, y);
        if (tile >= 0 && obj_dude != nullptr && obj_blocking_at(obj_dude, tile, obj_dude->elevation) == nullptr) {
            gDestination = tile;
            MobileCommand command;
            command.generation = mobileSnapshot().generation;
            command.type = MobileCommandType::Walk;
            command.id = tile;
            mobileQueueCommand(command);
        } else {
            gNoticeUntil = now + 1800;
        }
    }

    Palette colors = palette();
    muiPushClip(map);
    auto sx = [&](float x) { return map.centerX() + (x - gView.centerX) * gView.zoom; };
    auto sy = [&](float y) { return map.centerY() + (y - gView.centerY) * gView.zoom; };
    float wallThickness = std::max(1.5f, gView.zoom * 3.5f);
    for (const Line& line : gWorld.walls) {
        float x1 = sx(line.x1), y1 = sy(line.y1), x2 = sx(line.x2), y2 = sy(line.y2);
        if (std::max(x1, x2) < map.x || std::min(x1, x2) > map.right() || std::max(y1, y2) < map.y || std::min(y1, y2) > map.bottom()) {
            continue;
        }
        if (line.x1 == line.x2 && line.y1 == line.y2) {
            muiFillCircle(x1, y1, wallThickness * 0.5f, colors.wall);
        } else {
            muiDrawLine(x1, y1, x2, y2, wallThickness, colors.wall);
        }
    }
    float radius = std::clamp(gView.zoom * 8.0f, ui.dp(3.0f), ui.dp(7.0f));
    // Corpses and items under the living.
    for (int pass = 0; pass < 2; pass++) {
        for (const Mark& mark : gWorld.marks) {
            bool low = mark.kind == KindCorpse || mark.kind == KindItem || mark.kind == KindDoor;
            if (low != (pass == 0)) {
                continue;
            }
            float x = sx(mark.x), y = sy(mark.y);
            if (!map.contains(x, y)) {
                continue;
            }
            drawMark(mark, x, y, radius, colors);
        }
    }
    if (gDestination >= 0) {
        float x, y;
        tileXY(gDestination, &x, &y);
        muiStrokeCircle(sx(x), sy(y), ui.dp(7.0f), ui.dp(2.0f), muiRgb(0xFFD060));
    }
    drawDude(ui, sx(playerX), sy(playerY));
    if (gView.zoom >= ui.dp(0.4f)) {
        float size = ui.dp(13.0f);
        for (const Label& label : gWorld.labels) {
            float x = sx(label.x), y = sy(label.y) - ui.dp(16.0f);
            float width = muiTextWidth(label.text, size) + ui.dp(12.0f);
            MuiRect plate = { x - width / 2.0f, y - ui.dp(11.0f), width, ui.dp(22.0f) };
            if (!map.contains(plate.x, plate.y) || !map.contains(plate.right(), plate.bottom())) {
                continue;
            }
            muiFillRoundRect(plate, ui.dp(4.0f), muiRgb(0x000000, 190));
            muiStrokeRoundRect(plate, ui.dp(4.0f), ui.dp(1.0f), colors.exit.withAlpha(150));
            muiDrawTextAligned(label.text, plate, size, colors.exit, MuiAlign::Center, MuiAlign::Center);
        }
    }
    muiPopClip();

    // (The legend keeps clear of the buttons.)
    if (ui.button("map.center", center, U"CENTER")) {
        gTouched = 0;
        gView.centerX = playerX;
        gView.centerY = playerY;
    }
    if (ui.button("map.legend", legendButton, U"LEGEND")) {
        gLegend = !gLegend;
    }
    if (gLegend) {
        legend(ui, map, { center.x, center.y, center.w, legendButton.bottom() - center.y });
    }
    if (now < gNoticeUntil) {
        std::u32string text = u("Can't walk there");
        float width = muiTextWidth(text, ui.dp(15.0f)) + ui.dp(24.0f);
        MuiRect plate = { map.centerX() - width / 2.0f, map.y + ui.dp(10.0f), width, ui.dp(32.0f) };
        muiFillRoundRect(plate, ui.dp(5.0f), muiRgb(0x000000, 200));
        muiDrawTextAligned(text, plate, ui.dp(15.0f), theme.warning, MuiAlign::Center, MuiAlign::Center);
    }
}

} // namespace fallout
