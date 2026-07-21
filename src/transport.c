/*
 * Copyright (c) 2026 Pritam
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * transport.c — Abstract I/O layer over plain and TLS sockets
 *
 * Transport wraps a raw fd so the rest of the server can read/write
 * without caring about the underlying socket details.
 */

#include "transport.h"

#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include "log.h"

#ifdef SUPPORT_TLS_E
#include "tlse.h"
#endif  // SUPPORT_TLS_E

// Opaque Transport struct
struct Transport {
	int  fd;        // Underlying socket fd
#ifdef SUPPORT_TLS_E
	struct TLSContext *tls_ctx;  // NULL = plain socket
#endif  // SUPPORT_TLS_E
};

// Allocate a new transport for a connected socket.
Transport *transport_new(int fd)
{
	Transport *t = calloc(1, sizeof(*t));
	if (!t) {
		LOG_ERROR("calloc failed for transport_new (fd=%d)", fd);
		return NULL;
	}
	t->fd = fd;
#ifdef SUPPORT_TLS_E
	t->tls_ctx = NULL;
#endif  // SUPPORT_TLS_E

	// Disable Nagle's algorithm for lower latency on small responses
	int one = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);

	LOG_TRACE("Transport created (fd=%d)", fd);
	return t;
}

#ifdef SUPPORT_TLS_E
void transport_set_tls(Transport *t, struct TLSContext *master_ctx)
{
	if (!t) return;
	// Create a child TLS context from the master server context.
	// tls_accept() returns a per-connection context for the handshake.
	t->tls_ctx = tls_accept(master_ctx);
	if (!t->tls_ctx) {
		LOG_ERROR("tls_accept() failed for fd=%d", t->fd);
	}
}

// Perform the TLS handshake on an accepted connection.
// Returns 0 on success, -1 on error.
static int transport_tls_handshake(Transport *t)
{
	if (!t || !t->tls_ctx) return -1;

	unsigned char buf[4096];
	int max_rounds = 20;  // guard against infinite loops

	for (int i = 0; i < max_rounds; i++) {
		if (tls_established(t->tls_ctx))
			return 0;

		// Read encrypted data from the client
		ssize_t n = read(t->fd, buf, sizeof(buf));
		if (n <= 0) {
			LOG_WARN("TLS handshake: read failed (fd=%d, n=%zd)", t->fd, n);
			return -1;
		}

		// Feed it into the TLS state machine
		int rc = tls_consume_stream(t->tls_ctx, buf, (int)n, NULL);
		if (rc == TLS_GENERIC_ERROR || rc == TLS_BROKEN_PACKET) {
			LOG_WARN("TLS handshake: consume_stream error %d (fd=%d)", rc, t->fd);
			return -1;
		}

		// Flush any pending TLS records back to the client
		unsigned int out_len = 0;
		const unsigned char *out = tls_get_write_buffer(t->tls_ctx, &out_len);
		if (out && out_len > 0) {
			size_t written = 0;
			while (written < out_len) {
				ssize_t w = write(t->fd, out + written, out_len - written);
				if (w <= 0) {
					if (w < 0 && errno == EINTR) continue;
					LOG_WARN("TLS handshake: write failed (fd=%d)", t->fd);
					return -1;
				}
				written += (size_t)w;
			}
		}
		tls_buffer_clear(t->tls_ctx);
	}

	LOG_WARN("TLS handshake: max rounds exceeded (fd=%d)", t->fd);
	return -1;
}
#endif  // SUPPORT_TLS_E

// For plain sockets: no-op, returns 0.
// For TLS: performs the TLS handshake.
int transport_accept(Transport *t)
{
	if (!t) return -1;
#ifdef SUPPORT_TLS_E
	if (t->tls_ctx)
		return transport_tls_handshake(t);
#endif  // SUPPORT_TLS_E
	return 0;
}

// Read up to len bytes from the transport.
// Returns the number of bytes read, 0 on EOF, or -1 on error.
ssize_t transport_read(Transport *t, void *buf, size_t len)
{
	if (!t) return -1;
#ifdef SUPPORT_TLS_E
	if (t->tls_ctx) {
		// Check if TLS has pending decrypted data first
		if (tls_pending(t->tls_ctx) > 0) {
			return (ssize_t)tls_read(t->tls_ctx, (unsigned char *)buf, (unsigned int)len);
		}

		// Read encrypted bytes from socket, feed to TLS, return plaintext
		unsigned char raw[16384];
		ssize_t n = read(t->fd, raw, sizeof(raw));
		if (n <= 0) return n;

		int rc = tls_consume_stream(t->tls_ctx, (unsigned char *)raw, (int)n, NULL);
		if (rc < 0 && rc != TLS_NEED_MORE_DATA) {
			LOG_WARN("TLS read: consume_stream error %d (fd=%d)", rc, t->fd);
			return -1;
		}

		// Flush any pending TLS response (e.g., alerts, renegotiation)
		unsigned int out_len = 0;
		const unsigned char *out = tls_get_write_buffer(t->tls_ctx, &out_len);
		if (out && out_len > 0) {
			size_t written = 0;
			while (written < out_len) {
				ssize_t w = write(t->fd, out + written, out_len - written);
				if (w <= 0) break;
				written += (size_t)w;
			}
			tls_buffer_clear(t->tls_ctx);
		}

		// Now read the decrypted plaintext
		return (ssize_t)tls_read(t->tls_ctx, (unsigned char *)buf, (unsigned int)len);
	}
#endif  // SUPPORT_TLS_E
	return read(t->fd, buf, len);
}

