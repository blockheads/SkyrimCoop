#pragma once

#include "Stl.hpp"

namespace TiltedPhoques
{
    template<class T, class U>
    struct Locked;

    template<class T, class TLock = std::mutex>
    struct Lockable
    {
        struct Data
        {
            template<class... Args>
            Data(Args&& ... aArgs)
                : Value(std::forward<Args>(aArgs)...)
            {}

            T Value;
            TLock Lock;
        };

        struct Ref
        {
            Ref(typename std::shared_ptr<Data>::weak_type apData)
                : m_pData(std::move(apData))
            {}

            [[nodiscard]] Locked<T, TLock> Lock() const
            {
                if (auto locked = m_pData.lock())
                {
                    return { locked };
                }

                return { {} };
            }

        private:
            typename std::shared_ptr<Data>::weak_type m_pData;
        };


        template<class... Args>
        Lockable(Args&& ... aArgs)
            : m_pData(std::make_shared<Data>(std::forward<Args>(aArgs)...))
        {}

        Ref AsRef() const noexcept
        {
            return Ref(m_pData);
        }

        [[nodiscard]] Locked<T, TLock> Lock() const
        {
            return AsRef().Lock();
        }

    private:

        friend struct Locked<T, TLock>;

        std::shared_ptr<Data> m_pData;
    };
}

#include "Locked.hpp"
