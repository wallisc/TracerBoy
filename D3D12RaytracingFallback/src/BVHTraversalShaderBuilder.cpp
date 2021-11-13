//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************
#include "pch.h"

#if PROFILE_STATE_MACHINE
#include "CompiledShaders\NullTraverseLib.h"
#endif

namespace FallbackLayer
{
        void BVHTraversalShaderBuilder::Compile(
            _In_ bool IsAnyHitOrIntersectionUsed,
            _Out_ TraversalShader &traversalShader)
    {
    }

}
