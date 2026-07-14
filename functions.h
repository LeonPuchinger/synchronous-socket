#ifndef NATIVE_EXTENSION_GRAB_H
#define NATIVE_EXTENSION_GRAB_H

#include <node_api.h>

#include <string>

class SynchronousSocket {
  public:
    static napi_value Init(napi_env env, napi_value exports);
    ~SynchronousSocket();

  private:
    explicit SynchronousSocket(const std::string &socketPath);

    static napi_value New(napi_env env, napi_callback_info info);
    static napi_value Connect(napi_env env, napi_callback_info info);
    static napi_value Disconnect(napi_env env, napi_callback_info info);
    static napi_value Read(napi_env env, napi_callback_info info);
    static napi_value ReadIntoBuffer(napi_env env, napi_callback_info info);
    static napi_value Write(napi_env env, napi_callback_info info);
    static napi_value WriteFromBuffer(napi_env env, napi_callback_info info);

    static napi_ref constructor;
    int socketfd_;
    std::string socketPath_;
    
    friend class SynchronousSocketServer;
};

class SynchronousSocketServer {
  public:
    static napi_value Init(napi_env env, napi_value exports);
    ~SynchronousSocketServer();

  private:
    explicit SynchronousSocketServer(const std::string &socketPath);

    static napi_value New(napi_env env, napi_callback_info info);
    static napi_value Listen(napi_env env, napi_callback_info info);
    static napi_value Accept(napi_env env, napi_callback_info info);
    static napi_value Close(napi_env env, napi_callback_info info);

    static napi_ref constructor;
    int serverfd_;
    std::string socketPath_;
};


#endif
