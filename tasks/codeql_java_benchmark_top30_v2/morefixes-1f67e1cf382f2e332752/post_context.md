# Patch-overlapping Java context after the fix

## `codec-classes-quic/src/main/java/io/netty/incubator/codec/quic/ConnectionIdChannelMap.java` (changed lines (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100))

```java
/*
 * Copyright 2025 The Netty Project
 *
 * The Netty Project licenses this file to you under the Apache License,
 * version 2.0 (the "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at:
 *
 *   https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */
package io.netty.incubator.codec.quic;

import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.nio.ByteBuffer;
import java.security.SecureRandom;
import java.util.HashMap;
import java.util.Map;
import java.util.Objects;

/**
 * We use a custom hash that uses SipHash 1-3 to prevent
 * <a href="https://github.com/ncc-pbottine/QUIC-Hash-Dos-Advisory">Hash Denial-of-Service Attacks</a>.
 */
final class ConnectionIdChannelMap {
    private static final SecureRandom random = new SecureRandom();

    private final Map<ConnectionIdKey, QuicheQuicChannel> channelMap = new HashMap<>();
    private final SipHash sipHash;

    ConnectionIdChannelMap() {
        byte[] seed = new byte[SipHash.SEED_LENGTH];
        random.nextBytes(seed);
        // Use SipHash 1-3 for now which is also what rust is using by default.
        sipHash = new SipHash(1, 3, seed);
    }

    private ConnectionIdKey key(ByteBuffer cid) {
        long hash = sipHash.macHash(cid);
        return new ConnectionIdKey(hash, cid);
    }

    @Nullable
    QuicheQuicChannel put(ByteBuffer cid, QuicheQuicChannel channel) {
        return channelMap.put(key(cid), channel);
    }

    @Nullable
    QuicheQuicChannel remove(ByteBuffer cid) {
        return channelMap.remove(key(cid));
    }

    @Nullable
    QuicheQuicChannel get(ByteBuffer cid) {
        return channelMap.get(key(cid));
    }

    void clear() {
        channelMap.clear();
    }

    private static final class ConnectionIdKey implements Comparable<ConnectionIdKey> {
        private final long hash;
        private final ByteBuffer key;

        ConnectionIdKey(long hash, ByteBuffer key) {
            this.hash = hash;
            this.key = key;
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (o == null || getClass() != o.getClass()) {
                return false;
            }
            ConnectionIdKey that = (ConnectionIdKey) o;
            return hash == that.hash && Objects.equals(key, that.key);
        }

        @Override
        public int hashCode() {
            return (int) hash;
        }

        @Override
        public int compareTo(@NotNull ConnectionIdChannelMap.ConnectionIdKey o) {
            int result = Long.compare(hash, o.hash);
            return result != 0 ? result : key.compareTo(o.key);
        }
    }
}
```

## `codec-classes-quic/src/main/java/io/netty/incubator/codec/quic/QuicheQuicCodec.java` (changed lines (32, 33, 44))

