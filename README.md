# SynchronousSocket

Lightweight synchronous Unix domain socket bindings for Node.js.

This native addon exposes two classes:

- `SynchronousSocket` — a blocking client socket (connect/read/write).
- `SynchronousSocketServer` — a blocking server that accepts connections and
	returns `SynchronousSocket` instances for each client.

Build (from project root):

```bash
npm run build
```

Quick import:

```javascript
import { SynchronousSocket, SynchronousSocketServer } from 'synchronous-socket';
```

----

Client API (`SynchronousSocket`)

- `new SynchronousSocket(socketPath: string)`
	- Construct with the socket path. This does not connect.
- `connect(): void`
	- Connects to the bound Unix domain socket. Throws on failure.
- `disconnect(): void`
	- Closes the socket descriptor.
- `read(limit?: number): string | null`
	- Blocks until data is available, then reads up to `limit` bytes (if
		provided). Returns a string (may contain binary data) or `null` when the
		peer closed the connection.
- `readIntoBuffer(buf: ArrayBufferView): number | null`
	- Reads up to `buf.byteLength` bytes directly into the provided view. Returns
		the number of bytes read or `null` on EOF.
- `write(data: string): void`
	- Writes the provided string data. Throws on error.
- `writeFromBuffer(buf: ArrayBufferView): number`
	- Writes the full contents of `buf` (may loop until complete). Returns the
		total bytes written.

Notes:
- All operations are blocking and may throw `Error` on system failures.
- For binary-safe operations prefer `readIntoBuffer` / `writeFromBuffer`.

----

Server API (`SynchronousSocketServer`)

- `new SynchronousSocketServer(socketPath: string)`
	- Creates and binds a server socket at `socketPath`. Any existing socket
		file at that path is unlinked first.
- `listen(backlog?: number): void`
	- Marks the socket as passive (ready to accept). `backlog` is optional and
		defaults to `5`.
- `accept(): SynchronousSocket`
	- Blocking call that waits for and accepts the next incoming connection,
		returning a `SynchronousSocket` instance representing the client. The
		returned instance is fully usable with the client API methods.
- `close(): void`
	- Closes the listening socket and removes the socket file from disk.

Server notes:
- The `constructor` binds but does not start listening; call `listen()` to
	activate the server.
- `accept()` is blocking by design; spawn threads or processes if you need
	concurrent accept loops.

----

Examples

Server example (blocking):

```javascript
import { SynchronousSocketServer } from 'synchronous-socket';

const server = new SynchronousSocketServer('/tmp/example.sock');
server.listen(); // optional backlog
const client = server.accept(); // blocks until a client connects
const data = client.read();
console.log('from client:', data && data.toString());
client.write('reply from server');
client.disconnect();
server.close();
```

Client example (blocking):

```javascript
import { SynchronousSocket } from 'synchronous-socket';

const client = new SynchronousSocket('/tmp/example.sock');
client.connect();
client.write('hello server');
const reply = client.read();
console.log('reply:', reply && reply.toString());
client.disconnect();
```

Binary example (reading into a buffer):

```javascript
const buf = new Uint8Array(4096);
const n = client.readIntoBuffer(buf);
if (n !== null) console.log(buf.slice(0, n));
```

----

Design decisions

- The addon provides a blocking, synchronous API intentionally — it is
	suitable for scripts or native integrations where synchronous semantics are
	desired. Users requiring an async API should rely on the standard node sockets.
