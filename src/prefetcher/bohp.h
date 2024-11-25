// bohp.h

#ifndef __BOHP_H__
#define __BOHP_H__

#include <stdint.h>
#include "globals/global_types.h"
#include "pref_common.h"
#include "pref_stream.h"
#include "bohp.param.h"
#include <stdbool.h>


struct HWP_struct;
struct HWP_Info_struct;

typedef struct BOHP_Entry_Struct {
    int offset;
    int score;
} BOHP_Entry;

typedef struct BOHP_Learning_Struct {
    int blocks_in_page;
    BOHP_Entry* offset_list;
    int list_size;

    int round;
    int best_score;
    int index_to_test;
    int local_best_offset;
    int global_best_offset;
    int SCORE_MAX;
    int ROUND_MAX;
    int BAD_SCORE;
    bool debug;
} BOHP_Learning;


typedef struct BOHP_RecentRequestsEntry_Struct {
    uint64_t addr;
} BOHP_RecentRequestsEntry;


typedef struct BOHP_RecentRequestsTable_Struct {
    int size;
    BOHP_RecentRequestsEntry* entries;
    bool debug;
} BOHP_RecentRequestsTable;


typedef struct BOHP_Struct {
    HWP_Info* hwp_info;
    BOHP_Learning best_offset_learning;
    BOHP_RecentRequestsTable recent_requests_table;
    int prefetch_offset;
    bool debug;
} BOHP;

typedef struct BOHP_Core_Struct {
    BOHP* pref_bohp_core_ul1;
    BOHP* pref_bohp_core_umlc;
} BOHP_Core;

void bohp_init(HWP* hwp);


void bohp_ul1_miss(uns8 proc_id, Addr lineAddr, Addr loadPC,
                  uns32 global_hist);

void bohp_ul1_hit(uns8 proc_id, Addr lineAddr, Addr loadPC,
                 uns32 global_hist);

void bohp_umlc_miss(uns8 proc_id, Addr lineAddr, Addr loadPC,
                  uns32 global_hist);

void bohp_umlc_hit(uns8 proc_id, Addr lineAddr, Addr loadPC,
                 uns32 global_hist);

void init_bohp_core(HWP* hwp, BOHP* pref_bohp_core);

void bohp_train(BOHP* pref_bohp, uns8 proc_id, Addr lineAddr, Addr loadPC,
               uns32 global_hist, Flag create, Flag is_mlc);

int bohp_train_create_buffer(BOHP* pref_bohp, uns8 proc_id, Addr line_index,
                            Flag train, Flag create,
                            int extra_dis);

void bohp_throttle(BOHP* pref_bohp, uns8 proc_id);

void bohp_throttle_fb(BOHP* pref_bohp, uns8 proc_id);

float bohp_acc_getacc(BOHP* pref_bohp, int index, float pref_acc);

#endif /* __BOHP_H__ */