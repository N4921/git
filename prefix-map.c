#include "cache.h"
#include "prefix-map.h"

struct prefix_map_entry {
	struct hashmap_entry e;
	const char *name;
	size_t prefix_length;
	/* if item is NULL, the prefix is not unique */
	struct prefix_item *item;
};

struct prefix_map {
	struct hashmap map;
	size_t min_length, max_length;
};

static int map_cmp(const void *unused_cmp_data,
		   const void *entry,
		   const void *entry_or_key,
		   const void *unused_keydata)
{
	const struct prefix_map_entry *a = entry;
	const struct prefix_map_entry *b = entry_or_key;

	return a->prefix_length != b->prefix_length ||
		strncmp(a->name, b->name, a->prefix_length);
}

static void add_prefix_entry(struct hashmap *map, const char *name,
			     size_t prefix_length, struct prefix_item *item)
{
	struct prefix_map_entry *result = xmalloc(sizeof(*result));
	result->name = name;
	result->prefix_length = prefix_length;
	result->item = item;
	hashmap_entry_init(&result->e, memhash(name, prefix_length));
	hashmap_add(map, &result->e);
}

static void init_prefix_map(struct prefix_map *prefix_map,
			    size_t min_prefix_length, size_t max_prefix_length)
{
	hashmap_init(&prefix_map->map, map_cmp, NULL, 0);
	prefix_map->min_length = min_prefix_length;
	prefix_map->max_length = max_prefix_length;
}

static void add_prefix_item(struct prefix_map *prefix_map,
			    struct prefix_item *item)
{
	struct prefix_map_entry e = { { NULL } }, *e2;
	size_t j;

	e.item = item;
	e.name = item->name;

	for (j = prefix_map->min_length;
	     j <= prefix_map->max_length && e.name[j]; j++) {
		/* Avoid breaking UTF-8 multi-byte sequences */
		if (!isascii(e.name[j]))
			break;

		e.prefix_length = j;
		hashmap_entry_init(&e.e, memhash(e.name, j));
		e2 = hashmap_get(&prefix_map->map, &e.e, NULL);
		if (!e2) {
			/* prefix is unique at this stage */
			item->prefix_length = j;
			add_prefix_entry(&prefix_map->map, e.name, j, item);
			break;
		}

		if (!e2->item)
			continue; /* non-unique prefix */

		if (j != e2->item->prefix_length || memcmp(e.name, e2->name, j))
			BUG("unexpected prefix length: %d != %d (%s != %s)",
			    (int)j, (int)e2->item->prefix_length,
			    e.name, e2->name);

		/* skip common prefix */
		for (; j < prefix_map->max_length && e.name[j]; j++) {
			if (e.item->name[j] != e2->item->name[j])
				break;
			add_prefix_entry(&prefix_map->map, e.name, j + 1,
					 NULL);
		}

		/* e2 no longer refers to a unique prefix */
		if (j < prefix_map->max_length && e2->name[j]) {
			/* found a new unique prefix for e2's item */
			e2->item->prefix_length = j + 1;
			add_prefix_entry(&prefix_map->map, e2->name, j + 1,
					 e2->item);
		}
		else
			e2->item->prefix_length = 0;
		e2->item = NULL;

		if (j < prefix_map->max_length && e.name[j]) {
			/* found a unique prefix for the item */
			e.item->prefix_length = j + 1;
			add_prefix_entry(&prefix_map->map, e.name, j + 1,
					 e.item);
		} else
			/* item has no (short enough) unique prefix */
			e.item->prefix_length = 0;

		break;
	}
}

void find_unique_prefixes(struct prefix_item **array, size_t nr,
			  size_t min_length, size_t max_length)
{
	size_t i;
	struct prefix_map prefix_map;

	init_prefix_map(&prefix_map, min_length, max_length);
	for (i = 0; i < nr; i++)
		add_prefix_item(&prefix_map, array[i]);
	hashmap_free(&prefix_map.map, 1);
}