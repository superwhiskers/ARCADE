#pragma once

/// Broadcast a UDP message.
void UDP_Server(char *msg);

/// Send a stop message over UDP.
void UDP_Stop(void);

// Port to broadcast UDP messages on.
#define UDP_PORT 8000
