#include "functions.h"

#include <algorithm>
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

constexpr size_t kDefaultServerBacklog = 5;

void ThrowError(napi_env env, const char *message) {
    napi_throw_error(env, nullptr, message);
}

void ThrowTypeError(napi_env env, const char *message) {
    napi_throw_type_error(env, nullptr, message);
}

size_t GetTypedArrayByteLength(napi_typedarray_type type, size_t length) {
    size_t element_size = 0;
    switch (type) {
        case napi_int8_array:
        case napi_uint8_array:
        case napi_uint8_clamped_array:
            element_size = 1;
            break;
        case napi_int16_array:
        case napi_uint16_array:
            element_size = 2;
            break;
        case napi_int32_array:
        case napi_uint32_array:
        case napi_float32_array:
            element_size = 4;
            break;
        case napi_float64_array:
        case napi_bigint64_array:
        case napi_biguint64_array:
            element_size = 8;
            break;
        default:
            element_size = 0;
            break;
    }
    return element_size * length;
}

bool GetArrayBufferViewInfo(napi_env env, napi_value value, unsigned char **data, size_t *byte_length) {
    bool is_buffer = false;
    if (napi_is_buffer(env, value, &is_buffer) != napi_ok) {
        return false;
    }
    if (is_buffer) {
        void *raw_data = nullptr;
        if (napi_get_buffer_info(env, value, &raw_data, byte_length) != napi_ok) {
            return false;
        }
        *data = static_cast<unsigned char *>(raw_data);
        return true;
    }

    bool is_typedarray = false;
    if (napi_is_typedarray(env, value, &is_typedarray) != napi_ok) {
        return false;
    }
    if (is_typedarray) {
        napi_typedarray_type type;
        size_t length = 0;
        void *raw_data = nullptr;
        napi_value arraybuffer;
        size_t byte_offset = 0;
        if (napi_get_typedarray_info(env, value, &type, &length, &raw_data, &arraybuffer, &byte_offset) != napi_ok) {
            return false;
        }
        size_t bytes = GetTypedArrayByteLength(type, length);
        if (bytes == 0) {
            return false;
        }
        *data = static_cast<unsigned char *>(raw_data);
        *byte_length = bytes;
        return true;
    }

    bool is_dataview = false;
    if (napi_is_dataview(env, value, &is_dataview) != napi_ok) {
        return false;
    }
    if (is_dataview) {
        void *raw_data = nullptr;
        size_t length = 0;
        napi_value arraybuffer;
        size_t byte_offset = 0;
        if (napi_get_dataview_info(env, value, &length, &raw_data, &arraybuffer, &byte_offset) != napi_ok) {
            return false;
        }
        *data = static_cast<unsigned char *>(raw_data);
        *byte_length = length;
        return true;
    }

    return false;
}

bool GetOptionalLimit(napi_env env, napi_value value, bool *has_limit, size_t *limit) {
    if (value == nullptr) {
        *has_limit = false;
        return true;
    }

    napi_valuetype type;
    if (napi_typeof(env, value, &type) != napi_ok) {
        return false;
    }

    if (type == napi_undefined || type == napi_null) {
        *has_limit = false;
        return true;
    }

    if (type != napi_number) {
        ThrowTypeError(env, "Optional limit must be a number.");
        return false;
    }

    uint32_t limit_value = 0;
    if (napi_get_value_uint32(env, value, &limit_value) != napi_ok) {
        ThrowTypeError(env, "Optional limit must be a number.");
        return false;
    }

    *has_limit = true;
    *limit = static_cast<size_t>(limit_value);
    return true;
}

bool WaitForAvailableBytes(int fd, size_t *available_bytes) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;

    for (;;) {
        int pr = poll(&pfd, 1, -1);
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        break;
    }

    if (pfd.revents & (POLLERR | POLLNVAL)) {
        return false;
    }

    int queued = 0;
    if (ioctl(fd, FIONREAD, &queued) < 0) {
        return false;
    }

    if ((pfd.revents & POLLHUP) && queued == 0) {
        *available_bytes = 0;
        return true;
    }

    *available_bytes = static_cast<size_t>(queued);
    return true;
}

