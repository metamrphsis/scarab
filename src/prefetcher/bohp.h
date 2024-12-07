// bohp.h

#ifndef __BOHP_H__
#define __BOHP_H__

#include <stdbool.h>
#include <stdint.h>
#include "bohp.param.h"
#include "globals/global_types.h"
#include "libs/hash_lib.h"  // Added to utilize Hash_Table
#include "pref_common.h"
#include "pref_stream.h"

struct HWP_struct;
struct HWP_Info_struct;

typedef struct BOHP_Entry_Struct {
  int offset;
  int score;
} BOHP_Entry;

typedef struct BOHP_Learning_Struct {
  int         blocks_in_page;
  BOHP_Entry* offset_list;
  int         list_size;

  int  round;
  int  best_score;
  int  index_to_test;
  int  local_best_offset;
  int  global_best_offset;
  int  SCORE_MAX;
  int  ROUND_MAX;
  int  BAD_SCORE;
  bool debug;
} BOHP_Learning;

// Updated BOHP_RecentRequestsTable_Struct to use Hash_Table and circular queue
typedef struct BOHP_RecentRequestsTable_Struct {
  Hash_Table table;           // Hash table for recent requests
  uint64_t*  queue;           // Circular queue to track insertion order
  int        queue_size;      // Current number of entries in the queue
  int        queue_capacity;  // Maximum capacity of the queue
  int        queue_head;      // Index for the next insertion
  bool       debug;           // Debug flag
} BOHP_RecentRequestsTable;

typedef struct BOHP_Struct {
  HWP_Info*                hwp_info;
  BOHP_Learning            best_offset_learning;
  BOHP_RecentRequestsTable recent_requests_table;  // Updated to new structure
  int                      prefetch_offset;
  bool                     debug;
} BOHP;

typedef struct BOHP_Core_Struct {
  BOHP* pref_bohp_core_ul1;
  BOHP* pref_bohp_core_umlc;
} BOHP_Core;

// Function Declarations
void bohp_init(HWP* hwp);

void bohp_ul1_miss(uns8 proc_id, Addr lineAddr, Addr loadPC, uns32 global_hist);

void bohp_ul1_hit(uns8 proc_id, Addr lineAddr, Addr loadPC, uns32 global_hist);

void bohp_umlc_miss(uns8 proc_id, Addr lineAddr, Addr loadPC,
                    uns32 global_hist);

void bohp_umlc_hit(uns8 proc_id, Addr lineAddr, Addr loadPC, uns32 global_hist);

void init_bohp_core(HWP* hwp, BOHP* pref_bohp_core);

void bohp_train(BOHP* pref_bohp, uns8 proc_id, Addr lineAddr, Addr loadPC,
                uns32 global_hist, Flag create, Flag is_mlc);

int bohp_train_create_buffer(BOHP* pref_bohp, uns8 proc_id, Addr line_index,
                             Flag train, Flag create, int extra_dis);

void bohp_throttle(BOHP* pref_bohp, uns8 proc_id);

void bohp_throttle_fb(BOHP* pref_bohp, uns8 proc_id);

float bohp_acc_getacc(BOHP* pref_bohp, int index, float pref_acc);

#endif /* __BOHP_H__ */
