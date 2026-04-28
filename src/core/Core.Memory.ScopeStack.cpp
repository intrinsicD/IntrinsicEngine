module;

#include <utility>

module Extrinsic.Core.Memory:ScopeStack.Impl;
import :ScopeStack;

namespace Extrinsic::Core::Memory
{
    ScopeStack::ScopeStack(ScopeStack&& other) noexcept
        : m_Arena(std::move(other.m_Arena))
          , m_Head(other.m_Head)
          , m_DestructorCount(other.m_DestructorCount)
    {
        other.m_Head = nullptr;
        other.m_DestructorCount = 0;
    }

    ScopeStack& ScopeStack::operator=(ScopeStack&& other) noexcept
    {
        if (this == &other)
            return *this;

        Reset();
        m_Arena = std::move(other.m_Arena);
        m_Head = other.m_Head;
        m_DestructorCount = other.m_DestructorCount;

        other.m_Head = nullptr;
        other.m_DestructorCount = 0;
        return *this;
    }

    void ScopeStack::Reset() noexcept
    {
        auto* cur = m_Head;
        while (cur)
        {
            cur->DestroyFn(cur->Ptr);
            cur = cur->Next;
        }

        m_Head = nullptr;
        m_DestructorCount = 0;
        m_Arena.Reset();
    }
}