template <typename T>
T *UnwrapInstance(napi_env env, napi_callback_info info, size_t *argc, napi_value *argv, napi_value *this_value) {
    napi_status status = napi_get_cb_info(env, info, argc, argv, this_value, nullptr);
    if (status != napi_ok) {
        return nullptr;
    }

    T *instance = nullptr;
    if (napi_unwrap(env, *this_value, reinterpret_cast<void **>(&instance)) != napi_ok) {
        ThrowError(env, "Unable to access native instance.");
        return nullptr;
    }
    return instance;
}

napi_value CreateUndefined(napi_env env) {
    napi_value result;
    napi_get_undefined(env, &result);
    return result;
}

void FinalizeSynchronousSocket(napi_env env, void *data, void *hint) {
    delete static_cast<SynchronousSocket *>(data);
}

void FinalizeSynchronousSocketServer(napi_env env, void *data, void *hint) {
    delete static_cast<SynchronousSocketServer *>(data);
}

} // namespace

napi_ref SynchronousSocket::constructor = nullptr;
napi_ref SynchronousSocketServer::constructor = nullptr;

SynchronousSocket::SynchronousSocket(const std::string &socketPath) : socketfd_(-1), socketPath_(socketPath) { }

SynchronousSocket::~SynchronousSocket() {
    if (socketfd_ != -1) {
        close(socketfd_);
        socketfd_ = -1;
    }
}

napi_value SynchronousSocket::Init(napi_env env, napi_value exports) {
    napi_property_descriptor properties[] = {
        {"connect", nullptr, Connect, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"disconnect", nullptr, Disconnect, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"read", nullptr, Read, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"readIntoBuffer", nullptr, ReadIntoBuffer, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"write", nullptr, Write, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"writeFromBuffer", nullptr, WriteFromBuffer, nullptr, nullptr, nullptr, napi_default, nullptr},
    };

    napi_value constructor_value;
    napi_status status = napi_define_class(
        env,
        "SynchronousSocket",
        NAPI_AUTO_LENGTH,
        New,
        nullptr,
        sizeof(properties) / sizeof(properties[0]),
        properties,
        &constructor_value);
    if (status != napi_ok) {
        return nullptr;
    }

    status = napi_create_reference(env, constructor_value, 1, &constructor);
    if (status != napi_ok) {
        return nullptr;
    }

    status = napi_set_named_property(env, exports, "SynchronousSocket", constructor_value);
    if (status != napi_ok) {
        return nullptr;
    }

    return exports;
}

napi_value SynchronousSocket::New(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1];
    napi_value this_value;
    if (napi_get_cb_info(env, info, &argc, argv, &this_value, nullptr) != napi_ok) {
        return nullptr;
    }

    if (argc < 1) {
        ThrowTypeError(env, "socketPath must be a string.");
        return nullptr;
    }

    napi_valuetype value_type;
    if (napi_typeof(env, argv[0], &value_type) != napi_ok || value_type != napi_string) {
        ThrowTypeError(env, "socketPath must be a string.");
        return nullptr;
    }

    size_t length = 0;
    if (napi_get_value_string_utf8(env, argv[0], nullptr, 0, &length) != napi_ok) {
        ThrowError(env, "Unable to read socket path.");
        return nullptr;
    }

    std::string socket_path(length, '\0');
    if (napi_get_value_string_utf8(env, argv[0], &socket_path[0], length + 1, &length) != napi_ok) {
        ThrowError(env, "Unable to read socket path.");
        return nullptr;
    }

    auto *obj = new SynchronousSocket(socket_path);
    if (napi_wrap(env, this_value, obj, FinalizeSynchronousSocket, nullptr, nullptr) != napi_ok) {
        delete obj;
        return nullptr;
    }

    return this_value;
}

napi_value SynchronousSocket::Connect(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    napi_value argv[1];
    napi_value this_value;
    SynchronousSocket *obj = UnwrapInstance<SynchronousSocket>(env, info, &argc, argv, &this_value);
    if (obj == nullptr) {
        return nullptr;
    }

    obj->socketfd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (obj->socketfd_ == -1) {
        ThrowError(env, "Unable to open socket file descriptor.");
        return nullptr;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, obj->socketPath_.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(obj->socketfd_, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) == -1) {
        close(obj->socketfd_);
        obj->socketfd_ = -1;
        ThrowError(env, "Unable to connect to socket.");
        return nullptr;
    }

    return CreateUndefined(env);
}

