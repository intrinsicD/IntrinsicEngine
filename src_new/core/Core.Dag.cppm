module;

#include <concepts>
#include <cstdint>
#include <span>

export module Core.TaskGraph;


namespace Extrinsic::Core
{
    template <typename H>
    concept NodeHandle = requires(H h)
    {
        { h.Index } -> std::convertible_to<uint32_t>;
        { h.Generation } -> std::convertible_to<uint32_t>;
    };

    template <typename H>
    concept EdgeHandle = requires(H h)
    {
        { h.Index } -> std::convertible_to<uint32_t>;
        { h.Generation } -> std::convertible_to<uint32_t>;
    };

    class TaskGraph
    {
    public:
        TaskGraph() = default;
        ~TaskGraph() = default;

        NodeHandle AddNode();

        EdgeHandle AddDependency(NodeHandle source, std::span<NodeHandle> reads, std::span<NodeHandle> writes);

    private:
    };
}
