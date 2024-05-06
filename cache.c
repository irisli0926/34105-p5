#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include "cache.h"
#include "print_helpers.h"

cache_t *make_cache(int capacity, int block_size, int assoc, enum protocol_t protocol, bool lru_on_invalidate_f){
  cache_t *cache = malloc(sizeof(cache_t));
  cache->stats = make_cache_stats();
  
  cache->capacity = capacity;      // in Bytes
  cache->block_size = block_size;  // in Bytes
  cache->assoc = assoc;            // 1, 2, 3... etc.

  // FIX THIS CODE!
  // first, correctly set these 5 variables. THEY ARE ALL WRONG
  // note: you may find math.h's log2 function useful
  cache->n_cache_line = capacity / block_size;
  cache->n_set = capacity / (block_size * assoc);
  cache->n_offset_bit = log2(block_size);
  cache->n_index_bit = log2(cache->n_set);
  cache->n_tag_bit = 32 - cache->n_index_bit - cache->n_offset_bit;

  // next create the cache lines and the array of LRU bits
  // - malloc an array with n_rows
  // - for each element in the array, malloc another array with n_col
  // FIX THIS CODE!

  cache->lines = malloc(cache->n_set * sizeof(cache_line_t*));
  for (int i = 0; i < cache->n_set; i++) {
    cache->lines[i] = malloc(cache->assoc * sizeof(cache_line_t));
  }
  cache->lru_way = malloc(cache->n_set * sizeof(int));
  for (int i = 0; i < cache->n_set; i++) {
    cache->lru_way[i] = 0;
  }

  // initializes cache tags to 0, dirty bits to false,
  // state to INVALID, and LRU bits to 0
  // FIX THIS CODE!
 for (int i = 0; i < cache->n_set; i++) {
    for (int j = 0; j < cache->assoc; j++) {
      cache->lines[i][j].tag = 0;
      cache->lines[i][j].dirty_f = false;
      cache->lines[i][j].state = INVALID;
    }
  }

  cache->protocol = protocol;
  cache->lru_on_invalidate_f = lru_on_invalidate_f;
  
  return cache;
}

/* Given a configured cache, returns the tag portion of the given address.
 *
 * Example: a cache with 4 bits each in tag, index, offset
 * in binary -- get_cache_tag(0b111101010001) returns 0b1111
 * in decimal -- get_cache_tag(3921) returns 15 
 */
unsigned long get_cache_tag(cache_t *cache, unsigned long addr) {
  // FIX THIS CODE!
  unsigned long tag_mask = (1 << cache->n_tag_bit) - 1;
  addr = addr >> (cache->n_index_bit + cache->n_offset_bit);
  return (addr & tag_mask);
}

/* Given a configured cache, returns the index portion of the given address.
 *
 * Example: a cache with 4 bits each in tag, index, offset
 * in binary -- get_cache_index(0b111101010001) returns 0b0101
 * in decimal -- get_cache_index(3921) returns 5
 */
unsigned long get_cache_index(cache_t *cache, unsigned long addr) {
  // FIX THIS CODE!
  unsigned long index_mask = 1 << cache->n_index_bit;
  index_mask -= 1; // result is n_index_bit set bits
  addr = addr >> cache->n_offset_bit;

  return addr & index_mask;
}

/* Given a configured cache, returns the given address with the offset bits zeroed out.
 *
 * Example: a cache with 4 bits each in tag, index, offset
 * in binary -- get_cache_block_addr(0b111101010001) returns 0b111101010000
 * in decimal -- get_cache_block_addr(3921) returns 3920
 */
unsigned long get_cache_block_addr(cache_t *cache, unsigned long addr) {
  // FIX THIS CODE!
  unsigned long offset_mask = (1 << cache->n_offset_bit) - 1;
  unsigned long block_addr = addr & ~offset_mask;
  
  return block_addr;
}


//helper 1: handle no coherence protocol
bool handle_no_coherence_protocol(cache_t *cache, unsigned long addr, enum action_t action) {
  unsigned long index = get_cache_index(cache, addr);
  unsigned long tag = get_cache_tag(cache, addr);
  int way = cache->lru_way[index];
  bool hit = false;
  bool writeback_f = false;

  // Search for the address in the cache
  for (int i = 0; i < cache->assoc; i++) {
    if (cache->lines[index][i].tag == tag) {
      hit = true;
      way = i;
      break;
    }
  }
  log_way(way);
  log_set(index);

  cache_line_t *line = &cache->lines[index][way];
  hit = hit && (line->state == VALID); // hit status
  if (hit) {
    // Cache hit
    // Update LRU since hit
    if (action == STORE || action == LOAD){
      cache->lru_way[index] = (way + 1) % cache->assoc;
    }
    

    if (action == STORE) {
      line->dirty_f = true;
    }

    update_stats(cache->stats, true, writeback_f, false, action);
  } else {
    // Cache miss

    // Assume writeback if dirty
    writeback_f = line->dirty_f;

    if (action == LOAD) {
      line->dirty_f = false;
    } else if (action == STORE) {
      line->dirty_f = true;
    }
    update_stats(cache->stats, false, writeback_f, false, action);

    // Update LRU_way, cacheTags, state, dirty flags
    if (action == LOAD || action == STORE){
      line->tag = tag;
      line->state = VALID; 
      cache->lru_way[index] = (way + 1) % cache->assoc;
    }
  }

  return hit;
}

