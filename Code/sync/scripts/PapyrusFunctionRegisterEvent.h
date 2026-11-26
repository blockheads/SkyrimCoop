#pragma once

/**
 * @brief Dispatched when the game code registers a Papyrus function internally.
 */
struct PapyrusFunctionRegisterEvent
{
    explicit PapyrusFunctionRegisterEvent(const char* acpFunctionName, const char* acpNamespace, void* apFunctionAddress)
        : Name(acpFunctionName)
        , Namespace(acpNamespace)
        , Function(apFunctionAddress)
    {
    }

    std::string Name;
    std::string Namespace;
    void* Function;
};
