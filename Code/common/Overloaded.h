#pragma once

// One callable from several lambdas, for std::visit.
template <class... Ts> struct Overloaded : Ts...
{
    using Ts::operator()...;
};
