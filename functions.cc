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
#include <iostream>

Nan::Persistent<v8::Function> SynchronousSocket::constructor;

NAN_MODULE_INIT(SynchronousSocket::Init) {
    v8::Local<v8::FunctionTemplate> tpl = Nan::New<v8::FunctionTemplate>(New);
    tpl->SetClassName(Nan::New("SynchronousSocket").ToLocalChecked());
    tpl->InstanceTemplate()->SetInternalFieldCount(1);

    Nan::SetPrototypeMethod(tpl, "connect", Connect);
    Nan::SetPrototypeMethod(tpl, "disconnect", Disconnect);
    Nan::SetPrototypeMethod(tpl, "read", Read);
    Nan::SetPrototypeMethod(tpl, "write", Write);

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
    // Blocking read that returns whatever bytes are currently available.
    // Use poll() to wait for readability, then read up to a fixed buffer size.
    auto read_available_blocking = [](int fd, unsigned char **out_buf) -> ssize_t {
        *out_buf = NULL;
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        for (;;) {
            int pr = poll(&pfd, 1, -1);
            if (pr < 0) {
                if (errno == EINTR) continue;
                return -1;
            }
            break;
        }
        if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
            unsigned char *buf = (unsigned char *)malloc(1);
            if (!buf) return -1;
            *out_buf = buf;
            return 0;
        }
        const size_t BUF_SIZE = 4096;
        unsigned char *buf = (unsigned char *)malloc(BUF_SIZE);
        if (!buf) return -1;
        ssize_t nread = ::read(fd, buf, BUF_SIZE);
        if (nread < 0) {
            free(buf);
            return -1;
        }
        *out_buf = buf;
        return nread;
    };

    unsigned char *buf = NULL;
    ssize_t nread = read_available_blocking(obj->socketfd_, &buf);
    if (nread < 0) {
        if (buf) free(buf);
        return Nan::ThrowError("Unable to read from socket.");
    }
    v8::Local<v8::String> out;
    if (nread == 0) {
        free(buf);
        info.GetReturnValue().Set(Nan::Null());
        return;
    }
    else {
        out = Nan::New<v8::String>((const char *)buf, (int)nread).ToLocalChecked();
    }
    free(buf);
    info.GetReturnValue().Set(out);
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
