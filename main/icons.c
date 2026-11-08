#include "icons.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "esp_heap_caps.h"

/* A dozen services is more than one conversation uses; the oldest goes. */
#define SLOTS 12

typedef struct {
    char name[ICON_NAME_MAX + 1];
    uint8_t *buf; // ICON_BYTES in PSRAM, allocated on first use
    uint32_t got;
    uint32_t used; // a counter: the smallest was used longest ago
    lv_image_dsc_t dsc;
} slot_t;

static slot_t s_slots[SLOTS];
static uint32_t s_clock;

static slot_t *find(const char *name)
{
    for (int i = 0; i < SLOTS; i++) {
        if (s_slots[i].name[0] != '\0' && strcasecmp(s_slots[i].name, name) == 0) {
            return &s_slots[i];
        }
    }
    return NULL;
}

/* An empty slot, or the one used longest ago. */
static slot_t *free_slot(void)
{
    slot_t *old = &s_slots[0];
    for (int i = 0; i < SLOTS; i++) {
        if (s_slots[i].name[0] == '\0') {
            return &s_slots[i];
        }
        if (s_slots[i].used < old->used) {
            old = &s_slots[i];
        }
    }
    return old;
}

bool icons_put(const char *name, uint32_t at, const uint8_t *data, size_t len, const char **err)
{
    slot_t *sl = find(name);
    if (at == 0) {
        if (sl == NULL) {
            sl = free_slot();
        }
        if (sl->buf == NULL) {
            sl->buf = heap_caps_malloc(ICON_BYTES, MALLOC_CAP_SPIRAM);
            if (sl->buf == NULL) {
                *err = "no memory for the icon";
                return false;
            }
        }
        snprintf(sl->name, sizeof(sl->name), "%s", name);
        sl->got = 0;
        sl->used = ++s_clock;
    }
    if (sl == NULL || at != sl->got) {
        *err = "parts must come in order, from 0";
        return false;
    }
    if (sl->got + len > ICON_BYTES) {
        *err = "more than the icon's bytes";
        return false;
    }
    memcpy(sl->buf + sl->got, data, len);
    sl->got += (uint32_t) len;
    if (sl->got == ICON_BYTES) {
        sl->dsc = (lv_image_dsc_t) {
            .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565A8, .w = ICON_SIDE, .h = ICON_SIDE,
                       .stride = ICON_SIDE * 2},
            .data_size = ICON_BYTES,
            .data = sl->buf,
        };
        // A new picture under the same pointer: drop what LVGL cached of it.
        lv_image_cache_drop(&sl->dsc);
    }
    return true;
}

const lv_image_dsc_t *icons_find(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return NULL;
    }
    slot_t *sl = find(name);
    if (sl == NULL || sl->got != ICON_BYTES) {
        return NULL;
    }
    sl->used = ++s_clock;
    return &sl->dsc;
}

int icons_count(void)
{
    int n = 0;
    for (int i = 0; i < SLOTS; i++) {
        n += s_slots[i].name[0] != '\0' && s_slots[i].got == ICON_BYTES;
    }
    return n;
}

void icons_draw(lv_layer_t *layer, const lv_image_dsc_t *icon, float x, float y, float side, lv_opa_t opa)
{
    if (side < 4) {
        return;
    }
    lv_draw_image_dsc_t dsc;
    lv_draw_image_dsc_init(&dsc);
    dsc.src = icon;
    dsc.opa = opa;
    const int32_t scale = (int32_t) (256.0f * side / ICON_SIDE);
    dsc.scale_x = scale;
    dsc.scale_y = scale;
    dsc.pivot.x = ICON_SIDE / 2;
    dsc.pivot.y = ICON_SIDE / 2;
    dsc.antialias = 1;
    // The area is the image's own size; the scale grows it round the pivot.
    const lv_area_t a = {(int32_t) x - ICON_SIDE / 2, (int32_t) y - ICON_SIDE / 2, (int32_t) x - ICON_SIDE / 2 + ICON_SIDE - 1,
                         (int32_t) y - ICON_SIDE / 2 + ICON_SIDE - 1};
    lv_draw_image(layer, &dsc, &a);
}
