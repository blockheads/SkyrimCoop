#pragma once

/**
 * @brief Dispatched when connection with the server fails.
 */

struct ConnectionErrorEvent
{
    ConnectionErrorEvent() = default;

    std::string ErrorDetail;
};
