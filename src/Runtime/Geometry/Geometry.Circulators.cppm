module;

#include <cstddef>
#include <iterator>

export module Geometry.Circulators;

import Geometry.Properties;

export namespace Geometry::Circulators
{
    struct TraversalSentinel
    {
    };

    template <class Domain>
    struct HalfedgesAroundFaceRange
    {
        struct Iterator
        {
            using iterator_concept = std::input_iterator_tag;
            using iterator_category = std::input_iterator_tag;
            using value_type = HalfedgeHandle;
            using difference_type = std::ptrdiff_t;
            using reference = value_type;

            const Domain* Owner{};
            HalfedgeHandle Start{};
            HalfedgeHandle Current{};
            std::size_t RemainingSteps{0};
            bool Ended{true};

            [[nodiscard]] value_type operator*() const { return Current; }

            Iterator& operator++()
            {
                if (Ended || Owner == nullptr)
                {
                    return *this;
                }

                if (RemainingSteps == 0)
                {
                    Ended = true;
                    Current = {};
                    return *this;
                }

                const HalfedgeHandle next = Owner->NextHalfedge(Current);
                --RemainingSteps;
                if (!next.IsValid() || next == Start)
                {
                    Ended = true;
                    Current = {};
                    return *this;
                }

                Current = next;
                return *this;
            }

            Iterator operator++(int)
            {
                Iterator copy = *this;
                ++(*this);
                return copy;
            }

            friend bool operator==(const Iterator& it, TraversalSentinel) { return it.Ended; }
            friend bool operator!=(const Iterator& it, TraversalSentinel) { return !it.Ended; }
        };

        const Domain* Owner{};
        FaceHandle FaceValue{};

        [[nodiscard]] Iterator begin() const
        {
            if (Owner == nullptr || Owner->HalfedgesSize() == 0 || !Owner->IsValid(FaceValue) || Owner->IsDeleted(FaceValue))
            {
                return {};
            }

            const HalfedgeHandle h = Owner->Halfedge(FaceValue);
            if (!h.IsValid())
            {
                return {};
            }

            return Iterator{Owner, h, h, Owner->HalfedgesSize(), false};
        }

        [[nodiscard]] static TraversalSentinel end() { return {}; }
    };

    template <class Domain>
    struct VerticesAroundFaceRange
    {
        struct Iterator
        {
            using iterator_concept = std::input_iterator_tag;
            using iterator_category = std::input_iterator_tag;
            using value_type = VertexHandle;
            using difference_type = std::ptrdiff_t;
            using reference = value_type;

            const Domain* Owner{};
            HalfedgeHandle Start{};
            HalfedgeHandle Current{};
            std::size_t RemainingSteps{0};
            bool Ended{true};

            [[nodiscard]] value_type operator*() const { return Owner->ToVertex(Current); }

            Iterator& operator++()
            {
                if (Ended || Owner == nullptr)
                {
                    return *this;
                }

                if (RemainingSteps == 0)
                {
                    Ended = true;
                    Current = {};
                    return *this;
                }

                const HalfedgeHandle next = Owner->NextHalfedge(Current);
                --RemainingSteps;
                if (!next.IsValid() || next == Start)
                {
                    Ended = true;
                    Current = {};
                    return *this;
                }

                Current = next;
                return *this;
            }

            Iterator operator++(int)
            {
                Iterator copy = *this;
                ++(*this);
                return copy;
            }

            friend bool operator==(const Iterator& it, TraversalSentinel) { return it.Ended; }
            friend bool operator!=(const Iterator& it, TraversalSentinel) { return !it.Ended; }
        };

        const Domain* Owner{};
        FaceHandle FaceValue{};

        [[nodiscard]] Iterator begin() const
        {
            if (Owner == nullptr || Owner->HalfedgesSize() == 0 || !Owner->IsValid(FaceValue) || Owner->IsDeleted(FaceValue))
            {
                return {};
            }

            const HalfedgeHandle h = Owner->Halfedge(FaceValue);
            if (!h.IsValid())
            {
                return {};
            }

            return Iterator{Owner, h, h, Owner->HalfedgesSize(), false};
        }

        [[nodiscard]] static TraversalSentinel end() { return {}; }
    };

