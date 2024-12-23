// bohp.c

#include "debug/debug_macros.h"
#include "debug/debug_print.h"
#include "globals/global_defs.h"
#include "globals/global_types.h"
#include "globals/global_vars.h"

#include "globals/assert.h"
#include "globals/utils.h"
#include "op.h"

#include "bohp.h"
#include "bohp.param.h"
#include "core.param.h"
#include "debug/debug.param.h"
#include "general.param.h"
#include "libs/hash_lib.h"
#include "libs/list_lib.h"
#include "memory/memory.h"
#include "memory/memory.param.h"
#include "prefetcher/pref.param.h"
#include "prefetcher/pref_common.h"
#include "prefetcher/pref_type.h"
#include "statistics.h"

#define DEBUG_BOHP(proc_id, args...) _DEBUG(proc_id, DEBUG_BOHP_LEVEL, ##args)

BOHP_Core bohp_prefetchers_array;

// Forward declarations
void bohp_learning_init(BOHP_Learning* bohp_learning, int blocks_in_page);
void bohp_recent_requests_table_init(BOHP_RecentRequestsTable* bohp_rrt,
                                     int                       size);
void bohp_recent_requests_table_insert(BOHP_RecentRequestsTable* bohp_rrt,
                                       uint64_t                  addr);
bool bohp_recent_requests_table_find(BOHP_RecentRequestsTable* bohp_rrt,
                                     uint64_t                  addr);

// Function to initialize the learning component
void bohp_learning_init(BOHP_Learning* bohp_learning, int blocks_in_page) {
  bohp_learning->blocks_in_page = blocks_in_page;

  const int predefined_offsets[52] = {
    1,   2,   3,   4,   5,   6,   8,   9,   10,  12,  15,  16,  18,
    20,  24,  25,  27,  30,  32,  36,  40,  45,  48,  50,  54,  60,
    64,  72,  75,  80,  81,  90,  96,  100, 108, 120, 125, 128, 135,
    144, 150, 160, 162, 180, 192, 200, 216, 225, 240, 243, 250, 256};

  bohp_learning->offset_list = (BOHP_Entry*)malloc(sizeof(BOHP_Entry) * 52);
  if(bohp_learning->offset_list == NULL) {
    fprintf(stderr, "Error: Unable to allocate memory for BOHP offset list.\n");
    exit(EXIT_FAILURE);
  }

  bohp_learning->list_size = 52;
  for(int i = 0; i < bohp_learning->list_size; i++) {
    bohp_learning->offset_list[i].offset = predefined_offsets[i];
    bohp_learning->offset_list[i].score  = 0;
  }

  bohp_learning->round              = 0;
  bohp_learning->best_score         = 0;
  bohp_learning->index_to_test      = 0;
  bohp_learning->local_best_offset  = 0;
  bohp_learning->global_best_offset = 1;

  bohp_learning->SCORE_MAX = 31;
  bohp_learning->ROUND_MAX = 100;
  bohp_learning->BAD_SCORE = 1;

  bohp_learning->debug = FALSE;

  STAT_EVENT(0, BOHP_BEST_OFFSET_LEARNING_INITIALIZED);
  return;
}

// Updated initialization function for the recent requests table using
// Hash_Table
void bohp_recent_requests_table_init(BOHP_RecentRequestsTable* bohp_rrt,
                                     int                       size) {
  bohp_rrt->queue_capacity = size;
  bohp_rrt->queue_size     = 0;
  bohp_rrt->queue_head     = 0;
  bohp_rrt->debug          = FALSE;

  // Initialize the hash table
  // Choosing a prime number of buckets close to the expected size for better
  // distribution
  uns hash_buckets = size *
                     2;  // Example: double the size for load factor < 0.5
  init_hash_table(&bohp_rrt->table, "BOHP_RecentRequestsTable", hash_buckets,
                  sizeof(uint64_t));

  // Allocate memory for the circular queue
  bohp_rrt->queue = (uint64_t*)malloc(sizeof(uint64_t) * size);
  if(bohp_rrt->queue == NULL) {
    fprintf(stderr,
            "Error: Unable to allocate memory for Recent Requests Queue.\n");
    exit(EXIT_FAILURE);
  }

  STAT_EVENT(0, BOHP_RECENT_REQUESTS_TABLE_INITIALIZED);
  return;
}