// Write len bytes to the transport (handles partial writes internally).
// Returns the number of bytes written, or -1 on error.
ssize_t transport_write(Transport *t, const void *buf, size_t len)
{
	if (!t) return -1;

#ifdef SUPPORT_TLS_E
	if (t->tls_ctx) {
		// Encrypt plaintext in chunks — tls_write may not accept the full
		// buffer at once due to internal TLS record/plaintext size limits.
		unsigned char *src = (unsigned char *)buf;
		size_t remaining = len;

		while (remaining > 0) {
			unsigned int chunk = (remaining > 16384) ? 16384 : (unsigned int)remaining;
			int written = tls_write(t->tls_ctx, src, chunk);
			if (written <= 0) {
				LOG_WARN("TLS write: tls_write failed (fd=%d, rc=%d)", t->fd, written);
				return -1;
			}
			src += written;
			remaining -= (size_t)written;

			// Flush encrypted output to the socket
			unsigned int out_len = 0;
			const unsigned char *out = tls_get_write_buffer(t->tls_ctx, &out_len);
			if (out && out_len > 0) {
				size_t sent = 0;
				while (sent < out_len) {
					ssize_t n = write(t->fd, out + sent, out_len - sent);
					if (n <= 0) {
						if (n < 0 && errno == EINTR) continue;
						LOG_WARN("TLS write: socket write failed (fd=%d)", t->fd);
						return -1;
					}
					sent += (size_t)n;
				}
			}
			tls_buffer_clear(t->tls_ctx);
		}
		return (ssize_t)len;
	}
#endif  // SUPPORT_TLS_E

	// SIGPIPE is ignored in server.c
	size_t written = 0;
	while (written < len) {
		ssize_t n = write(t->fd, (const char *)buf + written, len - written);
		if (n <= 0) {
			if (n < 0 && errno == EINTR) continue;
			break;
		}
		written += (size_t)n;
	}
	return (ssize_t)written;
}

// Scatter-gather write using writev() - writes multiple buffers in one syscall
// For TLS: concatenates into a single buffer and encrypts as one record.
// Returns total bytes written, or -1 on error
ssize_t transport_writev(Transport *t, const struct iovec *iov, int iovcnt)
{
	if (!t) return -1;

#ifdef SUPPORT_TLS_E
	if (t->tls_ctx) {
		// Write each iov entry separately — TLS handles record framing.
		// Concatenating into one huge buffer hits tls_write's max plaintext limit.
		size_t total = 0;
		for (int i = 0; i < iovcnt; i++) {
			ssize_t n = transport_write(t, iov[i].iov_base, iov[i].iov_len);
			if (n < 0 || (size_t)n != iov[i].iov_len)
				return -1;
			total += (size_t)n;
		}
		return (ssize_t)total;
	}
#endif  // SUPPORT_TLS_E

	// SIGPIPE is ignored in server.c
	return writev(t->fd, iov, iovcnt);
}

// Close the transport and free resources.
void transport_close(Transport *t)
{
	if (!t) return;

#ifdef SUPPORT_TLS_E
	if (t->tls_ctx) {
		// Send close_notify before closing the socket
		tls_close_notify(t->tls_ctx);
		tls_destroy_context(t->tls_ctx);
		t->tls_ctx = NULL;
	}
#endif  // SUPPORT_TLS_E

	if (t->fd >= 0) {
		LOG_TRACE("Closing socket (fd=%d)", t->fd);
		close(t->fd);
		t->fd = -1;
	}
}

// Close and free the transport, nullifying the caller's pointer.
void transport_destroy(Transport **t)
{
	if (!t || !*t)
		return;
	transport_close(*t);
	free(*t);
	*t = NULL;
}

// Set a receive timeout on the underlying socket.
int transport_set_timeout(Transport *t, int seconds)
{
	if (!t || t->fd < 0) return -1;
	struct timeval tv = { .tv_sec = seconds, .tv_usec = 0 };
	int rc = setsockopt(t->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
	LOG_TRACE("Set SO_RCVTIMEO=%ds on fd=%d -> %d", seconds, t->fd, rc);
	return rc;
}

// Return the underlying socket fd (for logging / debugging).
int transport_fd(const Transport *t)
{
	return t ? t->fd : -1;
}

bool transport_is_tls(const Transport *t)
{
#ifdef SUPPORT_TLS_E
	return t && t->tls_ctx != NULL;
#else
	(void)t;
	return false;
#endif  // SUPPORT_TLS_E
}
