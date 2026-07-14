export declare class SynchronousSocket {
  constructor(socketPath: string);
  connect(): void;
  disconnect(): void;
  read(limit?: number): string | null;
  readIntoBuffer(buffer: ArrayBufferView | Uint8Array): number | null;
  write(data: string): void;
  writeFromBuffer(buffer: ArrayBufferView | Uint8Array): number;
}

export declare class SynchronousSocketServer {
  constructor(socketPath: string);
  listen(backlog?: number): void;
  accept(): SynchronousSocket;
  close(): void;
}

declare const _default: {
  SynchronousSocket: typeof SynchronousSocket;
  SynchronousSocketServer: typeof SynchronousSocketServer;
};

export default _default;