// Updated insert function using Hash_Table and circular queue
void bohp_recent_requests_table_insert(BOHP_RecentRequestsTable* bohp_rrt,
                                       uint64_t                  addr) {
  Flag  new_entry;
  void* existing = hash_table_access_create(&bohp_rrt->table, (int64)addr,
                                            &new_entry);
  if(existing) {}

  if(new_entry) {
    // If the queue is full, remove the oldest entry
    if(bohp_rrt->queue_size == bohp_rrt->queue_capacity) {
      uint64_t oldest_addr = bohp_rrt->queue[bohp_rrt->queue_head];
      // Delete the oldest address from the hash table
      hash_table_access_delete(&bohp_rrt->table, (int64)oldest_addr);
      // Overwrite the oldest entry in the queue
      bohp_rrt->queue[bohp_rrt->queue_head] = addr;
      // Move the head pointer
      bohp_rrt->queue_head = (bohp_rrt->queue_head + 1) %
                             bohp_rrt->queue_capacity;
    } else {
      // If the queue is not full, simply add the new address
      bohp_rrt->queue[bohp_rrt->queue_head] = addr;
      bohp_rrt->queue_head                  = (bohp_rrt->queue_head + 1) %
                             bohp_rrt->queue_capacity;
      bohp_rrt->queue_size++;
    }
  }

  STAT_EVENT(0, BOHP_RECENT_REQUEST_INSERTED);
  return;
}

// Updated find function using Hash_Table
bool bohp_recent_requests_table_find(BOHP_RecentRequestsTable* bohp_rrt,
                                     uint64_t                  addr) {
  void* data = hash_table_access(&bohp_rrt->table, (int64)addr);
  if(data != NULL) {
    STAT_EVENT(0, BOHP_RECENT_REQUEST_FOUND);
    return TRUE;
  } else {
    STAT_EVENT(0, BOHP_RECENT_REQUEST_NOT_FOUND);
    return FALSE;
  }
}

// Rest of the functions remain largely unchanged, except for the recent
// requests table operations

void bohp_init(HWP* hwp) {
  if(!BOHP_ON) {
    return;
  }

  hwp->hwp_info->enabled = TRUE;

  bohp_prefetchers_array.pref_bohp_core_ul1  = (BOHP*)malloc(sizeof(BOHP) *
                                                             NUM_CORES);
  bohp_prefetchers_array.pref_bohp_core_umlc = (BOHP*)malloc(sizeof(BOHP) *
                                                             NUM_CORES);

  if(bohp_prefetchers_array.pref_bohp_core_ul1 == NULL ||
     bohp_prefetchers_array.pref_bohp_core_umlc == NULL) {
    fprintf(stderr,
            "Error: Unable to allocate memory for BOHP prefetcher cores.\n");
    exit(EXIT_FAILURE);
  }

  for(uns8 proc_id = 0; proc_id < NUM_CORES; proc_id++) {
    init_bohp_core(hwp, &bohp_prefetchers_array.pref_bohp_core_ul1[proc_id]);
    init_bohp_core(hwp, &bohp_prefetchers_array.pref_bohp_core_umlc[proc_id]);
  }

  STAT_EVENT(0, BOHP_PREFETCHER_INITIALIZED);
  return;
}

void init_bohp_core(HWP* hwp, BOHP* pref_bohp_core) {
  pref_bohp_core->hwp_info = hwp->hwp_info;

  pref_bohp_core->best_offset_learning.offset_list = NULL;
  pref_bohp_core->best_offset_learning.list_size   = 0;
  bohp_learning_init(&pref_bohp_core->best_offset_learning,
                     BOHP_BLOCKS_IN_PAGE);

  // Initialize the recent requests table with a fixed size
  bohp_recent_requests_table_init(&pref_bohp_core->recent_requests_table,
                                  BOHP_RECENT_REQ_TABLE_SIZE);

  pref_bohp_core->prefetch_offset = 1;

  pref_bohp_core->debug = BOHP_DEBUG_MODE;

  STAT_EVENT(0, BOHP_CORE_INITIALIZED);
  return;
}

