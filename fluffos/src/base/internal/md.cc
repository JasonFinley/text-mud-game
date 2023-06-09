/*
   note: do not use MALLOC() etc. in this module.

   This module introduces quite a lot of overhead but it can be useful
   for tracking down memory leaks or for catching the freeing on non-malloc'd
   data.  This module could easily be extended to allow the malloced memory
   chunks to be tagged with a string label.
*/

#include "base/internal/md.h"

#include <cassert>  // for assert()
#include <cstdlib>  // malloc etc
#include <cinttypes>

#include "base/internal/log.h"
#include "base/internal/stralloc.h"

uint64_t blocks[MAX_TAGS];
uint64_t totals[MAX_TAGS];

int malloc_mask = 0;

md_node_t *table[MD_TABLE_SIZE];
unsigned int total_malloced = 0L;
unsigned int hiwater = 0L;

// TODO: defined in backend.cc
extern uint64_t g_current_gametick;

void MDmalloc(md_node_t *node, int size, int tag, const char *desc) {
  unsigned long h;
  static int count = 0;

  assert(size > 0);

  total_malloced += size;
  if (total_malloced > hiwater) {
    hiwater = total_malloced;
  }
  h = MD_HASH(node);
  node->size = size;
  node->next = table[h];
#ifdef DEBUGMALLOC_EXTENSIONS
  if ((tag & 0xff) > MAX_CATEGORY) {
    totals[tag & 0xff] += size;
    blocks[tag & 0xff]++;
  }
  auto cat = tag >> 8;
  if (cat != tag) {
    totals[cat & 0xff] += size;
    blocks[cat & 0xff]++;
  }
  node->tag = tag;
  node->id = count++;
  node->desc = desc ? desc : "default";
  node->gametick = g_current_gametick;

  assert(desc != nullptr);

  if (malloc_mask == node->tag) {
    debug_message("%d MDmalloc: %5d, [%-25s], %p:(%d)\n", node->id, node->tag, node->desc,
                  PTR(node), node->size);
  }
#endif
  table[h] = node;
}

#ifdef DEBUGMALLOC_EXTENSIONS
void set_tag(const void *ptr, int tag) {
  md_node_t *node = PTR_TO_NODET(ptr);

  if ((node->tag & 0xff) > MAX_CATEGORY) {
    totals[node->tag & 0xff] -= node->size;
    blocks[node->tag & 0xff]--;
  }
  auto cat = node->tag >> 8;
  if (cat != node->tag) {
    totals[cat & 0xff] -= node->size;
    blocks[cat & 0xff]--;
  }

  node->tag = tag;
  if ((node->tag & 0xff) > MAX_CATEGORY) {
    totals[node->tag & 0xff] += node->size;
    blocks[node->tag & 0xff]++;
  }
  cat = node->tag >> 8;
  if (cat != node->tag) {
    totals[cat & 0xff] += node->size;
    blocks[cat & 0xff]++;
  }
}
#endif

int MDfree(md_node_t *ptr) {
  unsigned long h;
  md_node_t *entry, **oentry;

  h = MD_HASH(ptr);
  oentry = &table[h];
  for (entry = *oentry; entry; oentry = &entry->next, entry = *oentry) {
    if (entry == ptr) {
      *oentry = entry->next;
      total_malloced -= entry->size;
      break;
    }
  }
  if (entry) {
#ifdef DEBUGMALLOC_EXTENSIONS
    if ((entry->tag & 0xff) > MAX_CATEGORY) {
      totals[entry->tag & 0xff] -= entry->size;
      blocks[entry->tag & 0xff]--;
    }
    auto cat = entry->tag >> 8;
    if (cat != entry->tag) {
      totals[cat & 0xff] -= entry->size;
      blocks[cat & 0xff]--;
    }
    if (malloc_mask == entry->tag) {
      debug_message("%d MDfree: %5d, [%-25s], %8lx:(%d)\n", entry->id, entry->tag, entry->desc,
                    (uintptr_t)PTR(entry), entry->size);
    }
#endif
  } else {
    debug_message("md: debugmalloc: attempted to free non-malloc'd pointer %08lx\n",
                  (uintptr_t)ptr);
#ifdef DEBUG
    abort();
#endif
    return 0;
  }
  return 1;
}

void set_malloc_mask(int mask) { malloc_mask = mask; }
