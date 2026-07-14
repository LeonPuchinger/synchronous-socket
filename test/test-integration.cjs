const assert = require("assert");
const { spawn } = require("child_process");
const { SynchronousSocket, SynchronousSocketServer } = require("..");

describe("SynchronousSocket integration (server/client)", function () {
    it("accepts a client and exchanges data (blocking)", function (done) {
        this.timeout(5000);

        const socketPath = "/tmp/test-socket-" + Date.now() + ".sock";

        const server = new SynchronousSocketServer(socketPath);
        server.listen();

        const clientScript = `
const { SynchronousSocket } = require('.')
const socketPath = '${socketPath}';
const c = new SynchronousSocket(socketPath);
try {
  c.connect();
  c.write('Hello from client');
  const r = c.read();
  console.log('CLIENT_REPLY:' + (r || 'null'));
  c.disconnect();
  process.exit(0);
} catch (e) {
  console.error('CLIENT_ERROR:' + e.message);
  process.exit(1);
}
`;

        const child = spawn(process.execPath, ["-e", clientScript], {
            cwd: process.cwd(),
        });
        child.stdout.on("data", (d) => process.stdout.write(d.toString()));
        child.stderr.on("data", (d) => process.stderr.write(d.toString()));

        try {
            const clientSocket = server.accept();
            const data = clientSocket.read();
            assert.strictEqual(data && data.toString(), "Hello from client");

            clientSocket.write("Hello from server");
            clientSocket.disconnect();
            server.close();

            child.on("exit", (code) => {
                assert.strictEqual(code, 0);
                done();
            });
        } catch (err) {
            server.close();
            done(err);
        }
    });
});