//helper 2: handle VI protocol
bool handle_vi_protocol(cache_t *cache, unsigned long addr, enum action_t action) {
  unsigned long index = get_cache_index(cache, addr);
  unsigned long tag = get_cache_tag(cache, addr);
  int way = cache->lru_way[index];
  bool hit = false;
  bool writeback_f = false;

  

  // Search for the address in the cache
  for (int i = 0; i < cache->assoc; i++) {
    if (cache->lines[index][i].tag == tag) {
      if (cache->lines[index][i].state == VALID){
          hit = true;
          way = i;
          break;
      }
      
    }
  }

  cache_line_t *line = &cache->lines[index][way];

  log_way(way);
  log_set(index);
  if (hit) {
    // Cache hit

    // Update the LRU way for the current cache index
    // Get a pointer to the cache line for the current cache index and way
    if (action == LOAD) {
      cache->lru_way[index] = (way + 1) % cache->assoc;
    } else if (action == STORE) {
      line->dirty_f = true;
      cache->lru_way[index] = (way + 1) % cache->assoc;
    } else {
      // Cache miss
      // check if dirty
      bool dirty = line->dirty_f;
      line->state = INVALID;
      hit = false;
      if (dirty) {
        // Writeback
        writeback_f = true;
        line->dirty_f = false;
      }
    }

    update_stats(cache->stats, hit, writeback_f, false, action);
  
  } else {
    // Cache miss
    cache_line_t *line = &cache->lines[index][way];
    writeback_f = line->dirty_f;
    if (action == LOAD) {
      line->state = VALID;
      line->tag = tag;
      cache->lru_way[index] = (way + 1) % cache->assoc;
    } else if (action == STORE) {
      line->state = VALID;
      line->tag = tag;
      line->dirty_f = true;
      cache->lru_way[index] = (way + 1) % cache->assoc;
    } else {
      // check if dirty
      bool dirty = line->dirty_f;
      if (!dirty) {
        line->state = INVALID;
      } else {
        // Writeback
        writeback_f = true;
        line->dirty_f = false;
      }
    }
    update_stats(cache->stats, hit, writeback_f, false, action);
  }
  return hit;

}

//helper 3: handle MSI protocol
bool handle_msi_protocol(cache_t *cache, unsigned long addr, enum action_t action) {
  unsigned long index = get_cache_index(cache, addr);
  unsigned long tag = get_cache_tag(cache, addr);
  int way = cache->lru_way[index];
  bool hit = false;
  bool writeback_f = false;

  bool upgrade_miss = false;

  // Search for the address in the cache
  for (int i = 0; i < cache->assoc; i++) {
    if (cache->lines[index][i].tag == tag) {
      hit = true;
      way = i;
      break;
    }
  }

  log_way(way);
  log_set(index);

  if (hit) {
    // Cache hit
    cache_line_t *line = &cache->lines[index][way];
    if (action == LOAD) {
      if (line->state == MODIFIED || line->state == SHARED) {
        line->state = SHARED;
      }
      cache->lru_way[index] = (way + 1) % cache->assoc;
    } else if (action == STORE) {
      if (line->state == MODIFIED || line->state == SHARED) {
        line->state = MODIFIED;
        //upgrade miss
        hit = false;
        upgrade_miss = true;
      }
    } else if (action == ST_MISS) {
      // Cache miss
      bool dirty = (line->state == MODIFIED);
      if (dirty) {
        writeback_f = true;
        line->state = INVALID;
      }
    } else if (action == LD_MISS) {
      // Cache miss
      bool dirty = line->state == MODIFIED;
      if (dirty) {
        writeback_f = true;
        line->state = SHARED;
      }
    }
    update_stats(cache->stats, hit, writeback_f, upgrade_miss, action);
  }
  else {
    // Cache miss
    cache_line_t *line = &cache->lines[index][way];
    writeback_f = (line->state == MODIFIED);
    bool dirty = line->state == MODIFIED;

    if (action == LOAD) {
      if (dirty) {
        writeback_f = true;
      }
      line->state = SHARED;
      line->tag = tag;
      cache->lru_way[index] = (way + 1) % cache->assoc;
    } else if (action == STORE) {
      if (dirty) {
        writeback_f = true;
      }
      line->state = MODIFIED;
      line->tag = tag;
      cache->lru_way[index] = (way + 1) % cache->assoc;
    } else {
      line->state = INVALID;
    }
    update_stats(cache->stats, hit, writeback_f, false, action);
  }
  return hit;
}



/* this method takes a cache, an address, and an action
 * it proceses the cache access. functionality in no particular order: 
 *   - look up the address in the cache, determine if hit or miss
 *   - update the LRU_way, cacheTags, state, dirty flags if necessary
 *   - update the cache statistics (call update_stats)
 * return true if there was a hit, false if there was a miss
 * Use the "get" helper functions above. They make your life easier.
 */
bool access_cache(cache_t *cache, unsigned long addr, enum action_t action) {
  if (cache->protocol == NONE) {
    return handle_no_coherence_protocol(cache, addr, action);
  } else if (cache->protocol == VI) {
    return handle_vi_protocol(cache, addr, action);
  } else if (cache->protocol == MSI) {
    return handle_msi_protocol(cache, addr, action);
  }
}