```java
/*
 * Copyright 2020 The Netty Project
 *
 * The Netty Project licenses this file to you under the Apache License,
 * version 2.0 (the "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at:
 *
 *   https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */
package io.netty.incubator.codec.quic;

import io.netty.buffer.ByteBuf;
import io.netty.channel.ChannelDuplexHandler;
import io.netty.channel.ChannelHandlerContext;
import io.netty.channel.ChannelPromise;
import io.netty.channel.MessageSizeEstimator;
import io.netty.channel.socket.DatagramPacket;
import io.netty.util.internal.logging.InternalLogger;
import io.netty.util.internal.logging.InternalLoggerFactory;
import org.jetbrains.annotations.Nullable;

import java.net.InetSocketAddress;
import java.net.SocketAddress;
import java.nio.ByteBuffer;
import java.util.ArrayDeque;
import java.util.HashSet;
import java.util.Queue;
import java.util.Set;
import java.util.function.Consumer;

import static io.netty.incubator.codec.quic.Quiche.allocateNativeOrder;

/**
 * Abstract base class for QUIC codecs.
 */
abstract class QuicheQuicCodec extends ChannelDuplexHandler {
    private static final InternalLogger LOGGER = InternalLoggerFactory.getInstance(QuicheQuicCodec.class);
    private final ConnectionIdChannelMap connectionIdToChannel = new ConnectionIdChannelMap();
    private final Set<QuicheQuicChannel> channels = new HashSet<>();
    private final Queue<QuicheQuicChannel> needsFireChannelReadComplete = new ArrayDeque<>();
    private final Queue<QuicheQuicChannel> delayedRemoval = new ArrayDeque<>();

    private final Consumer<QuicheQuicChannel> freeTask = this::removeChannel;
    private final FlushStrategy flushStrategy;
    private final int localConnIdLength;
    private final QuicheConfig config;

    private MessageSizeEstimator.Handle estimatorHandle;
    private QuicHeaderParser headerParser;
    private QuicHeaderParser.QuicHeaderProcessor parserCallback;
    private int pendingBytes;
    private int pendingPackets;
    private boolean inChannelReadComplete;
    private boolean delayRemoval;

    // This buffer is used to copy InetSocketAddress to sockaddr_storage and so pass it down the JNI layer.
    private ByteBuf senderSockaddrMemory;
    private ByteBuf recipientSockaddrMemory;

    QuicheQuicCodec(QuicheConfig config, int localConnIdLength, FlushStrategy flushStrategy) {
        this.config = config;
        this.localConnIdLength = localConnIdLength;
        this.flushStrategy = flushStrategy;
    }

    @Override
    public final boolean isSharable() {
        return false;
    }

    @Nullable
    protected final QuicheQuicChannel getChannel(ByteBuffer key) {
        return connectionIdToChannel.get(key);
    }

    private void addMapping(QuicheQuicChannel channel, ByteBuffer id) {
        QuicheQuicChannel ch = connectionIdToChannel.put(id, channel);
        assert ch == null;
    }

    private void removeMapping(QuicheQuicChannel channel, ByteBuffer id) {
        QuicheQuicChannel ch = connectionIdToChannel.remove(id);
        assert ch == channel;
    }

    private void processDelayedRemoval() {
        for (;;) {
            // Now remove all channels that we marked for removal.
            QuicheQuicChannel toBeRemoved = delayedRemoval.poll();
            if (toBeRemoved == null) {
                break;
            }
            removeChannel(toBeRemoved);
        }
    }

    private void removeChannel(QuicheQuicChannel channel) {
        if (delayRemoval) {
            boolean added = delayedRemoval.offer(channel);
            assert added;
        } else {
            boolean removed = channels.remove(channel);
            if (removed) {
                for (ByteBuffer id : channel.sourceConnectionIds()) {
                    QuicheQuicChannel ch = connectionIdToChannel.remove(id);
                    assert ch == channel;
                }
            }
        }
    }

    protected final void addChannel(QuicheQuicChannel channel) {
        boolean added = channels.add(channel);
        assert added;
        for (ByteBuffer id : channel.sourceConnectionIds()) {
            QuicheQuicChannel ch = connectionIdToChannel.put(id.duplicate(), channel);
            assert ch == null;
        }
    }

    @Override
    public final void handlerAdded(ChannelHandlerContext ctx) {
        senderSockaddrMemory = allocateNativeOrder(Quiche.SIZEOF_SOCKADDR_STORAGE);
        recipientSockaddrMemory = allocateNativeOrder(Quiche.SIZEOF_SOCKADDR_STORAGE);
        headerParser = new QuicHeaderParser(localConnIdLength);
        parserCallback = new QuicCodecHeaderProcessor(ctx);
        estimatorHandle = ctx.channel().config().getMessageSizeEstimator().newHandle();
        handlerAdded(ctx, localConnIdLength);
    }

    /**
     * See {@link io.netty.channel.ChannelHandler#handlerAdded(ChannelHandlerContext)}.
     */
    protected void handlerAdded(ChannelHandlerContext ctx, int localConnIdLength) {
        // NOOP.
    }

    @Override
    public void handlerRemoved(ChannelHandlerContext ctx) {
        try {
            // Use a copy of the array as closing the channel may cause an unwritable event that could also
            // remove channels.
            for (QuicheQuicChannel ch : channels.toArray(new QuicheQuicChannel[0])) {
                ch.forceClose();
            }
            if (pendingPackets > 0) {
                flushNow(ctx);
            }
        } finally {
            channels.clear();
            connectionIdToChannel.clear();
            needsFireChannelReadComplete.clear();
            delayedRemoval.clear();

            config.free();
            if (senderSockaddrMemory != null) {
                senderSockaddrMemory.release();
            }
            if (recipientSockaddrMemory != null) {
                recipientSockaddrMemory.release();
            }
            if (headerParser != null) {
                headerParser.close();
                headerParser = null;
            }
        }
    }

    @Override
    public final void channelRead(ChannelHandlerContext ctx, Object msg) throws Exception {
        DatagramPacket packet = (DatagramPacket) msg;
        try {
            ByteBuf buffer = ((DatagramPacket) msg).content();
            if (!buffer.isDirect()) {
                // We need a direct buffer as otherwise we can not access the memoryAddress.
                // Let's do a copy to direct memory.
                ByteBuf direct = ctx.alloc().directBuffer(buffer.readableBytes());
                try {
                    direct.writeBytes(buffer, buffer.readerIndex(), buffer.readableBytes());
                    handleQuicPacket(packet.sender(), packet.recipient(), direct);
                } finally {
                    direct.release();
                }
            } else {
                handleQuicPacket(packet.sender(), packet.recipient(), buffer);
            }
        } finally {
            packet.release();
        }
    }

    private void handleQuicPacket(InetSocketAddress sender, InetSocketAddress recipient, ByteBuf buffer) {
        try {
            headerParser.parse(sender, recipient, buffer, parserCallback);
        } catch (Exception e) {
            LOGGER.debug("Error while processing QUIC packet", e);
        }
    }

    /**
     * Handle a QUIC packet and return {@link QuicheQuicChannel} that is mapped to the id.
     *
     * @param ctx the {@link ChannelHandlerContext}.
     * @param sender the {@link InetSocketAddress} of the sender of the QUIC packet
     * @param recipient the {@link InetSocketAddress} of the recipient of the QUIC packet
     * @param type the type of the packet.
     * @param version the QUIC version
     * @param scid the source connection id.
     * @param dcid the destination connection id
     * @param token the token
     * @param senderSockaddrMemory the {@link ByteBuf} that can be used for the sender {@code struct sockaddr).
     * @param recipientSockaddrMemory the {@link ByteBuf} that can be used for the recipient {@code struct sockaddr).
     * @param freeTask the {@link Consumer} that will be called once native memory of the {@link QuicheQuicChannel} is
     *                  freed and so the mappings should be deleted to the ids.
     * @param localConnIdLength the length of the local connection ids.
     * @param config the {@link QuicheConfig} that is used.
     * @return the {@link QuicheQuicChannel} that is mapped to the id.
     * @throws Exception  thrown if there is an error during processing.
     */
    @Nullable
    protected abstract QuicheQuicChannel quicPacketRead(ChannelHandlerContext ctx, InetSocketAddress sender,
                                                        InetSocketAddress recipient, QuicPacketType type, long version,
                                                        ByteBuf scid, ByteBuf dcid, ByteBuf token,
                                                        ByteBuf senderSockaddrMemory, ByteBuf recipientSockaddrMemory,
                                                        Consumer<QuicheQuicChannel> freeTask,
                                                        int localConnIdLength, QuicheConfig config) throws Exception;

    @Override
    public final void channelReadComplete(ChannelHandlerContext ctx) {
        inChannelReadComplete = true;
        try {
            for (;;) {
                QuicheQuicChannel channel = needsFireChannelReadComplete.poll();
                if (channel == null) {
                    break;
                }
                channel.recvComplete();
            }
        } finally {
            inChannelReadComplete = false;
            if (pendingPackets > 0) {
                flushNow(ctx);
            }
        }
    }

    @Override
    public final void channelWritabilityChanged(ChannelHandlerContext ctx) {
        if (ctx.channel().isWritable()) {
            // Ensure we delay removal from the channels Set as otherwise we will might see an exception
            // due modifications while iteration.
            delayRemoval = true;
            try {
                for (QuicheQuicChannel channel : channels) {
                    // TODO: Be a bit smarter about this.
                    channel.writable();
                }
            } finally {
                // We are done with the loop, reset the flag and process the removals from the channels Set.
                delayRemoval = false;
                processDelayedRemoval();
            }
        } else {
            // As we batch flushes we need to ensure we at least try to flush a batch once the channel becomes
            // unwritable. Otherwise we may end up with buffering too much writes and so waste memory.
            ctx.flush();
        }

        ctx.fireChannelWritabilityChanged();
    }

    @Override
    public final void write(ChannelHandlerContext ctx, Object msg, ChannelPromise promise)  {
        pendingPackets ++;
        int size = estimatorHandle.size(msg);
        if (size > 0) {
            pendingBytes += size;
        }
        try {
            ctx.write(msg, promise);
        } finally {
            flushIfNeeded(ctx);
        }
    }

    @Override
    public final void flush(ChannelHandlerContext ctx) {
        // If we are in the channelReadComplete(...) method we might be able to delay the flush(...) until we finish
        // processing all channels.
        if (inChannelReadComplete) {
            flushIfNeeded(ctx);
        } else if (pendingPackets > 0) {
            flushNow(ctx);
        }
    }

    @Override
    public void connect(ChannelHandlerContext ctx, SocketAddress remoteAddress, SocketAddress localAddress,
                        ChannelPromise promise) throws Exception {
        if (remoteAddress instanceof QuicheQuicChannelAddress) {
            QuicheQuicChannelAddress addr = (QuicheQuicChannelAddress) remoteAddress;
            QuicheQuicChannel channel = addr.channel;
            connectQuicChannel(channel, remoteAddress, localAddress,
                    senderSockaddrMemory, recipientSockaddrMemory, freeTask, localConnIdLength, config, promise);
        } else {
            ctx.connect(remoteAddress, localAddress, promise);
        }
    }

    /**
     * Connects the given {@link QuicheQuicChannel}.
     *
     * @param channel                   the {@link QuicheQuicChannel} to connect.
     * @param remoteAddress             the remote {@link SocketAddress}.
     * @param localAddress              the local  {@link SocketAddress}
     * @param senderSockaddrMemory      the {@link ByteBuf} that can be used for the sender {@code struct sockaddr).
     * @param recipientSockaddrMemory   the {@link ByteBuf} that can be used for the recipient {@code struct sockaddr).
     * @param freeTask                  the {@link Consumer} that will be called once native memory of the
     *                                  {@link QuicheQuicChannel} is freed and so the mappings should be deleted to
     *                                  the ids.
     * @param localConnIdLength         the length of the local connection ids.
     * @param config                    the {@link QuicheConfig} that is used.
     * @param promise                   the {@link ChannelPromise} to notify once the connect is done.
     */
    protected abstract void connectQuicChannel(QuicheQuicChannel channel, SocketAddress remoteAddress,
                                               SocketAddress localAddress, ByteBuf senderSockaddrMemory,
                                               ByteBuf recipientSockaddrMemory, Consumer<QuicheQuicChannel> freeTask,
                                               int localConnIdLength, QuicheConfig config, ChannelPromise promise);


    private void flushIfNeeded(ChannelHandlerContext ctx) {
        // Check if we should force a flush() and so ensure the packets are delivered in a timely
        // manner and also make room in the outboundbuffer again that belongs to the underlying channel.
        if (flushStrategy.shouldFlushNow(pendingPackets, pendingBytes)) {
            flushNow(ctx);
        }
    }

    private void flushNow(ChannelHandlerContext ctx) {
        pendingBytes = 0;
        pendingPackets = 0;
        ctx.flush();
    }

    private final class QuicCodecHeaderProcessor implements QuicHeaderParser.QuicHeaderProcessor {

        private final ChannelHandlerContext ctx;

        QuicCodecHeaderProcessor(ChannelHandlerContext ctx) {
            this.ctx = ctx;
        }

        @Override
        public void process(InetSocketAddress sender, InetSocketAddress recipient, ByteBuf buffer, QuicPacketType type,
                            long version, ByteBuf scid, ByteBuf dcid, ByteBuf token) throws Exception {
            QuicheQuicChannel channel = quicPacketRead(ctx, sender, recipient,
                    type, version, scid,
                    dcid, token, senderSockaddrMemory, recipientSockaddrMemory, freeTask, localConnIdLength, config);
            if (channel != null) {
                // Add to queue first, we might be able to safe some flushes and consolidate them
                // in channelReadComplete(...) this way.
                if (channel.markInFireChannelReadCompleteQueue()) {
                    needsFireChannelReadComplete.add(channel);
                }
                channel.recv(sender, recipient, buffer);
                for (ByteBuffer retiredSourceConnectionId : channel.retiredSourceConnectionId()) {
                    removeMapping(channel, retiredSourceConnectionId);
                }
                for (ByteBuffer newSourceConnectionId : channel.newSourceConnectionIds()) {
                    addMapping(channel, newSourceConnectionId);
                }
            }
        }
    }
}
```

