#pragma once
#include <stdint.h>

// Independent page clock: millis wraparound is handled by unsigned subtraction.
struct DisplaySchedule {
  uint8_t page = 0; // 0 Energy, 1 Coupons
  uint32_t startedAt = 0;
  bool vehicleWasActive = false;
  bool dataWasValid = false;

  void reset(bool energy, uint32_t now) {
    page = energy ? 0 : 1;
    startedAt = now;
    vehicleWasActive = false;
    dataWasValid = false;
  }
  bool update(bool energy, bool coupons, uint32_t energyMs, uint32_t couponMs,
              bool vehicle, bool valid, uint32_t now) {
    if (vehicle) {
      vehicleWasActive = true;
      dataWasValid = valid;
      return false;
    }
    if (vehicleWasActive) {
      vehicleWasActive = false;
      dataWasValid = valid;
      startedAt = now; // Give the interrupted page its full duration on return.
      return true;
    }
    if (!valid) {
      dataWasValid = false;
      startedAt = now;
      return false;
    }
    if (!dataWasValid) {
      dataWasValid = true;
      startedAt = now;
    }
    if (!(energy && coupons)) {
      uint8_t onlyPage = energy ? 0 : 1;
      if (page == onlyPage) return false;
      page = onlyPage;
    } else {
      uint32_t duration = page == 0 ? energyMs : couponMs;
      if ((uint32_t)(now - startedAt) < duration) return false;
      page = page == 0 ? 1 : 0;
    }
    startedAt = now;
    return true;
  }
};
