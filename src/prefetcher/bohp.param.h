// bohp.param.h

#ifndef __BOHP_PARAM_H__
#define __BOHP_PARAM_H__

#include "globals/global_types.h"

#define DEF_PARAM(name, variable, type, func, def, const) \
    extern const type variable;
#include "bohp.param.def"
#undef DEF_PARAM

#endif /* __BOHP_PARAM_H__ */
