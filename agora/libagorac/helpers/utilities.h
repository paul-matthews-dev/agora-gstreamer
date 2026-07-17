#ifndef _UTILITIES_H_
#define _UTILITIES_H_

#include "../agoratype.h"

long GetTimeDiff(const TimePoint& start, const TimePoint& end);

TimePoint Now();

int verifyLicense();


#endif
