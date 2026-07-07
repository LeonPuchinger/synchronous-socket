#include "functions.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <algorithm>
#include <iostream>

Nan::Persistent<v8::Function> SynchronousSocket::constructor;

NAN_MODULE_INIT(SynchronousSocket::Init) {
    v8::Local<v8::FunctionTemplate> tpl = Nan::New<v8::FunctionTemplate>(New);
    tpl->SetClassName(Nan::New("SynchronousSocket").ToLocalChecked());
    tpl->InstanceTemplate()->SetInternalFieldCount(1);

    Nan::SetPrototypeMethod(tpl, "connect", Connect);
    Nan::SetPrototypeMethod(tpl, "disconnect", Disconnect);
    Nan::SetPrototypeMethod(tpl, "read", Read);
    Nan::SetPrototypeMethod(tpl, "readIntoBuffer", ReadIntoBuffer);
    Nan::SetPrototypeMethod(tpl, "write", Write);
    Nan::SetPrototypeMethod(tpl, "writeFromBuffer", WriteFromBuffer);

    constructor.Reset(Nan::GetFunction(tpl).ToLocalChecked());
    Nan::Set(target, Nan::New("SynchronousSocket").ToLocalChecked(), Nan::GetFunction(tpl).ToLocalChecked());
}

SynchronousSocket::SynchronousSocket(std::string socketPath) : socketPath_(socketPath) { }

SynchronousSocket::~SynchronousSocket() { }

