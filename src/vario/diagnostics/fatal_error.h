#pragma once

// Provide information about an upcoming fatal error before triggering the final fatal error.
// Arguments follow the printf pattern.
void fatalErrorInfo(const char* msg, ...);

// Trigger a fatal error that ends normal program execution.  Additional information
// can be provided by calling fatalErrorInfo before calling this function.
// Arguments follow the printf pattern for the final fatal error message.
void fatalError(const char* msg, ...);
