#pragma once
#include <stdint.h>

struct CouponFlashEvents {
  bool ready=false, active=false;
  uint32_t previous=0, pending=0, startedAt=0;
  void reset(){ready=false;active=false;pending=0;}
  void observe(bool enabled,bool valid,uint32_t count){
    if(!enabled || !valid){reset();return;}
    if(ready && count>previous){
      uint32_t added=count-previous;
      pending=UINT32_MAX-pending<added ? UINT32_MAX : pending+added;
    }
    previous=count;ready=true;
  }
  bool lit(uint32_t now,bool paused){
    if(paused){
      if(active && (uint32_t)(now-startedAt)<350 && pending<UINT32_MAX)pending++;
      active=false;return false;
    }
    if(active && (uint32_t)(now-startedAt)>=650)active=false;
    if(!active && pending){pending--;active=true;startedAt=now;}
    return active && (uint32_t)(now-startedAt)<350;
  }
};