napi_value SynchronousSocket::Disconnect(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    napi_value argv[1];
    napi_value this_value;
    SynchronousSocket *obj = UnwrapInstance<SynchronousSocket>(env, info, &argc, argv, &this_value);
    if (obj == nullptr) {
        return nullptr;
    }

    if (obj->socketfd_ != -1) {
        close(obj->socketfd_);
        obj->socketfd_ = -1;
    }

    return CreateUndefined(env);
}

napi_value SynchronousSocket::Read(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1];
    napi_value this_value;
    SynchronousSocket *obj = UnwrapInstance<SynchronousSocket>(env, info, &argc, argv, &this_value);
    if (obj == nullptr) {
        return nullptr;
    }

    bool has_limit = false;
    size_t limit = 0;
    if (!GetOptionalLimit(env, argc > 0 ? argv[0] : nullptr, &has_limit, &limit)) {
        return nullptr;
    }

    size_t available_bytes = 0;
    if (!WaitForAvailableBytes(obj->socketfd_, &available_bytes)) {
        ThrowError(env, "Unable to read from socket.");
        return nullptr;
    }

    if (available_bytes == 0) {
        napi_value result;
        napi_get_null(env, &result);
        return result;
    }

    size_t to_read = available_bytes;
    if (has_limit) {
        to_read = std::min(to_read, limit);
    }

    unsigned char *buffer = static_cast<unsigned char *>(malloc(to_read));
    if (buffer == nullptr) {
        ThrowError(env, "Unable to read from socket.");
        return nullptr;
    }

    ssize_t nread = ::read(obj->socketfd_, buffer, to_read);
    if (nread < 0) {
        free(buffer);
        ThrowError(env, "Unable to read from socket.");
        return nullptr;
    }

    napi_value result;
    if (napi_create_string_utf8(env, reinterpret_cast<const char *>(buffer), static_cast<size_t>(nread), &result) != napi_ok) {
        free(buffer);
        ThrowError(env, "Unable to read from socket.");
        return nullptr;
    }

    free(buffer);
    return result;
}

napi_value SynchronousSocket::ReadIntoBuffer(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1];
    napi_value this_value;
    SynchronousSocket *obj = UnwrapInstance<SynchronousSocket>(env, info, &argc, argv, &this_value);
    if (obj == nullptr) {
        return nullptr;
    }

    if (argc == 0) {
        ThrowTypeError(env, "Buffer must be a Uint8Array, Buffer, or other ArrayBuffer view.");
        return nullptr;
    }

    unsigned char *buffer_data = nullptr;
    size_t buffer_length = 0;
    if (!GetArrayBufferViewInfo(env, argv[0], &buffer_data, &buffer_length)) {
        ThrowTypeError(env, "Buffer must be a Uint8Array, Buffer, or other ArrayBuffer view.");
        return nullptr;
    }

    if (buffer_length == 0) {
        napi_value result;
        napi_create_uint32(env, 0, &result);
        return result;
    }

    size_t available_bytes = 0;
    if (!WaitForAvailableBytes(obj->socketfd_, &available_bytes)) {
        ThrowError(env, "Unable to read from socket.");
        return nullptr;
    }

    if (available_bytes == 0) {
        napi_value result;
        napi_get_null(env, &result);
        return result;
    }

    size_t to_read = std::min(buffer_length, available_bytes);
    ssize_t nread = ::read(obj->socketfd_, buffer_data, to_read);
    if (nread < 0) {
        ThrowError(env, "Unable to read from socket.");
        return nullptr;
    }

    napi_value result;
    napi_create_uint32(env, static_cast<uint32_t>(nread), &result);
    return result;
}

napi_value SynchronousSocket::Write(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1];
    napi_value this_value;
    SynchronousSocket *obj = UnwrapInstance<SynchronousSocket>(env, info, &argc, argv, &this_value);
    if (obj == nullptr) {
        return nullptr;
    }

    if (argc == 0) {
        ThrowTypeError(env, "Data must be a string.");
        return nullptr;
    }

    napi_valuetype value_type;
    if (napi_typeof(env, argv[0], &value_type) != napi_ok || value_type != napi_string) {
        ThrowTypeError(env, "Data must be a string.");
        return nullptr;
    }

    size_t length = 0;
    if (napi_get_value_string_utf8(env, argv[0], nullptr, 0, &length) != napi_ok) {
        ThrowError(env, "Unable to write to socket.");
        return nullptr;
    }

    std::string data(length, '\0');
    if (napi_get_value_string_utf8(env, argv[0], &data[0], length + 1, &length) != napi_ok) {
        ThrowError(env, "Unable to write to socket.");
        return nullptr;
    }

    ssize_t size = static_cast<ssize_t>(length);
    if (::write(obj->socketfd_, data.data(), static_cast<size_t>(size)) != size) {
        close(obj->socketfd_);
        obj->socketfd_ = -1;
        ThrowError(env, "Unable to write to socket.");
        return nullptr;
    }

    return CreateUndefined(env);
}

