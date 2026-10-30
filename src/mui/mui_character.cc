#include "mui/mui.h"
#include "mui/mui_gameskin.h"
#include "mui/mui_screens.h"

#include <stdio.h>

#include <algorithm>

#include "game/critter.h"
#include "game/object.h"
#include "game/perk.h"
#include "game/perk_defs.h"
#include "game/skill.h"
#include "game/skill_defs.h"
#include "game/stat.h"
#include "game/stat_defs.h"
#include "game/trait.h"

namespace fallout {

namespace {

    std::u32string num(int value)
    {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d", value);
        return muiDecodeUtf8(buffer);
    }

    std::u32string name(const char* text)
    {
        return muiDecodeGameText(text != nullptr ? text : "");
    }

    // Read-only character sheet (F1 stats, skills, traits and perks). Editing
    // (level ups, skill points) stays in the classic editor.
    class CharacterScreen : public MuiScreen {
    public:
        CharacterScreen() { modal = true; }

        MuiSurface surface() override { return muiGameScreenSurface(); }
        bool coversScreen() override { return true; }
        void back() override { finished = true; }
        void build(MuiContext& ui) override;

    public:
        bool classic = false;

    private:
        int tab = 0;
        void stats(std::vector<MuiRow>& rows);
        void skills(std::vector<MuiRow>& rows);
        void perks(std::vector<MuiRow>& rows);
    };

    void CharacterScreen::stats(std::vector<MuiRow>& rows)
    {
        rows.push_back({ U"SPECIAL", U"", true });
        for (int stat = STAT_STRENGTH; stat <= STAT_LUCK; stat++) {
            rows.push_back({ name(stat_name(stat)), num(stat_level(obj_dude, stat)) });
        }
        rows.push_back({ U"DERIVED", U"", true });
        static const int derived[] = { STAT_MAXIMUM_HIT_POINTS, STAT_MAXIMUM_ACTION_POINTS, STAT_ARMOR_CLASS, STAT_CARRY_WEIGHT, STAT_MELEE_DAMAGE, STAT_SEQUENCE, STAT_HEALING_RATE, STAT_CRITICAL_CHANCE, STAT_RADIATION_RESISTANCE, STAT_POISON_RESISTANCE };
        for (int stat : derived) {
            rows.push_back({ name(stat_name(stat)), num(stat_level(obj_dude, stat)) });
        }
        rows.push_back({ U"RESISTANCE / THRESHOLD", U"", true });
        static const int damage[] = { STAT_DAMAGE_RESISTANCE, STAT_DAMAGE_THRESHOLD, STAT_DAMAGE_RESISTANCE_LASER, STAT_DAMAGE_RESISTANCE_FIRE, STAT_DAMAGE_RESISTANCE_PLASMA, STAT_DAMAGE_RESISTANCE_ELECTRICAL, STAT_DAMAGE_RESISTANCE_EXPLOSION };
        for (int stat : damage) {
            rows.push_back({ name(stat_name(stat)), num(stat_level(obj_dude, stat)) });
        }
    }

    void CharacterScreen::skills(std::vector<MuiRow>& rows)
    {
        int tagged[4];
        skill_get_tags(tagged, 4);
        char points[48];
        snprintf(points, sizeof(points), "SKILL POINTS %d", stat_pc_get(PC_STAT_UNSPENT_SKILL_POINTS));
        rows.push_back({ muiDecodeUtf8(points), U"", true });
        for (int skill = 0; skill < SKILL_COUNT; skill++) {
            bool isTagged = false;
            for (int index = 0; index < 4; index++) {
                isTagged |= tagged[index] == skill;
            }
            MuiRow row = { name(skill_name(skill)), num(skill_level(obj_dude, skill)) + U"%" };
            row.selected = isTagged;
            rows.push_back(row);
        }
    }

    void CharacterScreen::perks(std::vector<MuiRow>& rows)
    {
        rows.push_back({ U"TRAITS", U"", true });
        int traits[2];
        trait_get(&traits[0], &traits[1]);
        int shown = 0;
        for (int trait : traits) {
            if (trait >= 0 && trait < TRAIT_COUNT) {
                rows.push_back({ name(trait_name(trait)), U"" });
                shown++;
            }
        }
        if (shown == 0) {
            rows.push_back({ U"NONE", U"", false, false, true });
        }
        rows.push_back({ U"PERKS", U"", true });
        int perkCount = 0;
        for (int perk = 0; perk < PERK_COUNT; perk++) {
            int level = perk_level(perk);
            if (level > 0) {
                rows.push_back({ name(perk_name(perk)), level > 1 ? num(level) : U"" });
                perkCount++;
            }
        }
        if (perkCount == 0) {
            rows.push_back({ U"NONE", U"", false, false, true });
        }
    }

    void CharacterScreen::build(MuiContext& ui)
    {
        const MuiTheme& theme = muiTheme();
        bool back = false;
        MuiRect content = muiScreenFrame(ui, U"CHARACTER", &back);
        if (back) {
            finished = true;
        }

        // Who: name, level, experience, hit points.
        float headerHeight = ui.dp(84.0f);
        MuiRect header = { content.x, content.y, content.w, headerHeight };
        ui.panel(header);
        MuiRect inner = header.inset(ui.dp(12.0f));
        std::u32string who = muiPlayerName();
        muiDrawTextAligned(who, { inner.x, inner.y, inner.w, inner.h * 0.5f }, ui.dp(22.0f), theme.accent, MuiAlign::Start, MuiAlign::Center);
        char line[96];
        snprintf(line, sizeof(line), "LEVEL %d   XP %d   HP %d/%d", stat_pc_get(PC_STAT_LEVEL), stat_pc_get(PC_STAT_EXPERIENCE), critter_get_hits(obj_dude), stat_level(obj_dude, STAT_MAXIMUM_HIT_POINTS));
        muiDrawTextAligned(muiDecodeUtf8(line), { inner.x, inner.y + inner.h * 0.5f, inner.w, inner.h * 0.5f }, muiFitTextSize(muiDecodeUtf8(line), inner.w, ui.dp(16.0f), ui.dp(10.0f)), theme.text, MuiAlign::Start, MuiAlign::Center);

        MuiRect tabs = { content.x, header.bottom() + ui.dp(8.0f), content.w, ui.dp(46.0f) };
        ui.segmented("character.tabs", tabs, { U"STATS", U"SKILLS", U"PERKS" }, &tab);

        std::vector<MuiRow> rows;
        if (tab == 0) {
            stats(rows);
        } else if (tab == 1) {
            skills(rows);
        } else {
            perks(rows);
        }
        float editHeight = ui.dp(52.0f);
        MuiRect edit = { content.x, content.bottom() - editHeight, content.w, editHeight };
        if (ui.button("character.edit", edit, U"CLASSIC EDITOR (LEVEL UP, SKILLS)")) {
            classic = true;
            finished = true;
        }
        MuiRect list = { content.x, tabs.bottom() + ui.dp(8.0f), content.w, edit.y - tabs.bottom() - ui.dp(16.0f) };
        muiListView(ui, "character.list." + std::to_string(tab), list, rows);
    }

} // namespace

int muiCharacterScreenRun()
{
    if (!muiIsEnabled()) {
        return -1;
    }
    CharacterScreen screen;
    muiRunModal(&screen);
    return screen.classic ? 1 : 0;
}

MuiScreen* muiCharacterCreate()
{
    return new CharacterScreen();
}

} // namespace fallout
