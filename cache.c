#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include "cache.h"
#include "print_helpers.h"

cache_t *make_cache(int capacity, int block_size, int assoc, enum protocol_t protocol, bool lru_on_invalidate_f) {
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

  // allocate lines: number of lines / sets times the size of the container for each line
  // the container is a pointer to cache_line_t, as each line points to an array representing the 'ways'
  cache->lines = malloc(cache->n_set * sizeof(cache_line_t*));

  // then, for each set, allocate the way(s) associated with it
  for (int i = 0; i < cache->n_set; i++) {
    cache->lines[i] = malloc(cache->assoc * sizeof(cache_line_t)); // assoc represents the number of ways, cache_line_t is the actual line
  }

  // create an array of lru counters: each set needs its own lru counter, which is just an int
  cache->lru_way = malloc(cache->n_set * sizeof(int));

  // then, clear each way's lru counter manually: could use calloc but ah well
  for (int i = 0; i < cache->n_set; i++) {
    cache->lru_way[i] = 0;
  }

  // initializes cache tags to 0, dirty bits to false,
  // state to INVALID, and LRU bits to 0
  // FIX THIS CODE!

  // for every set,
  for (int i = 0; i < cache->n_set; i++) {
    // for every way in the set,
    for (int j = 0; j < cache->assoc; j++) {
      // clear tag, set dirty bits, and reset the state
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
  unsigned long tag_mask = (1 << cache->n_tag_bit) - 1; // produces n_tag_bit ones

  // shift the stuff which isn't index or offset (necessarily the tag) into the LSBs
  addr = addr >> (cache->n_index_bit + cache->n_offset_bit); 


  return (addr & tag_mask); // and with tag_mask to obtain the n_tag_bit bits corresponding to the tag
}

/* Given a configured cache, returns the index portion of the given address.
 *
 * Example: a cache with 4 bits each in tag, index, offset
 * in binary -- get_cache_index(0b111101010001) returns 0b0101
 * in decimal -- get_cache_index(3921) returns 5
 */
unsigned long get_cache_index(cache_t *cache, unsigned long addr) {
  unsigned long index_mask = 1 << cache->n_index_bit; // shift 1 by n_index_bit bits
  index_mask -= 1; // result is n_index_bit set bits
  addr = addr >> cache->n_offset_bit; // shift the stuff which isn't offset into the LSBs

  return addr & index_mask; // and with index_mask to obtain the n_index_bit bits corresponding to the index
  // the tag is ignored since only the lower bits of index_mask are set, which will ignore the index.
}

/* Given a configured cache, returns the given address with the offset bits zeroed out.
 *
 * Example: a cache with 4 bits each in tag, index, offset
 * in binary -- get_cache_block_addr(0b111101010001) returns 0b111101010000
 * in decimal -- get_cache_block_addr(3921) returns 3920
 */
unsigned long get_cache_block_addr(cache_t *cache, unsigned long addr) {
  unsigned long offset_mask = (1 << cache->n_offset_bit) - 1; // this yields n_offset_bit ones

  // inverting offset_mask yields ones in everything but the offset bits, and ANDing with addr clears the offset bits
  unsigned long block_addr = addr & ~offset_mask; 
  
  return block_addr; // return the block address
}


//helper 1: handle no coherence protocol
bool handle_no_coherence_protocol(cache_t *cache, unsigned long addr, enum action_t action) {
  unsigned long index = get_cache_index(cache, addr); // obtain target index
  unsigned long tag = get_cache_tag(cache, addr); // obtain target tag
  
  int way = cache->lru_way[index]; // tracks which way our line is in. starts here to make lru updating easier. 
  bool hit = false; // flag to indicate whether we got a hit
  bool writeback_f = false; // flag to indicate whether to writeback

  // Search for the address in the cache: we already know the set, now search the ways:
  for (int i = 0; i < cache->assoc; i++) {
    // if the line's tag matches,
    if (cache->lines[index][i].tag == tag) {
      // set the hit flag, update the way, and exit the loop
      hit = true;
      way = i;
      break;
    }
  }

  // get a pointer to the line we found for easier operations
  cache_line_t *line = &cache->lines[index][way];
  
  // log the way and index
  log_way(way);
  log_set(index);

  // we only actually got a hit if the line was valid as well
  hit = hit && (line->state == VALID); // update hit status

  if (hit) {
    // Cache hit
    // Update LRU since hit, if the action is from an active core
    if (action == STORE || action == LOAD) {
      cache->lru_way[index] = (way + 1) % cache->assoc;
    }

    // if the action was a store, update the dirty bit as well
    if (action == STORE) {
      line->dirty_f = true;
    }
    // then, update the stats
    update_stats(cache->stats, true, writeback_f, false, action);
  } else {
    // Cache miss

    // Requires writeback if dirty
    writeback_f = line->dirty_f;

    if (action == LOAD) {
      line->dirty_f = false; // clear dirty bit if loading: brought into cache, but not modified
    } else if (action == STORE) {
      line->dirty_f = true; // set dirty bit if storing: emulates bringing into cache and writing
    }
    // do nothing to dirty bit on LD_MISS or ST_MISS

    update_stats(cache->stats, false, writeback_f, false, action);

    // On action from active core, update LRU_way, cacheTags, state, dirty flags
    if (action == LOAD || action == STORE) {
      line->tag = tag;
      line->state = VALID; 
      cache->lru_way[index] = (way + 1) % cache->assoc;
    }
  }

  return hit;
}

//helper 2: handle VI protocol
bool handle_vi_protocol(cache_t *cache, unsigned long addr, enum action_t action) {
  unsigned long index = get_cache_index(cache, addr); // obtain target index
  unsigned long tag = get_cache_tag(cache, addr); // obtain target tag
  
  int way = cache->lru_way[index]; // tracks which way our line is in. starts here to make lru updating easier. 
  bool hit = false; // flag to indicate whether we got a hit
  bool writeback_f = false; // flag to indicate whether to writeback

  

  // Search for the address in the cache
  for (int i = 0; i < cache->assoc; i++) {
    // if the tag matches and the line is valid,
    if (cache->lines[index][i].tag == tag) {
      if (cache->lines[index][i].state == VALID) {
          // update the hit flag, set the way we found, and exit
          hit = true;
          way = i;
          break;
      }
      
    }
  }

  // get a pointer to the line we found for easier operations
  cache_line_t *line = &cache->lines[index][way];

  // log the way and index
  log_way(way);
  log_set(index);
  
  if (hit) {
    // Cache hit

    // On load or store from active core,
    // Update the LRU way for the current cache index
    if (action == LOAD) {
      cache->lru_way[index] = (way + 1) % cache->assoc;
    } else if (action == STORE) {
      line->dirty_f = true; // on store, additionally update dirty bit
      cache->lru_way[index] = (way + 1) % cache->assoc;
    } else {
      // LD_MISS or ST_MISS
      //! potentially not required?
      bool dirty = line->dirty_f; // check if dirty

      line->state = INVALID; // invalidate 
      hit = false; // set hit to false

      // if the line was dirty,
      if (dirty) {
        writeback_f = true; // require writeback
        line->dirty_f = false; // after writeback, the line is no longer dirty
      }
    }

    // then, update the stats
    update_stats(cache->stats, hit, writeback_f, false, action);
  
  } else {
    // Cache miss
    writeback_f = line->dirty_f; // if dirty, requires writeback

    // if active core operation, bring thing into cache and set LRU way.
    if (action == LOAD) {
      line->state = VALID;
      line->tag = tag;
      cache->lru_way[index] = (way + 1) % cache->assoc;
    } else if (action == STORE) {
      line->state = VALID;
      line->tag = tag;
      line->dirty_f = true; // additionally set dirty: brought into cache and written
      cache->lru_way[index] = (way + 1) % cache->assoc;
    } else {
      // otherwise, LD_MISS or ST_MISS
      // check if dirty
      bool dirty = line->dirty_f;
      line->state = INVALID; // if not dirty, just invalidate
      if (dirty){
        // otherwise, requires writeback
        // Writeback
        writeback_f = true;
        line->dirty_f = false;
      }
    }
    // then, update the stats
    update_stats(cache->stats, hit, writeback_f, false, action);
  }
  return hit;

}

//helper 3: handle MSI protocol
bool handle_msi_protocol(cache_t *cache, unsigned long addr, enum action_t action) {
  unsigned long index = get_cache_index(cache, addr); // obtain target index
  unsigned long tag = get_cache_tag(cache, addr); // obtain target tag
  
  int way = cache->lru_way[index]; // tracks which way our line is in. starts here to make lru updating easier. 
  bool hit = false; // flag to indicate whether we got a hit
  bool writeback_f = false; // flag to indicate whether to writeback

  bool upgrade_miss = false; // flag to indicate whether an upgrade miss occurred

  // Search for the address in the cache
  for (int i = 0; i < cache->assoc; i++) {
    // if the tag matches and the line is valid,
    if (cache->lines[index][i].tag == tag) {
      if (cache->lines[index][i].state != INVALID){
          // set the hit flag, update the way, and exit the loop
          hit = true;
          way = i;
          break;
      }
    }
  }

// get a pointer to the line we found for easier operations
  cache_line_t *line = &cache->lines[index][way];

  // log the way and index
  log_way(way);
  log_set(index);

  if (hit) {
    // Cache hit
    if (action == LOAD) {
      // if the line was modified or shared, change state to shared
      if (line->state == MODIFIED || line->state == SHARED) {
        line->state = SHARED;
      }
      cache->lru_way[index] = (way + 1) % cache->assoc; // update LRU way
    } else if (action == STORE) {
      if (line->state == SHARED) {
        // if line was shared, upgrade miss
        line->state = MODIFIED;
        hit = false;
        upgrade_miss = true;
      }
      //! not using dirty bit: correct?
    } else if (action == ST_MISS) {
      // Cache miss
      bool dirty = (line->state == MODIFIED); // if modified, was dirty
      if (dirty) {
        writeback_f = true;
        line->state = INVALID;
      }
    } else if (action == LD_MISS) {
      // Cache miss
      bool dirty = (line->state == MODIFIED); // if modified, was dirty
      if (dirty) {
        writeback_f = true;
        line->state = SHARED;
      }
    }
    // then, update the stats
    update_stats(cache->stats, hit, writeback_f, upgrade_miss, action);
  }
  else {
    // Cache miss
    writeback_f = (line->state == MODIFIED); // if modified, requires writeback
    bool dirty = line->state == MODIFIED;
    int way = cache->lru_way[index];

    if (action == LOAD) {
      // if from a load,
      if (dirty) {
        writeback_f = true; // if dirty, requires writeback
      }
      line->state = SHARED; // update MSI state

      // update tag and LRU
      line->tag = tag; 
      cache->lru_way[index] = (way + 1) % cache->assoc;
    } else if (action == STORE) {
      // if from a store,
      if (dirty) {
        writeback_f = true; // if dirty, requires writeback
      }
      line->state = MODIFIED; // update MSI state

      // update tag and LRU
      line->tag = tag;
      cache->lru_way[index] = (way + 1) % cache->assoc;
    }
    // then, update the stats
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

