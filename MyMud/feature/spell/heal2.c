/*****************************************************************************
Copyright: 2019, Mud.Ren
File name: heal2.c
Description: 单体治疗魔法贝霍伊米
Author: xuefeng
Version: v1.0
Date: 2019-03-23
History:
*****************************************************************************/
/**
 * 小于咒语固有魔回时恢复直接是[初始值]
 * 大于固有魔回时恢复是[初始值]+(魔回-固有魔回)×增长率
 * [实际恢复值]=[恢复计算值]=(max([初始值],[初始值]+(魔回-固有魔回)×增长率)×蓄力倍率)+(等级/10+1)×蓄力次数)×魔防倍率×属性倍率×其他倍率
 */
nosave mapping spell_info = ([
    "name" : "heal2", // 咒文名称
    "type" : TYPE_H, // 伤害类型
    "range" : RANGE_S, // 作用范围
    "base" : 100, // 基础值(固有魔回)
    "min"  : 75, // 最小伤害
    "rand" : 21, // 最大波动值 75 + random(21) = 95
    "rate" : 0.2392, // 增长率
    "max"  : 999, // 伤害上限
    "mp"   : 4, // 魔力消耗
]);

#include "type_h_s.h"
