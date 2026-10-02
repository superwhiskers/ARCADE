#pragma once

/// Maximum number of outputs.
#define MAX_ZMQ_VARS 512

/// Mapping of string names to output indices.
typedef struct {
    /// Name of output.
    char tag[65];

    /// Index of output inside the `PUB_DATA` structure.
    int index;
} ZMQ_TagMap;

/// Function to run the ZeroMQ thread.
void *ZMQ_Client(void *);
