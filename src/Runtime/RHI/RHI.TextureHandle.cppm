module;

export module RHI.TextureHandle;

import Core.Handle;

export namespace RHI
{
    struct TextureTag {};
    using TextureHandle = Core::StrongHandle<TextureTag>;
}