# SynchronousSocket

This Node.js module exposes a class called `SynchronousSocket`, which
acts as a synchronous interface to Unix domain sockets.

There are five methods for the class `SynchronousSocket`, all
implemented in C++. These are:

- `connect`
- `disconnect`
- `read`
- `readIntoBuffer`
- `write`

Using these five functions you can synchronously communicate with a
Unix domain socket.

## Example usage:

```
i = new SynchronousSocket.SynchronousSocket("/path/to/a/unix/domain/socket");
i.connect();
i.write("Message to client!");
response = i.read();
console.log(response.toString());

// For binary data, read directly into a preallocated Uint8Array or Buffer.
const bytes = new Uint8Array(4096);
const nread = i.readIntoBuffer(bytes);
if (nread !== null) {
	console.log(bytes.slice(0, nread));
}
```