NAN_METHOD(SynchronousSocket::New) {
    std::string socketPath = *Nan::Utf8String(info[0]);
    SynchronousSocket *obj = new SynchronousSocket(socketPath);
    obj->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

NAN_METHOD(SynchronousSocket::Connect) {
    SynchronousSocket* obj = Nan::ObjectWrap::Unwrap<SynchronousSocket>(info.This());
    obj->socketfd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (obj->socketfd_ == -1) {
        Nan::ThrowError("Unable to open socket file descriptor.");
    }
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, obj->socketPath_.c_str(), sizeof(addr.sun_path) - 1);
    if (connect(obj->socketfd_, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        close(obj->socketfd_);
        Nan::ThrowError("Unable to connect to socket.");
    }
}

NAN_METHOD(SynchronousSocket::Disconnect) {
    SynchronousSocket* obj = Nan::ObjectWrap::Unwrap<SynchronousSocket>(info.This());
    close(obj->socketfd_);
}

NAN_METHOD(SynchronousSocket::Read) {
    SynchronousSocket *obj = Nan::ObjectWrap::Unwrap<SynchronousSocket>(info.This());
    bool has_limit = false;
    size_t limit = 0;
    if (info.Length() > 0 && info[0]->IsNumber()) {
        has_limit = true;
        limit = Nan::To<uint32_t>(info[0]).FromJust();
    }
    else if (info.Length() > 0 && !info[0]->IsUndefined() && !info[0]->IsNull()) {
        return Nan::ThrowTypeError("Optional limit must be a number.");
    }

    auto wait_for_available_bytes = [](int fd, size_t *available_bytes) -> bool {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        for (;;) {
            int pr = poll(&pfd, 1, -1);
            if (pr < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            break;
        }
        if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
            *available_bytes = 0;
            return true;
        }
        int queued = 0;
        if (ioctl(fd, FIONREAD, &queued) < 0) {
            return false;
        }
        *available_bytes = (size_t)queued;
        return true;
    };

    size_t available_bytes = 0;
    if (!wait_for_available_bytes(obj->socketfd_, &available_bytes)) {
        return Nan::ThrowError("Unable to read from socket.");
    }
    if (available_bytes == 0) {
        info.GetReturnValue().Set(Nan::Null());
        return;
    }
    size_t to_read = available_bytes;
    if (has_limit) {
        to_read = std::min(to_read, limit);
    }
    unsigned char *buf = (unsigned char *)malloc(to_read);
    if (!buf) {
        return Nan::ThrowError("Unable to read from socket.");
    }
    ssize_t nread = ::read(obj->socketfd_, buf, to_read);
    if (nread < 0) {
        free(buf);
        return Nan::ThrowError("Unable to read from socket.");
    }
    v8::Local<v8::String> out = Nan::New<v8::String>((const char *)buf, (int)nread).ToLocalChecked();
    free(buf);
    info.GetReturnValue().Set(out);
}

NAN_METHOD(SynchronousSocket::ReadIntoBuffer) {
    SynchronousSocket *obj = Nan::ObjectWrap::Unwrap<SynchronousSocket>(info.This());
    if (info.Length() == 0 || !info[0]->IsArrayBufferView()) {
        return Nan::ThrowTypeError("Buffer must be a Uint8Array, Buffer, or other ArrayBuffer view.");
    }

    v8::Local<v8::ArrayBufferView> view = info[0].As<v8::ArrayBufferView>();
    size_t buffer_length = view->ByteLength();
    if (buffer_length == 0) {
        info.GetReturnValue().Set(Nan::New<v8::Uint32>(0));
        return;
    }

    auto wait_for_available_bytes = [](int fd, size_t *available_bytes) -> bool {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        for (;;) {
            int pr = poll(&pfd, 1, -1);
            if (pr < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            break;
        }
        if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
            *available_bytes = 0;
            return true;
        }
        int queued = 0;
        if (ioctl(fd, FIONREAD, &queued) < 0) {
            return false;
        }
        *available_bytes = (size_t)queued;
        return true;
    };

    size_t available_bytes = 0;
    if (!wait_for_available_bytes(obj->socketfd_, &available_bytes)) {
        return Nan::ThrowError("Unable to read from socket.");
    }
    if (available_bytes == 0) {
        info.GetReturnValue().Set(Nan::Null());
        return;
    }
    size_t to_read = std::min(buffer_length, available_bytes);
    std::shared_ptr<v8::BackingStore> backing_store = view->Buffer()->GetBackingStore();
    unsigned char *out = static_cast<unsigned char *>(backing_store->Data()) + view->ByteOffset();
    ssize_t nread = ::read(obj->socketfd_, out, to_read);
    if (nread < 0) {
        return Nan::ThrowError("Unable to read from socket.");
    }
    info.GetReturnValue().Set(Nan::New<v8::Number>(static_cast<double>(nread)));
}

NAN_METHOD(SynchronousSocket::Write) {
    SynchronousSocket* obj = Nan::ObjectWrap::Unwrap<SynchronousSocket>(info.This());
    if (!info[0]->IsString()) {
        return Nan::ThrowTypeError("Data must be a string.");
    }
    Nan::Utf8String socketDataArg(info[0]);
    const char* data = *socketDataArg;
    ssize_t size = strlen(data);
    if (write(obj->socketfd_, data, size) != size) {
        close(obj->socketfd_);
        Nan::ThrowError("Unable to write to socket.");
    }
}

NAN_METHOD(SynchronousSocket::WriteFromBuffer) {
    SynchronousSocket *obj = Nan::ObjectWrap::Unwrap<SynchronousSocket>(info.This());
    if (info.Length() == 0 || !info[0]->IsArrayBufferView()) {
        return Nan::ThrowTypeError("Buffer must be a Uint8Array, Buffer, or other ArrayBuffer view.");
    }
    v8::Local<v8::ArrayBufferView> view = info[0].As<v8::ArrayBufferView>();
    size_t buffer_length = view->ByteLength();
    if (buffer_length == 0) {
        info.GetReturnValue().Set(Nan::New<v8::Uint32>(0));
        return;
    }
    std::shared_ptr<v8::BackingStore> backing_store = view->Buffer()->GetBackingStore();
    unsigned char *data = static_cast<unsigned char *>(backing_store->Data()) + view->ByteOffset();
    size_t total_written = 0;
    while (total_written < buffer_length) {
        ssize_t written = ::write(obj->socketfd_, data + total_written, buffer_length - total_written);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(obj->socketfd_);
            return Nan::ThrowError("Unable to write to socket.");
        }
        if (written == 0) {
            close(obj->socketfd_);
            return Nan::ThrowError("Unable to write to socket.");
        }
        total_written += (size_t)written;
    }
    info.GetReturnValue().Set(Nan::New<v8::Number>(static_cast<double>(total_written)));
}