void bohp_train(BOHP* bohp_train, uns8 proc_id, Addr lineAddr, Addr loadPC,
                uns32 global_hist, Flag create, Flag is_mlc) {
  Addr     block_number = lineAddr >> LOG2(BOHP_BLOCK_SIZE);
  uint64_t addr         = block_number;

  bohp_recent_requests_table_insert(&bohp_train->recent_requests_table, addr);
  STAT_EVENT(proc_id, BOHP_RECENT_REQUEST_INSERTED);

  BOHP_Learning bohp_learning  = bohp_train->best_offset_learning;
  int           best_offset    = bohp_learning.global_best_offset;
  uint64_t      prefetch_block = block_number + best_offset;

  if((prefetch_block % bohp_learning.blocks_in_page) <
     bohp_learning.blocks_in_page) {
    if(bohp_recent_requests_table_find(&bohp_train->recent_requests_table,
                                       prefetch_block)) {
      STAT_EVENT(proc_id, BOHP_PREFETCH_USEFUL);
      bohp_learning.best_score++;
      STAT_EVENT(proc_id, BOHP_BEST_SCORE_UPDATED);
      if(bohp_learning.best_score > bohp_learning.SCORE_MAX) {
        bohp_learning.global_best_offset = best_offset;
        bohp_learning.best_score         = 0;
        STAT_EVENT(proc_id, BOHP_NEW_BEST_OFFSET);
      }
    } else {
      STAT_EVENT(proc_id, BOHP_PREFETCH_NOT_USEFUL);
      bohp_learning.best_score--;
      if(bohp_learning.best_score < bohp_learning.BAD_SCORE) {
        bohp_learning.global_best_offset = 0;
        bohp_learning.best_score         = 0;
        STAT_EVENT(proc_id, BOHP_PREFETCH_DISABLED);
      }
    }
  }

  if(bohp_learning.list_size > 0) {
    int tested_offset =
      bohp_learning.offset_list[bohp_learning.index_to_test].offset;
    uint64_t test_prefetch = block_number + tested_offset;

    if((test_prefetch % bohp_learning.blocks_in_page) <
       bohp_learning.blocks_in_page) {
      if(bohp_recent_requests_table_find(&bohp_train->recent_requests_table,
                                         test_prefetch)) {
        bohp_learning.offset_list[bohp_learning.index_to_test].score++;
        STAT_EVENT(proc_id, BOHP_OFFSET_SCORE_UPDATED);
        if(bohp_learning.offset_list[bohp_learning.index_to_test].score >
           bohp_learning.best_score) {
          bohp_learning.best_score =
            bohp_learning.offset_list[bohp_learning.index_to_test].score;
          bohp_learning.local_best_offset =
            bohp_learning.offset_list[bohp_learning.index_to_test].offset;
          STAT_EVENT(proc_id, BOHP_LOCAL_BEST_OFFSET_UPDATED);
        }
      } else {
        bohp_learning.offset_list[bohp_learning.index_to_test].score--;
        STAT_EVENT(proc_id, BOHP_OFFSET_SCORE_DECREMENTED);
      }

      bohp_learning.index_to_test = (bohp_learning.index_to_test + 1) %
                                    bohp_learning.list_size;

      if(bohp_learning.index_to_test == 0) {
        bohp_learning.round++;
        STAT_EVENT(proc_id, BOHP_LEARNING_ROUND_COMPLETED);
        if(bohp_learning.round >= bohp_learning.ROUND_MAX ||
           bohp_learning.best_score >= bohp_learning.SCORE_MAX) {
          if(bohp_learning.best_score > bohp_learning.BAD_SCORE) {
            bohp_learning.global_best_offset = bohp_learning.local_best_offset;
            STAT_EVENT(proc_id, BOHP_BEST_OFFSET_UPDATED);
          } else {
            bohp_learning.global_best_offset = 0;
            STAT_EVENT(proc_id, BOHP_PREFETCH_DISABLED);
          }

          for(int i = 0; i < bohp_learning.list_size; i++) {
            bohp_learning.offset_list[i].score = 0;
          }
          bohp_learning.best_score = 0;
          bohp_learning.round      = 0;
        }
      }
    }
  }

  if(bohp_learning.global_best_offset > 0) {
    uint64_t prefetch_addr = (block_number + bohp_learning.global_best_offset)
                             << LOG2(BOHP_BLOCK_SIZE);
    new_mem_req(MRT_DPRF, proc_id, prefetch_addr, BOHP_BLOCK_SIZE, 1, NULL,
                NULL, unique_count, 0);
    STAT_EVENT(proc_id, BOHP_PREFETCH_ISSUED);
  }
  return;
}

