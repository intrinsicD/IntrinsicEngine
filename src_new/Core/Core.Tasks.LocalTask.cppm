module;

#include <memory>

export module Extrinsic.Core.Tasks:LocalTask;

namespace Extrinsic::Core::Tasks
{
    export class LocalTask
    {
        static constexpr size_t STORAGE_SIZE = 120; // 128 total - sizeof(Concept*)

        struct Concept
        {
            virtual ~Concept() = default;
            virtual void Execute() = 0;
            virtual void MoveTo(void* dest) = 0;
        };

        template <typename T>
        struct Model final : Concept
        {
            T payload;

            explicit Model(T&& p) : payload(std::move(p))
            {
            }

            void Execute() override { payload(); }

            void MoveTo(void* dest) override
            {
                std::construct_at(static_cast<Model*>(dest), std::move(payload));
            }
        };

        alignas(std::max_align_t) std::byte m_Storage[STORAGE_SIZE]{}; // Value-initialize to zero
        Concept* m_VTable = nullptr; // Points to m_Storage (reinterpreted)

    public:
        LocalTask() = default;

        // Constructor for lambdas
        template <typename F> requires (!std::is_same_v<std::decay_t<F>, LocalTask>)
        LocalTask(F&& f)
        {
            using Type = std::decay_t<F>;
            static_assert(sizeof(Model<Type>) <= STORAGE_SIZE,
                          "Task lambda capture is too big! Use pointers or simplify captures.");
            static_assert(alignof(Model<Type>) <= alignof(std::max_align_t),
                          "Task alignment requirement too strict.");

            auto* ptr = reinterpret_cast<Model<Type>*>(m_Storage);
            std::construct_at(ptr, std::forward<F>(f));
            m_VTable = ptr;
        }

        ~LocalTask();

        // Move Constructor
        LocalTask(LocalTask&& other) noexcept;

        // Move Assignment
        LocalTask& operator=(LocalTask&& other) noexcept;

        // No copy
        LocalTask(const LocalTask&) = delete;
        LocalTask& operator=(const LocalTask&) = delete;

        void operator()() const;

        [[nodiscard]] bool Valid() const { return m_VTable != nullptr; }
    };
}
