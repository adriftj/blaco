#pragma once

#ifdef __cplusplus
extern "C" {
#endif

bool _BlInitTimerLoop();
void _BlExitNotifyTimerLoop();
void _BlWaitTimerLoopExited();

#ifdef __cplusplus
}
#endif
