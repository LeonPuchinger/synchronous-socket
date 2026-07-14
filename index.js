import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const nativeAddon = require('./build/Release/SynchronousSocket.node');

export const { SynchronousSocket, SynchronousSocketServer } = nativeAddon;
export default nativeAddon;