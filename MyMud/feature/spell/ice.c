/*****************************************************************************
Copyright: 2019, Mud.Ren
File name: ice.c
Description:冰系魔法：单体攻击
Author: xuefeng
Version: v1.0
Date: 2019-03-12
History:
*****************************************************************************/
/**
 * 小于咒语固有魔攻时伤害直接是[初始值]
 * 大于固有魔攻时伤害是[初始值]+(魔攻-固有魔攻)×增长率
 * [实际伤害值]=[伤害计算值]=(max([初始值],[初始值]+(魔攻-固有魔攻)×增长率)×蓄力倍率)+(等级/10+1)×蓄力次数)×魔防倍率×属性倍率×其他倍率
 */
nosave mapping spell_info = ([
    "name" : "ice", // 咒文名称
    "type" : TYPE_M, // 伤害类型
    "range" : RANGE_S, // 作用范围
    "base" : 50, // 基础值(固有魔攻)
    "min"  : 25, // 最小伤害
    "rand" : 10, // 最大波动值 25 + random(10) = 34
    "rate" : 0.094, // 增长率
    "max"  : 999, // 伤害上限
    "mp"   : 3, // 魔力消耗
]);

#include "type_m_s.h"
