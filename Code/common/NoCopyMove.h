#pragma once

// Deletes a class's copy and move constructors and assignments.
#define NO_COPY_MOVE(className)                                                                                                \
    className(className&&) = delete;                                                                                           \
    className(const className&) = delete;                                                                                      \
    className& operator=(const className&) = delete;                                                                           \
    className& operator=(className&&) = delete;