## `codec-classes-quic/src/main/java/io/netty/incubator/codec/quic/SipHash.java` (changed lines (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150))

```java
/*
 * Copyright 2025 The Netty Project
 *
 * The Netty Project licenses this file to you under the Apache License,
 * version 2.0 (the "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at:
 *
 *   https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */
package io.netty.incubator.codec.quic;

import io.netty.util.internal.ObjectUtil;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/**
 * <a href="https://www.aumasson.jp/siphash/siphash.pdf">Siphash implementation</a>.
 */
final class SipHash {

    static final int SEED_LENGTH = 16;

    // Make this class allocation free as soon as its constructed.
    private final int compressionRounds;
    private final int finalizationRounds;

    // As specified in https://www.aumasson.jp/siphash/siphash.pdf
    private static final long INITIAL_STATE_V0 = 0x736f6d6570736575L; // "somepseu"
    private static final long INITIAL_STATE_V1 = 0x646f72616e646f6dL; // "dorandom"
    private static final long INITIAL_STATE_V2 = 0x6c7967656e657261L; // "lygenera"
    private static final long INITIAL_STATE_V3 = 0x7465646279746573L;  // "tedbytes"

    private final long initialStateV0;
    private final long initialStateV1;
    private final long initialStateV2;
    private final long initialStateV3;

    private long v0;
    private long v1;
    private long v2;
    private long v3;

    SipHash(int compressionRounds, int finalizationRounds, byte[] seed) {
        if (seed.length != SEED_LENGTH) {
            throw new IllegalArgumentException("seed must be of length " + SEED_LENGTH);
        }
        this.compressionRounds = ObjectUtil.checkPositive(compressionRounds, "compressionRounds");
        this.finalizationRounds = ObjectUtil.checkPositive(finalizationRounds, "finalizationRounds");

        // Wrap the seed to extract two longs that will be used to generate the initial state.
        // Use little-endian as in the paper.
        ByteBuffer keyBuffer = ByteBuffer.wrap(seed).order(ByteOrder.LITTLE_ENDIAN);
        final long k0 = keyBuffer.getLong();
        final long k1 = keyBuffer.getLong();

        initialStateV0 = INITIAL_STATE_V0 ^ k0;
        initialStateV1 = INITIAL_STATE_V1 ^ k1;
        initialStateV2 = INITIAL_STATE_V2 ^ k0;
        initialStateV3 = INITIAL_STATE_V3 ^ k1;
    }

    long macHash(ByteBuffer input) {
        v0 = initialStateV0;
        v1 = initialStateV1;
        v2 = initialStateV2;
        v3 = initialStateV3;
        int remaining = input.remaining();
        int position = input.position();
        int len = remaining - (remaining % Long.BYTES);
        boolean needsReverse = input.order() == ByteOrder.BIG_ENDIAN;
        for (int offset = position; offset < len; offset +=  Long.BYTES) {
            long m = input.getLong(offset);
            if (needsReverse) {
                // We use little-endian as in the paper.
                m = Long.reverseBytes(m);
            }
            v3 ^= m;
            for (int i = 0; i < compressionRounds; i++) {
                sipround();
            }
            v0 ^= m;
        }

        // Get last bits.
        final int left = remaining & (Long.BYTES - 1);
        long b = (long) remaining << 56;
        assert left < Long.BYTES;
        switch (left) {
            case 7:
                b |= ((long) input.get(position + len + 6) << 48);
            case 6:
                b |= ((long) input.get(position + len + 5) << 40);
            case 5:
                b |= ((long) input.get(position + len + 4) << 32);
            case 4:
                b |= ((long) input.get(position + len + 3) << 24);
            case 3:
                b |= ((long) input.get(position + len + 2) << 16);
            case 2:
                b |= ((long) input.get(position + len + 1) << 8);
            case 1:
                b |= input.get(position + len);
                break;
            case 0:
                break;
            default:
                throw new IllegalStateException("Unexpected value: " + left);
        }

        v3 ^= b;
        for (int i = 0; i < compressionRounds; i++) {
            sipround();
        }

        v0 ^= b;
        v2 ^= 0xFF;
        for (int i = 0; i < finalizationRounds; i++) {
            sipround();
        }

        return v0 ^ v1 ^ v2 ^ v3;
    }

    private void sipround() {
        v0 += v1;
        v2 += v3;
        v1 = Long.rotateLeft(v1, 13);
        v3 = Long.rotateLeft(v3, 16);
        v1 ^= v0;
        v3 ^= v2;

        v0 = Long.rotateLeft(v0, 32);

        v2 += v1;
        v0 += v3;
        v1 = Long.rotateLeft(v1, 17);
        v3 = Long.rotateLeft(v3, 21);
        v1 ^= v2;
        v3 ^= v0;

        v2 = Long.rotateLeft(v2, 32);
    }
}
```

