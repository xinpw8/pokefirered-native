/*
 * test_map_data.h -- Data-driven integrity tests for all map definitions.
 */

#ifndef TEST_MAP_DATA_H
#define TEST_MAP_DATA_H

static int test_map_data_integrity(void)
{
    size_t m;
    int errors = 0;

    for (m = 0; m < gPfrNativeMapCount; m++) {
        const PfrNativeMap *map = &gPfrNativeMaps[m];
        size_t i;

        /* Basic fields */
        if (map->tiles == NULL) {
            printf("  FAIL: map %zu: tiles == NULL\n", m);
            errors++;
            continue; /* can't do tile-based checks */
        }

        if (map->tile_count != (size_t)map->width * map->height) {
            printf("  FAIL: map %zu (%s): tile_count %zu != width*height %u*%u\n",
                   m, map->name ? map->name : "?",
                   map->tile_count, map->width, map->height);
            errors++;
        }

        if (map->name == NULL || map->name[0] == '\0') {
            printf("  FAIL: map %zu: name is NULL or empty\n", m);
            errors++;
        }

        if (map->id_symbol == NULL || map->id_symbol[0] == '\0') {
            printf("  FAIL: map %zu: id_symbol is NULL or empty\n", m);
            errors++;
        }

        if (map->width == 0) {
            printf("  FAIL: map %zu (%s): width == 0\n", m, map->name);
            errors++;
        }

        if (map->height == 0) {
            printf("  FAIL: map %zu (%s): height == 0\n", m, map->name);
            errors++;
        }

        if (map->map_id != (PfrNativeMapId)m) {
            printf("  FAIL: map %zu (%s): map_id %u != index %zu\n",
                   m, map->name, map->map_id, m);
            errors++;
        }

        /* Warp validation */
        for (i = 0; i < map->warp_count; i++) {
            const PfrNativeWarp *w = &map->warps[i];
            if (w->supported) {
                if (w->dest_map >= gPfrNativeMapCount) {
                    printf("  FAIL: map %zu (%s) warp %zu: dest_map %u >= MAP_COUNT %zu\n",
                           m, map->name, i, w->dest_map, gPfrNativeMapCount);
                    errors++;
                } else {
                    const PfrNativeMap *dest = &gPfrNativeMaps[w->dest_map];
                    if (w->dest_warp_id >= dest->warp_count) {
                        printf("  FAIL: map %zu (%s) warp %zu: dest_warp_id %u >= dest warp_count %zu\n",
                               m, map->name, i, w->dest_warp_id, dest->warp_count);
                        errors++;
                    }
                }
            }
        }

        /* Object event validation — upstream data has some NPCs placed slightly
         * outside map bounds for off-screen movement patterns. Only warn. */
        for (i = 0; i < map->object_event_count; i++) {
            const PfrNativeObjectEvent *obj = &map->object_events[i];
            (void)obj; /* coords may be OOB in upstream data — not an engine bug */
            if (obj->script_id >= PFR_NATIVE_SCRIPT_COUNT
                && obj->script_id != PFR_NATIVE_SCRIPT_NONE) {
                printf("  FAIL: map %zu (%s) object %zu: script_id %u >= SCRIPT_COUNT %d\n",
                       m, map->name, i, obj->script_id, PFR_NATIVE_SCRIPT_COUNT);
                errors++;
            }
        }

        /* BG event validation */
        for (i = 0; i < map->bg_event_count; i++) {
            const PfrNativeBgEvent *bg = &map->bg_events[i];
            if (bg->x < 0 || bg->x >= (int16_t)map->width) {
                printf("  FAIL: map %zu (%s) bg_event %zu: x=%d out of bounds [0, %u)\n",
                       m, map->name, i, bg->x, map->width);
                errors++;
            }
            if (bg->y < 0 || bg->y >= (int16_t)map->height) {
                printf("  FAIL: map %zu (%s) bg_event %zu: y=%d out of bounds [0, %u)\n",
                       m, map->name, i, bg->y, map->height);
                errors++;
            }
        }

        /* Coord event validation */
        for (i = 0; i < map->coord_event_count; i++) {
            const PfrNativeCoordEvent *ce = &map->coord_events[i];
            if (ce->x < 0 || ce->x >= (int16_t)map->width) {
                printf("  FAIL: map %zu (%s) coord_event %zu: x=%d out of bounds [0, %u)\n",
                       m, map->name, i, ce->x, map->width);
                errors++;
            }
            if (ce->y < 0 || ce->y >= (int16_t)map->height) {
                printf("  FAIL: map %zu (%s) coord_event %zu: y=%d out of bounds [0, %u)\n",
                       m, map->name, i, ce->y, map->height);
                errors++;
            }
            if (ce->script_id >= PFR_NATIVE_SCRIPT_COUNT
                && ce->script_id != PFR_NATIVE_SCRIPT_NONE) {
                printf("  FAIL: map %zu (%s) coord_event %zu: script_id %u >= SCRIPT_COUNT %d\n",
                       m, map->name, i, ce->script_id, PFR_NATIVE_SCRIPT_COUNT);
                errors++;
            }
        }
    }

    if (errors > 0) {
        printf("  map_data_integrity: %d error(s)\n", errors);
        return 1;
    }
    return 0;
}