void bohp_ul1_miss(uns8 proc_id, Addr lineAddr, Addr loadPC,
                   uns32 global_hist) {
  if(!BOHP_UL1_ON) {
    return;
  }
  bohp_train(&bohp_prefetchers_array.pref_bohp_core_ul1[proc_id], proc_id,
             lineAddr, loadPC, global_hist, TRUE, FALSE);
  STAT_EVENT(proc_id, BOHP_UL1_CACHE_MISS_HANDLED);
  return;
}

void bohp_ul1_hit(uns8 proc_id, Addr lineAddr, Addr loadPC, uns32 global_hist) {
  if(!BOHP_UL1_ON) {
    return;
  }
  bohp_train(&bohp_prefetchers_array.pref_bohp_core_ul1[proc_id], proc_id,
             lineAddr, loadPC, global_hist, FALSE, FALSE);
  STAT_EVENT(proc_id, BOHP_UL1_CACHE_HIT_HANDLED);
  return;
}

void bohp_umlc_miss(uns8 proc_id, Addr lineAddr, Addr loadPC,
                    uns32 global_hist) {
  if(!BOHP_UMLC_ON) {
    return;
  }
  bohp_train(&bohp_prefetchers_array.pref_bohp_core_umlc[proc_id], proc_id,
             lineAddr, loadPC, global_hist, TRUE, TRUE);
  STAT_EVENT(proc_id, BOHP_UMLC_CACHE_MISS_HANDLED);
  return;
}

void bohp_umlc_hit(uns8 proc_id, Addr lineAddr, Addr loadPC,
                   uns32 global_hist) {
  if(!BOHP_UMLC_ON) {
    return;
  }
  bohp_train(&bohp_prefetchers_array.pref_bohp_core_umlc[proc_id], proc_id,
             lineAddr, loadPC, global_hist, FALSE, TRUE);
  STAT_EVENT(proc_id, BOHP_UMLC_CACHE_HIT_HANDLED);
  return;
}

void bohp_throttle(BOHP* bohp_train, uns8 proc_id) {
  float acc = bohp_acc_getacc(bohp_train, 0, 1.0);
  if(acc > BOHP_ACC_THRESH_1) {
    bohp_train->prefetch_offset += 1;
    STAT_EVENT(proc_id, BOHP_THROTTLE_UP);
  } else if(acc < BOHP_ACC_THRESH_2) {
    bohp_train->prefetch_offset -= 1;
    if(bohp_train->prefetch_offset < 1)
      bohp_train->prefetch_offset = 1;
    STAT_EVENT(proc_id, BOHP_THROTTLE_DOWN);
  }
  return;
}

void bohp_throttle_fb(BOHP* bohp_train, uns8 proc_id) {
  bohp_throttle(bohp_train, proc_id);
  return;
}

float bohp_acc_getacc(BOHP* bohp_train, int index, float pref_acc) {
  return pref_acc;
}
