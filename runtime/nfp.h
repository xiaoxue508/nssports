#pragma once
#include "ipc.h"

#ifdef __cplusplus
extern "C" {
#endif

void nfp_user_manager_handler(IpcSession *s, const IpcRequest *rq, IpcResponse *rs);

#ifdef __cplusplus
}
#endif
