#define PURE_CUDA
#include "../svgf_shared.h"

using namespace shared;

CUDA_DEVICE_KERNEL void debugVisualize(
    BufferToDisplay bufferToDisplay,
    float motionVectorOffset, float motionVectorScale) {
    uint2 launchIndex = make_uint2(blockDim.x * blockIdx.x + threadIdx.x,
                                   blockDim.y * blockIdx.y + threadIdx.y);
    int2 pix = make_int2(launchIndex.x, launchIndex.y);
    int2 imageSize = plp.s->imageSize;
    if (launchIndex.x >= imageSize.x ||
        launchIndex.y >= imageSize.y)
        return;

    uint32_t curBufIdx = plp.f->bufferIndex;
    const StaticPipelineLaunchParameters::TemporalSet &staticTemporalSet =
        plp.s->temporalSets[curBufIdx];
    const PerFramePipelineLaunchParameters::TemporalSet &perFrameTemporalSet =
        plp.f->temporalSets[curBufIdx];

    float4 color = make_float4(0.0f, 0.0f, 0.0f, 1.0f);
    switch (bufferToDisplay) {
    case BufferToDisplay::NoisyBeauty:
        break;
    case BufferToDisplay::Albedo: {
        Albedo albedo = plp.s->albedoBuffer.read(pix);
        color = make_float4(albedo.dhReflectance, 1.0f);
        break;
    }
    case BufferToDisplay::Normal: {
        GBuffer1 gBuffer1 = perFrameTemporalSet.GBuffer1.read(glPix(pix));
        color = make_float4(0.5f * gBuffer1.normalInWorld + make_float3(0.5f), 1.0f);
        break;
    }
    case BufferToDisplay::Flow: {
        GBuffer2 gBuffer2 = perFrameTemporalSet.GBuffer2.read(glPix(pix));
        float2 curScreenPos = make_float2(pix.x + 0.5f, pix.y + 0.5f) / imageSize;
        float2 prevScreenPos = gBuffer2.prevScreenPos;
        float2 motionVector = imageSize * (curScreenPos - prevScreenPos);
        color = make_float4(clamp(motionVectorScale * motionVector.x + motionVectorOffset, 0.0f, 1.0f),
                            clamp(motionVectorScale * motionVector.y + motionVectorOffset, 0.0f, 1.0f),
                            motionVectorOffset, 1.0f);
        break;
    }
    case BufferToDisplay::SampleCount: {
        MomentPair_SampleInfo momentPair_sampleInfo =
            staticTemporalSet.momentPair_sampleInfo_buffer.read(pix);
        float value = min(momentPair_sampleInfo.sampleInfo.count / 255.0f, 1.0f);
        color = make_float4(make_float3(value), 1.0f);
        break;
    }
    default:
        Assert_ShouldNotBeCalled();
        break;
    }

    plp.f->debugVisualizeBuffer.write(glPix(pix), color);
}
