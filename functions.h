#ifndef NATIVE_EXTENSION_GRAB_H
#define NATIVE_EXTENSION_GRAB_H

#include <nan.h>

class SynchronousSocket : public Nan::ObjectWrap {
  public:
    static NAN_MODULE_INIT(Init);

  private:
    explicit SynchronousSocket(std::string socketPath);
    ~SynchronousSocket();

    static NAN_METHOD(New);
    static NAN_METHOD(Connect);
    static NAN_METHOD(Disconnect);
    static NAN_METHOD(Read);
    static NAN_METHOD(ReadIntoBuffer);
    static NAN_METHOD(Write);
    static NAN_METHOD(WriteFromBuffer);

    static Nan::Persistent<v8::Function> constructor;
    int socketfd_;
    std::string socketPath_;
    
    friend class SynchronousSocketServer;
};

class SynchronousSocketServer : public Nan::ObjectWrap {
  public:
    static NAN_MODULE_INIT(Init);

  private:
    explicit SynchronousSocketServer(std::string socketPath);
    ~SynchronousSocketServer();

    static NAN_METHOD(New);
    static NAN_METHOD(Listen);
    static NAN_METHOD(Accept);
    static NAN_METHOD(Close);

    static Nan::Persistent<v8::Function> constructor;
    int serverfd_;
    std::string socketPath_;
};


#endif