    template <class Domain>
    struct HalfedgesAroundVertexRange
    {
        struct Iterator
        {
            using iterator_concept = std::input_iterator_tag;
            using iterator_category = std::input_iterator_tag;
            using value_type = HalfedgeHandle;
            using difference_type = std::ptrdiff_t;
            using reference = value_type;

            const Domain* Owner{};
            HalfedgeHandle Start{};
            HalfedgeHandle Current{};
            std::size_t RemainingSteps{0};
            bool Ended{true};

            [[nodiscard]] value_type operator*() const { return Current; }

            Iterator& operator++()
            {
                if (Ended || Owner == nullptr)
                {
                    return *this;
                }

                if (RemainingSteps == 0)
                {
                    Ended = true;
                    Current = {};
                    return *this;
                }

                const HalfedgeHandle next = Owner->CWRotatedHalfedge(Current);
                --RemainingSteps;
                if (!next.IsValid() || next == Start)
                {
                    Ended = true;
                    Current = {};
                    return *this;
                }

                Current = next;
                return *this;
            }

            Iterator operator++(int)
            {
                Iterator copy = *this;
                ++(*this);
                return copy;
            }

            friend bool operator==(const Iterator& it, TraversalSentinel) { return it.Ended; }
            friend bool operator!=(const Iterator& it, TraversalSentinel) { return !it.Ended; }
        };

        const Domain* Owner{};
        VertexHandle VertexValue{};

        [[nodiscard]] Iterator begin() const
        {
            if (Owner == nullptr || Owner->HalfedgesSize() == 0 || !Owner->IsValid(VertexValue) || Owner->IsDeleted(VertexValue) || Owner->IsIsolated(VertexValue))
            {
                return {};
            }

            const HalfedgeHandle h = Owner->Halfedge(VertexValue);
            if (!h.IsValid())
            {
                return {};
            }

            return Iterator{Owner, h, h, Owner->HalfedgesSize(), false};
        }

        [[nodiscard]] static TraversalSentinel end() { return {}; }
    };

    template <class Domain>
    struct FacesAroundVertexRange
    {
        struct Iterator
        {
            using iterator_concept = std::input_iterator_tag;
            using iterator_category = std::input_iterator_tag;
            using value_type = FaceHandle;
            using difference_type = std::ptrdiff_t;
            using reference = value_type;

            const Domain* Owner{};
            HalfedgeHandle Start{};
            HalfedgeHandle Current{};
            std::size_t RemainingSteps{0};
            bool Ended{true};

            [[nodiscard]] value_type operator*() const { return Owner->Face(Current); }

            Iterator& operator++()
            {
                if (Ended || Owner == nullptr)
                {
                    return *this;
                }

                while (RemainingSteps > 0)
                {
                    const HalfedgeHandle next = Owner->CWRotatedHalfedge(Current);
                    --RemainingSteps;
                    if (!next.IsValid() || next == Start)
                    {
                        Ended = true;
                        Current = {};
                        return *this;
                    }

                    Current = next;
                    const FaceHandle f = Owner->Face(Current);
                    if (f.IsValid() && !Owner->IsDeleted(f))
                    {
                        return *this;
                    }
                }

                Ended = true;
                Current = {};
                return *this;
            }

            Iterator operator++(int)
            {
                Iterator copy = *this;
                ++(*this);
                return copy;
            }

            friend bool operator==(const Iterator& it, TraversalSentinel) { return it.Ended; }
            friend bool operator!=(const Iterator& it, TraversalSentinel) { return !it.Ended; }
        };

        const Domain* Owner{};
        VertexHandle VertexValue{};

        [[nodiscard]] Iterator begin() const
        {
            if (Owner == nullptr || Owner->HalfedgesSize() == 0 || !Owner->IsValid(VertexValue) || Owner->IsDeleted(VertexValue) || Owner->IsIsolated(VertexValue))
            {
                return {};
            }

            HalfedgeHandle h = Owner->Halfedge(VertexValue);
            if (!h.IsValid())
            {
                return {};
            }

            const HalfedgeHandle ringStart = h;
            std::size_t remaining = Owner->HalfedgesSize();
            while (remaining > 0)
            {
                const FaceHandle f = Owner->Face(h);
                if (f.IsValid() && !Owner->IsDeleted(f))
                {
                    return Iterator{Owner, h, h, Owner->HalfedgesSize(), false};
                }

                h = Owner->CWRotatedHalfedge(h);
                --remaining;
                if (!h.IsValid() || h == ringStart)
                {
                    return {};
                }
            }

            return {};
        }