napi_value SynchronousSocket::WriteFromBuffer(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1];
    napi_value this_value;
    SynchronousSocket *obj = UnwrapInstance<SynchronousSocket>(env, info, &argc, argv, &this_value);
    if (obj == nullptr) {
        return nullptr;
    }

    if (argc == 0) {
        ThrowTypeError(env, "Buffer must be a Uint8Array, Buffer, or other ArrayBuffer view.");
        return nullptr;
    }

    unsigned char *buffer_data = nullptr;
    size_t buffer_length = 0;
    if (!GetArrayBufferViewInfo(env, argv[0], &buffer_data, &buffer_length)) {
        ThrowTypeError(env, "Buffer must be a Uint8Array, Buffer, or other ArrayBuffer view.");
        return nullptr;
    }

    if (buffer_length == 0) {
        napi_value result;
        napi_create_uint32(env, 0, &result);
        return result;
    }

    size_t total_written = 0;
    while (total_written < buffer_length) {
        ssize_t written = ::write(obj->socketfd_, buffer_data + total_written, buffer_length - total_written);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(obj->socketfd_);
            obj->socketfd_ = -1;
            ThrowError(env, "Unable to write to socket.");
            return nullptr;
        }

        if (written == 0) {
            close(obj->socketfd_);
            obj->socketfd_ = -1;
            ThrowError(env, "Unable to write to socket.");
            return nullptr;
        }

        total_written += static_cast<size_t>(written);
    }

    napi_value result;
    napi_create_uint32(env, static_cast<uint32_t>(total_written), &result);
    return result;
}

SynchronousSocketServer::SynchronousSocketServer(const std::string &socketPath) : serverfd_(-1), socketPath_(socketPath) { }

SynchronousSocketServer::~SynchronousSocketServer() {
    if (serverfd_ != -1) {
        close(serverfd_);
        serverfd_ = -1;
    }

    if (!socketPath_.empty()) {
        unlink(socketPath_.c_str());
    }
}

napi_value SynchronousSocketServer::Init(napi_env env, napi_value exports) {
    napi_property_descriptor properties[] = {
        {"listen", nullptr, Listen, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"accept", nullptr, Accept, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"close", nullptr, Close, nullptr, nullptr, nullptr, napi_default, nullptr},
    };

    napi_value constructor_value;
    napi_status status = napi_define_class(
        env,
        "SynchronousSocketServer",
        NAPI_AUTO_LENGTH,
        New,
        nullptr,
        sizeof(properties) / sizeof(properties[0]),
        properties,
        &constructor_value);
    if (status != napi_ok) {
        return nullptr;
    }

    status = napi_create_reference(env, constructor_value, 1, &constructor);
    if (status != napi_ok) {
        return nullptr;
    }

    status = napi_set_named_property(env, exports, "SynchronousSocketServer", constructor_value);
    if (status != napi_ok) {
        return nullptr;
    }

    return exports;
}

napi_value SynchronousSocketServer::New(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1];
    napi_value this_value;
    if (napi_get_cb_info(env, info, &argc, argv, &this_value, nullptr) != napi_ok) {
        return nullptr;
    }

    if (argc < 1) {
        ThrowTypeError(env, "socketPath must be a string.");
        return nullptr;
    }

    napi_valuetype value_type;
    if (napi_typeof(env, argv[0], &value_type) != napi_ok || value_type != napi_string) {
        ThrowTypeError(env, "socketPath must be a string.");
        return nullptr;
    }

    size_t length = 0;
    if (napi_get_value_string_utf8(env, argv[0], nullptr, 0, &length) != napi_ok) {
        ThrowError(env, "Unable to read socket path.");
        return nullptr;
    }

    std::string socket_path(length, '\0');
    if (napi_get_value_string_utf8(env, argv[0], &socket_path[0], length + 1, &length) != napi_ok) {
        ThrowError(env, "Unable to read socket path.");
        return nullptr;
    }

    auto *obj = new SynchronousSocketServer(socket_path);
    obj->serverfd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (obj->serverfd_ == -1) {
        delete obj;
        ThrowError(env, "Unable to open server socket file descriptor.");
        return nullptr;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, obj->socketPath_.c_str(), sizeof(addr.sun_path) - 1);
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';

    unlink(obj->socketPath_.c_str());

    if (bind(obj->serverfd_, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) == -1) {
        delete obj;
        ThrowError(env, "Unable to bind to socket path.");
        return nullptr;
    }

    if (napi_wrap(env, this_value, obj, FinalizeSynchronousSocketServer, nullptr, nullptr) != napi_ok) {
        delete obj;
        return nullptr;
    }

    return this_value;
}