## `codec-native-quic/src/test/java/io/netty/incubator/codec/quic/SipHashTest.java` (changed lines (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158))

```java
/*
 * Copyright 2025 The Netty Project
 *
 * The Netty Project licenses this file to you under the Apache License,
 * version 2.0 (the "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at:
 *
 *   https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */
package io.netty.incubator.codec.quic;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.MethodSource;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.concurrent.ThreadLocalRandom;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;

public class SipHashTest {

    private static final byte[] REFERENCE_SEED = new byte[] { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
            0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F };

    private static final int[][] REFERENCE_OUTPUT = {
            { 0x31, 0x0e, 0x0e, 0xdd, 0x47, 0xdb, 0x6f, 0x72, },
            { 0xfd, 0x67, 0xdc, 0x93, 0xc5, 0x39, 0xf8, 0x74, },
            { 0x5a, 0x4f, 0xa9, 0xd9, 0x09, 0x80, 0x6c, 0x0d, },
            { 0x2d, 0x7e, 0xfb, 0xd7, 0x96, 0x66, 0x67, 0x85, },
            { 0xb7, 0x87, 0x71, 0x27, 0xe0, 0x94, 0x27, 0xcf, },
            { 0x8d, 0xa6, 0x99, 0xcd, 0x64, 0x55, 0x76, 0x18, },
            { 0xce, 0xe3, 0xfe, 0x58, 0x6e, 0x46, 0xc9, 0xcb, },
            { 0x37, 0xd1, 0x01, 0x8b, 0xf5, 0x00, 0x02, 0xab, },
            { 0x62, 0x24, 0x93, 0x9a, 0x79, 0xf5, 0xf5, 0x93, },
            { 0xb0, 0xe4, 0xa9, 0x0b, 0xdf, 0x82, 0x00, 0x9e, },
            { 0xf3, 0xb9, 0xdd, 0x94, 0xc5, 0xbb, 0x5d, 0x7a, },
            { 0xa7, 0xad, 0x6b, 0x22, 0x46, 0x2f, 0xb3, 0xf4, },
            { 0xfb, 0xe5, 0x0e, 0x86, 0xbc, 0x8f, 0x1e, 0x75, },
            { 0x90, 0x3d, 0x84, 0xc0, 0x27, 0x56, 0xea, 0x14, },
            { 0xee, 0xf2, 0x7a, 0x8e, 0x90, 0xca, 0x23, 0xf7, },
            { 0xe5, 0x45, 0xbe, 0x49, 0x61, 0xca, 0x29, 0xa1, },
            { 0xdb, 0x9b, 0xc2, 0x57, 0x7f, 0xcc, 0x2a, 0x3f, },
            { 0x94, 0x47, 0xbe, 0x2c, 0xf5, 0xe9, 0x9a, 0x69, },
            { 0x9c, 0xd3, 0x8d, 0x96, 0xf0, 0xb3, 0xc1, 0x4b, },
            { 0xbd, 0x61, 0x79, 0xa7, 0x1d, 0xc9, 0x6d, 0xbb, },
            { 0x98, 0xee, 0xa2, 0x1a, 0xf2, 0x5c, 0xd6, 0xbe, },
            { 0xc7, 0x67, 0x3b, 0x2e, 0xb0, 0xcb, 0xf2, 0xd0, },
            { 0x88, 0x3e, 0xa3, 0xe3, 0x95, 0x67, 0x53, 0x93, },
            { 0xc8, 0xce, 0x5c, 0xcd, 0x8c, 0x03, 0x0c, 0xa8, },
            { 0x94, 0xaf, 0x49, 0xf6, 0xc6, 0x50, 0xad, 0xb8, },
            { 0xea, 0xb8, 0x85, 0x8a, 0xde, 0x92, 0xe1, 0xbc, },
            { 0xf3, 0x15, 0xbb, 0x5b, 0xb8, 0x35, 0xd8, 0x17, },
            { 0xad, 0xcf, 0x6b, 0x07, 0x63, 0x61, 0x2e, 0x2f, },
            { 0xa5, 0xc9, 0x1d, 0xa7, 0xac, 0xaa, 0x4d, 0xde, },
            { 0x71, 0x65, 0x95, 0x87, 0x66, 0x50, 0xa2, 0xa6, },
            { 0x28, 0xef, 0x49, 0x5c, 0x53, 0xa3, 0x87, 0xad, },
            { 0x42, 0xc3, 0x41, 0xd8, 0xfa, 0x92, 0xd8, 0x32, },
            { 0xce, 0x7c, 0xf2, 0x72, 0x2f, 0x51, 0x27, 0x71, },
            { 0xe3, 0x78, 0x59, 0xf9, 0x46, 0x23, 0xf3, 0xa7, },
            { 0x38, 0x12, 0x05, 0xbb, 0x1a, 0xb0, 0xe0, 0x12, },
            { 0xae, 0x97, 0xa1, 0x0f, 0xd4, 0x34, 0xe0, 0x15, },
            { 0xb4, 0xa3, 0x15, 0x08, 0xbe, 0xff, 0x4d, 0x31, },
            { 0x81, 0x39, 0x62, 0x29, 0xf0, 0x90, 0x79, 0x02, },
            { 0x4d, 0x0c, 0xf4, 0x9e, 0xe5, 0xd4, 0xdc, 0xca, },
            { 0x5c, 0x73, 0x33, 0x6a, 0x76, 0xd8, 0xbf, 0x9a, },
            { 0xd0, 0xa7, 0x04, 0x53, 0x6b, 0xa9, 0x3e, 0x0e, },
            { 0x92, 0x59, 0x58, 0xfc, 0xd6, 0x42, 0x0c, 0xad, },
            { 0xa9, 0x15, 0xc2, 0x9b, 0xc8, 0x06, 0x73, 0x18, },
            { 0x95, 0x2b, 0x79, 0xf3, 0xbc, 0x0a, 0xa6, 0xd4, },
            { 0xf2, 0x1d, 0xf2, 0xe4, 0x1d, 0x45, 0x35, 0xf9, },
            { 0x87, 0x57, 0x75, 0x19, 0x04, 0x8f, 0x53, 0xa9, },
            { 0x10, 0xa5, 0x6c, 0xf5, 0xdf, 0xcd, 0x9a, 0xdb, },
            { 0xeb, 0x75, 0x09, 0x5c, 0xcd, 0x98, 0x6c, 0xd0, },
            { 0x51, 0xa9, 0xcb, 0x9e, 0xcb, 0xa3, 0x12, 0xe6, },
            { 0x96, 0xaf, 0xad, 0xfc, 0x2c, 0xe6, 0x66, 0xc7, },
            { 0x72, 0xfe, 0x52, 0x97, 0x5a, 0x43, 0x64, 0xee, },
            { 0x5a, 0x16, 0x45, 0xb2, 0x76, 0xd5, 0x92, 0xa1, },
            { 0xb2, 0x74, 0xcb, 0x8e, 0xbf, 0x87, 0x87, 0x0a, },
            { 0x6f, 0x9b, 0xb4, 0x20, 0x3d, 0xe7, 0xb3, 0x81, },
            { 0xea, 0xec, 0xb2, 0xa3, 0x0b, 0x22, 0xa8, 0x7f, },
            { 0x99, 0x24, 0xa4, 0x3c, 0xc1, 0x31, 0x57, 0x24, },
            { 0xbd, 0x83, 0x8d, 0x3a, 0xaf, 0xbf, 0x8d, 0xb7, },
            { 0x0b, 0x1a, 0x2a, 0x32, 0x65, 0xd5, 0x1a, 0xea, },
            { 0x13, 0x50, 0x79, 0xa3, 0x23, 0x1c, 0xe6, 0x60, },
            { 0x93, 0x2b, 0x28, 0x46, 0xe4, 0xd7, 0x06, 0x66, },
            { 0xe1, 0x91, 0x5f, 0x5c, 0xb1, 0xec, 0xa4, 0x6c, },
            { 0xf3, 0x25, 0x96, 0x5c, 0xa1, 0x6d, 0x62, 0x9f, },
            { 0x57, 0x5f, 0xf2, 0x8e, 0x60, 0x38, 0x1b, 0xe5, },
            { 0x72, 0x45, 0x06, 0xeb, 0x4c, 0x32, 0x8a, 0x95, }
    };

    static class ReferenceTestParam {
        private final byte[] input;
        private final long expectedOutput;

        ReferenceTestParam(byte[] input, long expectedOutput) {
            this.input = input;
            this.expectedOutput = expectedOutput;
        }
    }

    static ReferenceTestParam[] params() {
        ReferenceTestParam[] testCases = new ReferenceTestParam[64];
        // Use little-endian as this is what is expected by the reference output.
        ByteBuffer bb = ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN);
        for (int i = 0; i < testCases.length; ++i) {
            byte[] input = new byte[i];
            for (int j = 0; j < input.length; ++j) {
                input[j] = (byte) j;
            }
            for (int v: REFERENCE_OUTPUT[i]) {
                bb.put((byte) v);
            }
            bb.flip();
            testCases[i] = new ReferenceTestParam(input, bb.getLong());
            bb.clear();
        }
        return testCases;
    }

    @ParameterizedTest
    @MethodSource("params")
    void testReferenceCases(ReferenceTestParam param ) {
        SipHash hash = new SipHash(2, 4, REFERENCE_SEED);
        long h = hash.macHash(ByteBuffer.wrap(param.input));
        assertEquals(param.expectedOutput, h);
    }

    @Test
    void testHash() {
        byte[] bytes = new byte[64];
        ThreadLocalRandom.current().nextBytes(bytes);

        byte[] seed = new byte[16];
        ThreadLocalRandom.current().nextBytes(seed);
        SipHash hash = new SipHash(1, 3, seed);

        ByteBuffer buffer = ByteBuffer.wrap(bytes);
        ByteBuffer emptyBuffer = ByteBuffer.wrap(new byte[0]);

        long bufferHash = hash.macHash(buffer);
        assertEquals(bufferHash, hash.macHash(buffer));

        long emptyBufferHash = hash.macHash(emptyBuffer);
        assertEquals(emptyBufferHash, hash.macHash(emptyBuffer));

        assertNotEquals(bufferHash, emptyBufferHash);
    }
}
```