        [[nodiscard]] static TraversalSentinel end() { return {}; }
    };

    template <class Domain>
    struct BoundaryHalfedgesRange
    {
        struct Iterator
        {
            using iterator_concept = std::input_iterator_tag;
            using iterator_category = std::input_iterator_tag;
            using value_type = HalfedgeHandle;
            using difference_type = std::ptrdiff_t;
            using reference = value_type;

            const Domain* Owner{};
            HalfedgeHandle Start{};
            HalfedgeHandle Current{};
            std::size_t RemainingSteps{0};
            bool Ended{true};

            [[nodiscard]] value_type operator*() const { return Current; }

            Iterator& operator++()
            {
                if (Ended || Owner == nullptr)
                {
                    return *this;
                }

                if (RemainingSteps == 0)
                {
                    Ended = true;
                    Current = {};
                    return *this;
                }

                const HalfedgeHandle next = Owner->NextHalfedge(Current);
                --RemainingSteps;
                if (!next.IsValid() || next == Start)
                {
                    Ended = true;
                    Current = {};
                    return *this;
                }

                Current = next;
                return *this;
            }

            Iterator operator++(int)
            {
                Iterator copy = *this;
                ++(*this);
                return copy;
            }

            friend bool operator==(const Iterator& it, TraversalSentinel) { return it.Ended; }
            friend bool operator!=(const Iterator& it, TraversalSentinel) { return !it.Ended; }
        };

        const Domain* Owner{};
        HalfedgeHandle StartBoundary{};

        [[nodiscard]] Iterator begin() const
        {
            if (Owner == nullptr || Owner->HalfedgesSize() == 0 || !Owner->IsValid(StartBoundary) || Owner->IsDeleted(StartBoundary) || !Owner->IsBoundary(StartBoundary))
            {
                return {};
            }

            return Iterator{Owner, StartBoundary, StartBoundary, Owner->HalfedgesSize(), false};
        }

        [[nodiscard]] static TraversalSentinel end() { return {}; }
    };

    template <class Domain>
    struct BoundaryVerticesRange
    {
        struct Iterator
        {
            using iterator_concept = std::input_iterator_tag;
            using iterator_category = std::input_iterator_tag;
            using value_type = VertexHandle;
            using difference_type = std::ptrdiff_t;
            using reference = value_type;

            const Domain* Owner{};
            HalfedgeHandle Start{};
            HalfedgeHandle Current{};
            std::size_t RemainingSteps{0};
            bool Ended{true};

            [[nodiscard]] value_type operator*() const { return Owner->FromVertex(Current); }

            Iterator& operator++()
            {
                if (Ended || Owner == nullptr)
                {
                    return *this;
                }

                if (RemainingSteps == 0)
                {
                    Ended = true;
                    Current = {};
                    return *this;
                }

                const HalfedgeHandle next = Owner->NextHalfedge(Current);
                --RemainingSteps;
                if (!next.IsValid() || next == Start)
                {
                    Ended = true;
                    Current = {};
                    return *this;
                }

                Current = next;
                return *this;
            }

            Iterator operator++(int)
            {
                Iterator copy = *this;
                ++(*this);
                return copy;
            }

            friend bool operator==(const Iterator& it, TraversalSentinel) { return it.Ended; }
            friend bool operator!=(const Iterator& it, TraversalSentinel) { return !it.Ended; }
        };

        const Domain* Owner{};
        HalfedgeHandle StartBoundary{};

        [[nodiscard]] Iterator begin() const
        {
            if (Owner == nullptr || Owner->HalfedgesSize() == 0 || !Owner->IsValid(StartBoundary) || Owner->IsDeleted(StartBoundary) || !Owner->IsBoundary(StartBoundary))
            {
                return {};
            }

            return Iterator{Owner, StartBoundary, StartBoundary, Owner->HalfedgesSize(), false};
        }

        [[nodiscard]] static TraversalSentinel end() { return {}; }
    };
}