napi_value SynchronousSocketServer::Listen(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1];
    napi_value this_value;
    SynchronousSocketServer *obj = UnwrapInstance<SynchronousSocketServer>(env, info, &argc, argv, &this_value);
    if (obj == nullptr) {
        return nullptr;
    }

    int backlog = static_cast<int>(kDefaultServerBacklog);
    if (argc > 0) {
        napi_valuetype value_type;
        if (napi_typeof(env, argv[0], &value_type) != napi_ok || (value_type != napi_number && value_type != napi_undefined && value_type != napi_null)) {
            ThrowTypeError(env, "Optional backlog must be a number.");
            return nullptr;
        }

        if (value_type == napi_number) {
            if (napi_get_value_int32(env, argv[0], &backlog) != napi_ok) {
                ThrowTypeError(env, "Optional backlog must be a number.");
                return nullptr;
            }
        }
    }

    if (listen(obj->serverfd_, backlog) == -1) {
        ThrowError(env, "Unable to listen on socket.");
        return nullptr;
    }

    return CreateUndefined(env);
}

napi_value SynchronousSocketServer::Accept(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    napi_value argv[1];
    napi_value this_value;
    SynchronousSocketServer *obj = UnwrapInstance<SynchronousSocketServer>(env, info, &argc, argv, &this_value);
    if (obj == nullptr) {
        return nullptr;
    }

    struct pollfd pfd;
    pfd.fd = obj->serverfd_;
    pfd.events = POLLIN;

    for (;;) {
        int pr = poll(&pfd, 1, -1);
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            ThrowError(env, "Error waiting for connection.");
            return nullptr;
        }
        break;
    }

    if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
        ThrowError(env, "Server socket error.");
        return nullptr;
    }

    struct sockaddr_un addr;
    socklen_t addrlen = sizeof(addr);
    int clientfd = accept(obj->serverfd_, reinterpret_cast<struct sockaddr *>(&addr), &addrlen);
    if (clientfd == -1) {
        ThrowError(env, "Unable to accept connection.");
        return nullptr;
    }

    napi_value constructor_value;
    if (napi_get_reference_value(env, SynchronousSocket::constructor, &constructor_value) != napi_ok) {
        close(clientfd);
        ThrowError(env, "Unable to create socket instance.");
        return nullptr;
    }

    napi_value empty_path;
    if (napi_create_string_utf8(env, "", NAPI_AUTO_LENGTH, &empty_path) != napi_ok) {
        close(clientfd);
        ThrowError(env, "Unable to create socket instance.");
        return nullptr;
    }

    napi_value instance;
    if (napi_new_instance(env, constructor_value, 1, &empty_path, &instance) != napi_ok) {
        close(clientfd);
        ThrowError(env, "Unable to create socket instance.");
        return nullptr;
    }

    SynchronousSocket *client_socket = nullptr;
    if (napi_unwrap(env, instance, reinterpret_cast<void **>(&client_socket)) != napi_ok || client_socket == nullptr) {
        close(clientfd);
        ThrowError(env, "Unable to create socket instance.");
        return nullptr;
    }

    client_socket->socketfd_ = clientfd;
    return instance;
}

napi_value SynchronousSocketServer::Close(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    napi_value argv[1];
    napi_value this_value;
    SynchronousSocketServer *obj = UnwrapInstance<SynchronousSocketServer>(env, info, &argc, argv, &this_value);
    if (obj == nullptr) {
        return nullptr;
    }

    if (obj->serverfd_ != -1) {
        close(obj->serverfd_);
        obj->serverfd_ = -1;
    }

    if (!obj->socketPath_.empty()) {
        unlink(obj->socketPath_.c_str());
    }

    return CreateUndefined(env);
}