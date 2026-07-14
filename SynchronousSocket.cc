#include "functions.h"

napi_value InitAll(napi_env env, napi_value exports) {
    if (SynchronousSocket::Init(env, exports) == nullptr) {
        return nullptr;
    }

    if (SynchronousSocketServer::Init(env, exports) == nullptr) {
        return nullptr;
    }

    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, InitAll)