// SynchronousSocketServer implementation

Nan::Persistent<v8::Function> SynchronousSocketServer::constructor;

NAN_MODULE_INIT(SynchronousSocketServer::Init) {
    v8::Local<v8::FunctionTemplate> tpl = Nan::New<v8::FunctionTemplate>(New);
    tpl->SetClassName(Nan::New("SynchronousSocketServer").ToLocalChecked());
    tpl->InstanceTemplate()->SetInternalFieldCount(1);

    Nan::SetPrototypeMethod(tpl, "listen", Listen);
    Nan::SetPrototypeMethod(tpl, "accept", Accept);
    Nan::SetPrototypeMethod(tpl, "close", Close);

    constructor.Reset(Nan::GetFunction(tpl).ToLocalChecked());
    Nan::Set(target, Nan::New("SynchronousSocketServer").ToLocalChecked(), Nan::GetFunction(tpl).ToLocalChecked());
}

SynchronousSocketServer::SynchronousSocketServer(std::string socketPath) : serverfd_(-1), socketPath_(socketPath) { }

SynchronousSocketServer::~SynchronousSocketServer() { }

NAN_METHOD(SynchronousSocketServer::New) {
    std::string socketPath = *Nan::Utf8String(info[0]);
    SynchronousSocketServer *obj = new SynchronousSocketServer(socketPath);
    
    // Bind to socket path
    obj->serverfd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (obj->serverfd_ == -1) {
        Nan::ThrowError("Unable to open server socket file descriptor.");
        return;
    }
    
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, obj->socketPath_.c_str(), sizeof(addr.sun_path) - 1);
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
    
    // Remove existing socket file if it exists
    unlink(obj->socketPath_.c_str());
    
    if (bind(obj->serverfd_, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        close(obj->serverfd_);
        Nan::ThrowError("Unable to bind to socket path.");
        return;
    }
    
    obj->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

NAN_METHOD(SynchronousSocketServer::Listen) {
    SynchronousSocketServer* obj = Nan::ObjectWrap::Unwrap<SynchronousSocketServer>(info.This());
    
    int backlog = 5; // Default backlog
    if (info.Length() > 0 && info[0]->IsNumber()) {
        backlog = Nan::To<int32_t>(info[0]).FromJust();
    }
    
    if (listen(obj->serverfd_, backlog) == -1) {
        Nan::ThrowError("Unable to listen on socket.");
    }
}

NAN_METHOD(SynchronousSocketServer::Accept) {
    SynchronousSocketServer* obj = Nan::ObjectWrap::Unwrap<SynchronousSocketServer>(info.This());
    
    // Poll for incoming connections
    struct pollfd pfd;
    pfd.fd = obj->serverfd_;
    pfd.events = POLLIN;
    
    for (;;) {
        int pr = poll(&pfd, 1, -1);
        if (pr < 0) {
            if (errno == EINTR) continue;
            Nan::ThrowError("Error waiting for connection.");
            return;
        }
        break;
    }
    
    if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
        Nan::ThrowError("Server socket error.");
        return;
    }
    
    struct sockaddr_un addr;
    socklen_t addrlen = sizeof(addr);
    int clientfd = accept(obj->serverfd_, (struct sockaddr*)&addr, &addrlen);
    
    if (clientfd == -1) {
        Nan::ThrowError("Unable to accept connection.");
        return;
    }
    
    // Create new SynchronousSocket instance with the accepted connection
    SynchronousSocket* clientSocket = new SynchronousSocket("");
    clientSocket->socketfd_ = clientfd;
    
    v8::Local<v8::Object> instance = Nan::NewInstance(Nan::New(SynchronousSocket::constructor)).ToLocalChecked();
    clientSocket->Wrap(instance);
    
    info.GetReturnValue().Set(instance);
}

NAN_METHOD(SynchronousSocketServer::Close) {
    SynchronousSocketServer* obj = Nan::ObjectWrap::Unwrap<SynchronousSocketServer>(info.This());
    
    if (obj->serverfd_ != -1) {
        close(obj->serverfd_);
        obj->serverfd_ = -1;
    }
    
    // Remove the socket file
    unlink(obj->socketPath_.c_str());
}
