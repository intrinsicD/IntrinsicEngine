module;

export module RHI.BufferHandle;

import Core.Handle;

export namespace RHI
{
    struct BufferTag
    {
    };

    using BufferHandle = Core::StrongHandle<BufferTag>;
}
