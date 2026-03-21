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

    namespace Detail
    {
        template <class Domain, class ValueType, class DereferencePolicy, class AdvancePolicy>
        struct RingIterator
        {
            using iterator_concept = std::input_iterator_tag;
            using iterator_category = std::input_iterator_tag;
            using value_type = ValueType;
            using difference_type = std::ptrdiff_t;
            using reference = value_type;

            const Domain* Owner{};
            HalfedgeHandle Start{};
            HalfedgeHandle Current{};
            std::size_t RemainingSteps{0};
            bool Ended{true};

            [[nodiscard]] value_type operator*() const { return DereferencePolicy::Get(*Owner, Current); }

            RingIterator& operator++()
            {
                AdvancePolicy::Advance(*this);
                return *this;
            }

            RingIterator operator++(int)
            {
                RingIterator copy = *this;
                ++(*this);
                return copy;
            }

            friend bool operator==(const RingIterator& it, TraversalSentinel) { return it.Ended; }
            friend bool operator!=(const RingIterator& it, TraversalSentinel) { return !it.Ended; }
        };

        template <class Iterator>
        void EndTraversal(Iterator& it)
        {
            it.Ended = true;
            it.Current = {};
        }

        template <class Iterator, class StepFn>
        void AdvanceSimple(Iterator& it, StepFn step)
        {
            if (it.Ended || it.Owner == nullptr)
            {
                return;
            }

            if (it.RemainingSteps == 0)
            {
                EndTraversal(it);
                return;
            }

            const HalfedgeHandle next = step(*it.Owner, it.Current);
            --it.RemainingSteps;
            if (!next.IsValid() || next == it.Start)
            {
                EndTraversal(it);
                return;
            }

            it.Current = next;
        }

        template <class Iterator, class Domain>
        [[nodiscard]] Iterator MakeIterator(const Domain* owner, HalfedgeHandle start)
        {
            return Iterator{owner, start, start, owner->HalfedgesSize(), false};
        }

        template <class Domain>
        struct ReturnHalfedge
        {
            [[nodiscard]] static HalfedgeHandle Get(const Domain&, HalfedgeHandle current) { return current; }
        };

        template <class Domain>
        struct ReturnToVertex
        {
            [[nodiscard]] static VertexHandle Get(const Domain& owner, HalfedgeHandle current) { return owner.ToVertex(current); }
        };

        template <class Domain>
        struct ReturnFromVertex
        {
            [[nodiscard]] static VertexHandle Get(const Domain& owner, HalfedgeHandle current) { return owner.FromVertex(current); }
        };

        template <class Domain>
        struct ReturnFace
        {
            [[nodiscard]] static FaceHandle Get(const Domain& owner, HalfedgeHandle current) { return owner.Face(current); }
        };

        struct AdvanceViaNextHalfedge
        {
            template <class Iterator>
            static void Advance(Iterator& it)
            {
                AdvanceSimple(it, [](const auto& owner, HalfedgeHandle current) {
                    return owner.NextHalfedge(current);
                });
            }
        };

        struct AdvanceViaCWRotation
        {
            template <class Iterator>
            static void Advance(Iterator& it)
            {
                AdvanceSimple(it, [](const auto& owner, HalfedgeHandle current) {
                    return owner.CWRotatedHalfedge(current);
                });
            }
        };

        struct AdvanceViaCWRotationToValidFace
        {
            template <class Iterator>
            static void Advance(Iterator& it)
            {
                if (it.Ended || it.Owner == nullptr)
                {
                    return;
                }

                while (it.RemainingSteps > 0)
                {
                    const HalfedgeHandle next = it.Owner->CWRotatedHalfedge(it.Current);
                    --it.RemainingSteps;
                    if (!next.IsValid() || next == it.Start)
                    {
                        EndTraversal(it);
                        return;
                    }

                    it.Current = next;
                    const FaceHandle face = it.Owner->Face(it.Current);
                    if (face.IsValid() && !it.Owner->IsDeleted(face))
                    {
                        return;
                    }
                }

                EndTraversal(it);
            }
        };

        template <class Domain>
        using SimpleHalfedgeIterator = RingIterator<Domain, HalfedgeHandle, ReturnHalfedge<Domain>, AdvanceViaNextHalfedge>;

        template <class Domain>
        using FaceVertexIterator = RingIterator<Domain, VertexHandle, ReturnToVertex<Domain>, AdvanceViaNextHalfedge>;

        template <class Domain>
        using VertexHalfedgeIterator = RingIterator<Domain, HalfedgeHandle, ReturnHalfedge<Domain>, AdvanceViaCWRotation>;

        template <class Domain>
        using FaceIterator = RingIterator<Domain, FaceHandle, ReturnFace<Domain>, AdvanceViaCWRotationToValidFace>;

        template <class Domain>
        using BoundaryVertexIterator = RingIterator<Domain, VertexHandle, ReturnFromVertex<Domain>, AdvanceViaNextHalfedge>;
    }

    template <class Domain>
    struct HalfedgesAroundFaceRange
    {
        using Iterator = Detail::SimpleHalfedgeIterator<Domain>;

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

            return Detail::MakeIterator<Iterator>(Owner, h);
        }

        [[nodiscard]] static TraversalSentinel end() { return {}; }
    };

    template <class Domain>
    struct VerticesAroundFaceRange
    {
        using Iterator = Detail::FaceVertexIterator<Domain>;

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

            return Detail::MakeIterator<Iterator>(Owner, h);
        }

        [[nodiscard]] static TraversalSentinel end() { return {}; }
    };

    template <class Domain>
    struct HalfedgesAroundVertexRange
    {
        using Iterator = Detail::VertexHalfedgeIterator<Domain>;

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

            return Detail::MakeIterator<Iterator>(Owner, h);
        }

        [[nodiscard]] static TraversalSentinel end() { return {}; }
    };

    template <class Domain>
    struct FacesAroundVertexRange
    {
        using Iterator = Detail::FaceIterator<Domain>;

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
                    return Detail::MakeIterator<Iterator>(Owner, h);
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
        using Iterator = Detail::SimpleHalfedgeIterator<Domain>;

        const Domain* Owner{};
        HalfedgeHandle StartBoundary{};

        [[nodiscard]] Iterator begin() const
        {
            if (Owner == nullptr || Owner->HalfedgesSize() == 0 || !Owner->IsValid(StartBoundary) || Owner->IsDeleted(StartBoundary) || !Owner->IsBoundary(StartBoundary))
            {
                return {};
            }

            return Detail::MakeIterator<Iterator>(Owner, StartBoundary);
        }

        [[nodiscard]] static TraversalSentinel end() { return {}; }
    };

    template <class Domain>
    struct BoundaryVerticesRange
    {
        using Iterator = Detail::BoundaryVertexIterator<Domain>;

        const Domain* Owner{};
        HalfedgeHandle StartBoundary{};

        [[nodiscard]] Iterator begin() const
        {
            if (Owner == nullptr || Owner->HalfedgesSize() == 0 || !Owner->IsValid(StartBoundary) || Owner->IsDeleted(StartBoundary) || !Owner->IsBoundary(StartBoundary))
            {
                return {};
            }

            return Detail::MakeIterator<Iterator>(Owner, StartBoundary);
        }

        [[nodiscard]] static TraversalSentinel end() { return {}; }
    };
}
