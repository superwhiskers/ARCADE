#pragma once

#include <stdbool.h>
#include <stddef.h>

/// Load and initialize the configuration.
int init_Server(void);

/// Helper for opening a file and memory-mapping it.
///
/// The address of the mapping is returned, and the length of the file is stored
/// in `len`.
///
/// The caller is responsible for calling `munmap(2)` to deallocate the memory
/// used for the file.
char *OpenFile(const char *filename, size_t *len);

/// Get the executable of the simulator from the configuration.
///
/// Returned strings are owned by the caller and must be freed.
char *SimName(void);

/// Read a flag from the configuration.
bool Read_flags(char *category, char *flagname, bool fallback);

/// Read a variable from the configuration.
char *Read_Vars(char *category, char *varname, char *fallback);