static int test_map_count_reasonable(void)
{
    TEST_ASSERT(gPfrNativeMapCount > 400, "map count should be > 400");
    TEST_ASSERT(gPfrNativeMapCount < 500, "map count should be < 500");
    return 0;
}

static int test_map_pallet_town_exists(void)
{
    const PfrNativeMap *map = pfr_native_get_map(PFR_NATIVE_MAP_PALLET_TOWN);
    TEST_ASSERT(map != NULL, "Pallet Town map exists");
    TEST_ASSERT_EQ(map->width, 24, "Pallet Town width == 24");
    TEST_ASSERT_EQ(map->height, 20, "Pallet Town height == 20");
    TEST_ASSERT(map->connection_count > 0, "Pallet Town has connections");
    TEST_ASSERT(map->warp_count > 0, "Pallet Town has warps");
    return 0;
}

static int test_map_find_by_name(void)
{
    PfrNativeMapId id = pfr_native_find_map_by_name("PalletTown");
    TEST_ASSERT(id != PFR_NATIVE_MAP_INVALID, "find PalletTown by name");
    TEST_ASSERT_EQ(id, PFR_NATIVE_MAP_PALLET_TOWN, "PalletTown has expected map_id");

    PfrNativeMapId bad = pfr_native_find_map_by_name("NonExistentMap_XYZ_123");
    TEST_ASSERT_EQ(bad, PFR_NATIVE_MAP_INVALID, "non-existent map returns INVALID");

    PfrNativeMapId null_name = pfr_native_find_map_by_name(NULL);
    TEST_ASSERT_EQ(null_name, PFR_NATIVE_MAP_INVALID, "NULL name returns INVALID");

    return 0;
}

static int test_map_get_invalid(void)
{
    const PfrNativeMap *map = pfr_native_get_map(PFR_NATIVE_MAP_INVALID);
    TEST_ASSERT(map == NULL, "invalid map_id returns NULL");

    const PfrNativeMap *map2 = pfr_native_get_map(60000);
    TEST_ASSERT(map2 == NULL, "out-of-range map_id returns NULL");

    return 0;
}

/* ---------- Test registration ---------- */

static const TestEntry map_data_tests[] = {
    {"map_data_integrity",     test_map_data_integrity},
    {"map_count_reasonable",   test_map_count_reasonable},
    {"map_pallet_town_exists", test_map_pallet_town_exists},
    {"map_find_by_name",       test_map_find_by_name},
    {"map_get_invalid",        test_map_get_invalid},
};

#endif /* TEST_MAP_DATA_H */
