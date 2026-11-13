#include "growth.h"

#include <stdio.h>
#include <string.h>

#include "card.h"
#include "esp_log.h"
#include "life_int.h"
#include "look.h"
#include "nvs.h"

static const char *TAG = "growth";

#define NVS_NAMESPACE "growth"

typedef struct {
    growth_measure_t measure;
    int32_t need;
    int palette; // a colour of look.c, or -1
    int act;     // a rare act, or -1
    const char *id;
    const char *en, *it; // what it unlocks, for the page and the toast
} milestone_t;

/* In the order they usually come: something soon, then rarer. A new one goes
 * at the end: the bits in NVS are the indexes. */
static const milestone_t MILESTONES[] = {
    {GROWTH_DAYS, 2, -1, ACT_WAVE, "act:wave", "It waves at you", "Ti saluta con la mano"},
    {GROWTH_FACTS, 10, 1, -1, "sphere:dawn", "The Dawn colour", "Il colore Alba"},
    {GROWTH_PEOPLE, 1, -1, ACT_SHY, "act:shy", "A shy peek", "Una sbirciata timida"},
    {GROWTH_DAYS, 7, 2, -1, "sphere:mint", "The Mint colour", "Il colore Menta"},
    {GROWTH_USEFUL, 3, -1, ACT_HOP, "act:hop", "A happy hop", "Un saltello felice"},
    {GROWTH_FACTS, 30, 3, -1, "sphere:lilac", "The Lilac colour", "Il colore Lilla"},
    {GROWTH_PEOPLE, 3, -1, ACT_GIGGLE, "act:giggle", "A giggle to itself", "Una risatina tra se'"},
    {GROWTH_DAYS, 21, 4, -1, "sphere:night", "The Night colour", "Il colore Notte"},
    {GROWTH_USEFUL, 10, -1, ACT_CHEER, "act:cheer", "A little cheer", "Un piccolo evviva"},
    {GROWTH_FACTS, 75, 5, -1, "sphere:gold", "The Gold colour", "Il colore Oro"},
    {GROWTH_DAYS, 60, -1, ACT_LOVE, "act:love", "Hearts, for you", "Cuori, per te"},
    {GROWTH_PEOPLE, 6, 6, -1, "sphere:rose", "The Rose colour", "Il colore Rosa"},
};
#define N ((int) (sizeof(MILESTONES) / sizeof(MILESTONES[0])))

static const char *const KEYS[GROWTH_MEASURES] = {"days", "facts", "people", "useful"};

static uint32_t s_reached;
static int32_t s_counts[GROWTH_MEASURES];
/* Reached and not yet celebrated: set by the link's task, taken on LVGL's. */
static uint32_t s_news;

void growth_init(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return; // nothing yet
    }
    nvs_get_u32(nvs, "reached", &s_reached);
    for (int i = 0; i < GROWTH_MEASURES; i++) {
        nvs_get_i32(nvs, KEYS[i], &s_counts[i]);
    }
    nvs_close(nvs);
    ESP_LOGI(TAG, "%d of %d milestones", growth_reached_count(), N);
}

static void save(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    nvs_set_u32(nvs, "reached", s_reached);
    for (int i = 0; i < GROWTH_MEASURES; i++) {
        nvs_set_i32(nvs, KEYS[i], s_counts[i]);
    }
    nvs_commit(nvs);
    nvs_close(nvs);
}

uint32_t growth_update(const int32_t counts[GROWTH_MEASURES])
{
    bool changed = false;
    for (int i = 0; i < GROWTH_MEASURES; i++) {
        changed |= counts[i] != s_counts[i];
        s_counts[i] = counts[i];
    }
    // Reached stays reached: forgetting a fact takes nothing back.
    uint32_t fresh = 0;
    for (int i = 0; i < N; i++) {
        if (!(s_reached & (1u << i)) && s_counts[MILESTONES[i].measure] >= MILESTONES[i].need) {
            fresh |= 1u << i;
        }
    }
    s_reached |= fresh;
    if (changed || fresh) {
        save(); // the counts too: the page shows them without the computer
    }
    if (fresh) {
        __atomic_fetch_or(&s_news, fresh, __ATOMIC_RELAXED);
        ESP_LOGI(TAG, "reached 0x%lx", (unsigned long) fresh);
    }
    return fresh;
}

int growth_milestones(void)
{
    return N;
}

growth_measure_t growth_milestone_measure(int i)
{
    return MILESTONES[i].measure;
}

int32_t growth_milestone_need(int i)
{
    return MILESTONES[i].need;
}

const char *growth_milestone_reward(int i, bool italian)
{
    return italian ? MILESTONES[i].it : MILESTONES[i].en;
}

const char *growth_milestone_id(int i)
{
    return MILESTONES[i].id;
}

bool growth_reached(int i)
{
    return i >= 0 && i < N && (s_reached & (1u << i));
}

int32_t growth_count(growth_measure_t m)
{
    return s_counts[m];
}

int32_t growth_next(growth_measure_t m)
{
    int32_t next = -1;
    for (int i = 0; i < N; i++) {
        if (MILESTONES[i].measure == m && !growth_reached(i) && (next < 0 || MILESTONES[i].need < next)) {
            next = MILESTONES[i].need;
        }
    }
    return next;
}

int growth_reached_count(void)
{
    return __builtin_popcount(s_reached);
}

bool growth_palette_ok(int palette)
{
    if (palette == 0) {
        return true; // pearl, from the start
    }
    for (int i = 0; i < N; i++) {
        if (MILESTONES[i].palette == palette) {
            return growth_reached(i);
        }
    }
    return false;
}

bool growth_act_ok(int act)
{
    if (act < 0 || act >= ACT_COUNT || !(ACTS[act].flags & ACTF_RARE)) {
        return true;
    }
    for (int i = 0; i < N; i++) {
        if (MILESTONES[i].act == act) {
            return growth_reached(i);
        }
    }
    return false;
}

const char *growth_take_news(void)
{
    const uint32_t news = __atomic_exchange_n(&s_news, 0, __ATOMIC_RELAXED);
    if (news == 0) {
        return NULL;
    }
    static char line[72];
    const int n = __builtin_popcount(news);
    if (n > 1) {
        snprintf(line, sizeof(line), card_tr("%d new things: Settings > Yoru", "%d novita': Impostazioni > Yoru"), n);
        return line;
    }
    const int i = __builtin_ctz(news);
    snprintf(line, sizeof(line), "%s %s", card_tr("New:", "Novita':"), growth_milestone_reward(i, card_italian()));
    return line;
}
