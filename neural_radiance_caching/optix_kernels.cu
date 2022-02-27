﻿#include "neural_radiance_caching_shared.h"

using namespace shared;

struct HitPointParameter {
    float b1, b2;
    int32_t primIndex;

    CUDA_DEVICE_FUNCTION static HitPointParameter get() {
        HitPointParameter ret;
        float2 bc = optixGetTriangleBarycentrics();
        ret.b1 = bc.x;
        ret.b2 = bc.y;
        ret.primIndex = optixGetPrimitiveIndex();
        return ret;
    }
};

struct HitGroupSBTRecordData {
    GeometryInstanceData geomInstData;

    CUDA_DEVICE_FUNCTION static const HitGroupSBTRecordData &get() {
        return *reinterpret_cast<HitGroupSBTRecordData*>(optixGetSbtDataPointer());
    }
};



CUDA_DEVICE_KERNEL void RT_AH_NAME(visibility)() {
    float visibility = 0.0f;
    VisibilityRayPayloadSignature::set(&visibility);
}



CUDA_DEVICE_KERNEL void RT_RG_NAME(setupGBuffers)() {
    uint2 launchIndex = make_uint2(optixGetLaunchIndex().x, optixGetLaunchIndex().y);

    const PerspectiveCamera &camera = plp.f->camera;
    float jx = 0.5f;
    float jy = 0.5f;
    if (plp.f->enableJittering) {
        // JP: ジッターをかけると現状の実装ではUnbiased要件を満たさないかもしれない。要検討。
        // EN: Jittering may break the requirements for unbiasedness with the current implementation.
        //     Need more consideration.
        PCG32RNG rng = plp.s->rngBuffer.read(launchIndex);
        jx = rng.getFloat0cTo1o();
        jy = rng.getFloat0cTo1o();
        plp.s->rngBuffer.write(launchIndex, rng);
    }
    float x = (launchIndex.x + jx) / plp.s->imageSize.x;
    float y = (launchIndex.y + jy) / plp.s->imageSize.y;
    float vh = 2 * std::tan(camera.fovY * 0.5f);
    float vw = camera.aspect * vh;

    float3 origin = camera.position;
    float3 direction = normalize(camera.orientation * make_float3(vw * (0.5f - x), vh * (0.5f - y), 1));

    HitPointParams hitPointParams;
    hitPointParams.positionInWorld = make_float3(NAN);
    hitPointParams.prevPositionInWorld = make_float3(NAN);
    hitPointParams.normalInWorld = make_float3(NAN);
    hitPointParams.texCoord = make_float2(NAN);
    hitPointParams.materialSlot = 0xFFFFFFFF;

    PickInfo pickInfo = {};

    HitPointParams* hitPointParamsPtr = &hitPointParams;
    PickInfo* pickInfoPtr = &pickInfo;
    optixu::trace<PrimaryRayPayloadSignature>(
        plp.f->travHandle, origin, direction,
        0.0f, FLT_MAX, 0.0f, 0xFF, OPTIX_RAY_FLAG_NONE,
        RayType_Primary, NumRayTypes, RayType_Primary,
        hitPointParamsPtr, pickInfoPtr);



    float2 curRasterPos = make_float2(launchIndex.x + 0.5f, launchIndex.y + 0.5f);
    float2 prevRasterPos =
        plp.f->prevCamera.calcScreenPosition(hitPointParams.prevPositionInWorld)
        * make_float2(plp.s->imageSize.x, plp.s->imageSize.y);
    float2 motionVector = curRasterPos - prevRasterPos;
    if (plp.f->resetFlowBuffer || isnan(hitPointParams.prevPositionInWorld.x))
        motionVector = make_float2(0.0f, 0.0f);

    GBuffer0 gBuffer0;
    gBuffer0.positionInWorld = hitPointParams.positionInWorld;
    gBuffer0.texCoord_x = hitPointParams.texCoord.x;
    GBuffer1 gBuffer1;
    gBuffer1.normalInWorld = hitPointParams.normalInWorld;
    gBuffer1.texCoord_y = hitPointParams.texCoord.y;
    GBuffer2 gBuffer2;
    gBuffer2.motionVector = motionVector;
    gBuffer2.materialSlot = hitPointParams.materialSlot;

    uint32_t bufIdx = plp.f->bufferIndex;
    plp.s->GBuffer0[bufIdx].write(launchIndex, gBuffer0);
    plp.s->GBuffer1[bufIdx].write(launchIndex, gBuffer1);
    plp.s->GBuffer2[bufIdx].write(launchIndex, gBuffer2);

    if (launchIndex.x == plp.f->mousePosition.x &&
        launchIndex.y == plp.f->mousePosition.y)
        *plp.f->pickInfo = pickInfo;

    // JP: デノイザーに必要な情報を出力。
    // EN: Output information required for the denoiser.
    float3 firstHitNormal = transpose(camera.orientation) * hitPointParams.normalInWorld;
    firstHitNormal.x *= -1;
    float3 prevAlbedoResult = make_float3(0.0f, 0.0f, 0.0f);
    float3 prevNormalResult = make_float3(0.0f, 0.0f, 0.0f);
    if (plp.f->numAccumFrames > 0) {
        prevAlbedoResult = getXYZ(plp.s->albedoAccumBuffer.read(launchIndex));
        prevNormalResult = getXYZ(plp.s->normalAccumBuffer.read(launchIndex));
    }
    float curWeight = 1.0f / (1 + plp.f->numAccumFrames);
    float3 albedoResult = (1 - curWeight) * prevAlbedoResult + curWeight * hitPointParams.albedo;
    float3 normalResult = (1 - curWeight) * prevNormalResult + curWeight * firstHitNormal;
    plp.s->albedoAccumBuffer.write(launchIndex, make_float4(albedoResult, 1.0f));
    plp.s->normalAccumBuffer.write(launchIndex, make_float4(normalResult, 1.0f));
}

CUDA_DEVICE_KERNEL void RT_CH_NAME(setupGBuffers)() {
    uint2 launchIndex = make_uint2(optixGetLaunchIndex().x, optixGetLaunchIndex().y);

    auto sbtr = HitGroupSBTRecordData::get();
    const InstanceData &inst = plp.f->instanceDataBuffer[optixGetInstanceId()];
    const GeometryInstanceData &geomInst = sbtr.geomInstData;

    HitPointParams* hitPointParams;
    PickInfo* pickInfo;
    PrimaryRayPayloadSignature::get(&hitPointParams, &pickInfo);

    auto hp = HitPointParameter::get();
    float3 positionInWorld;
    float3 prevPositionInWorld;
    float3 shadingNormalInWorld;
    float3 texCoord0DirInWorld;
    //float3 geometricNormalInWorld;
    float2 texCoord;
    {
        const Triangle &tri = geomInst.triangleBuffer[hp.primIndex];
        const Vertex &v0 = geomInst.vertexBuffer[tri.index0];
        const Vertex &v1 = geomInst.vertexBuffer[tri.index1];
        const Vertex &v2 = geomInst.vertexBuffer[tri.index2];
        float b1 = hp.b1;
        float b2 = hp.b2;
        float b0 = 1 - (b1 + b2);
        float3 localP = b0 * v0.position + b1 * v1.position + b2 * v2.position;
        shadingNormalInWorld = b0 * v0.normal + b1 * v1.normal + b2 * v2.normal;
        texCoord0DirInWorld = b0 * v0.texCoord0Dir + b1 * v1.texCoord0Dir + b2 * v2.texCoord0Dir;
        //geometricNormalInWorld = cross(v1.position - v0.position, v2.position - v0.position);
        texCoord = b0 * v0.texCoord + b1 * v1.texCoord + b2 * v2.texCoord;

        positionInWorld = optixTransformPointFromObjectToWorldSpace(localP);
        prevPositionInWorld = inst.prevTransform * localP;
        shadingNormalInWorld = normalize(optixTransformNormalFromObjectToWorldSpace(shadingNormalInWorld));
        texCoord0DirInWorld = normalize(optixTransformVectorFromObjectToWorldSpace(texCoord0DirInWorld));
        //geometricNormalInWorld = normalize(optixTransformNormalFromObjectToWorldSpace(geometricNormalInWorld));
        if (!allFinite(shadingNormalInWorld)) {
            shadingNormalInWorld = make_float3(0, 0, 1);
            texCoord0DirInWorld = make_float3(1, 0, 0);
        }
    }

    const MaterialData &mat = plp.s->materialDataBuffer[geomInst.materialSlot];

    BSDF bsdf;
    bsdf.setup(mat, texCoord);
    ReferenceFrame shadingFrame(shadingNormalInWorld, texCoord0DirInWorld);
    float3 modLocalNormal = mat.readModifiedNormal(mat.normal, texCoord, mat.normalDimension);
    if (plp.f->enableBumpMapping)
        applyBumpMapping(modLocalNormal, &shadingFrame);
    float3 vOut = -optixGetWorldRayDirection();
    float3 vOutLocal = shadingFrame.toLocal(normalize(vOut));

    hitPointParams->albedo = bsdf.evaluateDHReflectanceEstimate(vOutLocal);
    hitPointParams->positionInWorld = positionInWorld;
    hitPointParams->prevPositionInWorld = prevPositionInWorld;
    hitPointParams->normalInWorld = shadingFrame.normal;
    hitPointParams->texCoord = texCoord;
    hitPointParams->materialSlot = geomInst.materialSlot;

    // JP: マウスが乗っているピクセルの情報を出力する。
    // EN: Export the information of the pixel on which the mouse is.
    if (launchIndex.x == plp.f->mousePosition.x &&
        launchIndex.y == plp.f->mousePosition.y) {
        pickInfo->hit = true;
        pickInfo->instSlot = optixGetInstanceId();
        pickInfo->geomInstSlot = geomInst.geomInstSlot;
        pickInfo->matSlot = geomInst.materialSlot;
        pickInfo->primIndex = hp.primIndex;
        pickInfo->positionInWorld = positionInWorld;
        pickInfo->normalInWorld = shadingFrame.normal;
        pickInfo->albedo = hitPointParams->albedo;
        float3 emittance = make_float3(0.0f, 0.0f, 0.0f);
        if (mat.emittance) {
            float4 texValue = tex2DLod<float4>(mat.emittance, texCoord.x, texCoord.y, 0.0f);
            emittance = make_float3(texValue);
        }
        pickInfo->emittance = emittance;
    }
}

CUDA_DEVICE_KERNEL void RT_MS_NAME(setupGBuffers)() {
    uint2 launchIndex = make_uint2(optixGetLaunchIndex().x, optixGetLaunchIndex().y);

    float3 vOut = -optixGetWorldRayDirection();
    float3 p = -vOut;

    float posPhi, posTheta;
    toPolarYUp(p, &posPhi, &posTheta);

    float phi = posPhi + plp.f->envLightRotation;

    float u = phi / (2 * Pi);
    u -= floorf(u);
    float v = posTheta / Pi;

    HitPointParams* hitPointParams;
    PickInfo* pickInfo;
    PrimaryRayPayloadSignature::get(&hitPointParams, &pickInfo);

    hitPointParams->albedo = make_float3(0.0f, 0.0f, 0.0f);
    hitPointParams->positionInWorld = p;
    hitPointParams->prevPositionInWorld = p;
    hitPointParams->normalInWorld = vOut;
    hitPointParams->texCoord = make_float2(u, v);
    hitPointParams->materialSlot = 0xFFFFFFFF;

    // JP: マウスが乗っているピクセルの情報を出力する。
    // EN: Export the information of the pixel on which the mouse is.
    if (launchIndex.x == plp.f->mousePosition.x &&
        launchIndex.y == plp.f->mousePosition.y) {
        pickInfo->hit = true;
        pickInfo->instSlot = 0xFFFFFFFF;
        pickInfo->geomInstSlot = 0xFFFFFFFF;
        pickInfo->matSlot = 0xFFFFFFFF;
        pickInfo->primIndex = 0xFFFFFFFF;
        pickInfo->positionInWorld = p;
        pickInfo->albedo = make_float3(0.0f, 0.0f, 0.0f);
        float3 emittance = make_float3(0.0f, 0.0f, 0.0f);
        if (plp.s->envLightTexture && plp.f->enableEnvLight) {
            float4 texValue = tex2DLod<float4>(plp.s->envLightTexture, u, v, 0.0f);
            emittance = make_float3(texValue);
            emittance *= Pi * plp.f->envLightPowerCoeff;
        }
        pickInfo->emittance = emittance;
        pickInfo->normalInWorld = vOut;
    }
}



CUDA_DEVICE_FUNCTION void convertToPolar(const float3 &dir, float* phi, float* theta) {
    float z = std::fmin(std::fmax(dir.z, -1.0f), 1.0f);
    *theta = std::acos(z);
    *phi = std::atan2(dir.y, dir.x);
}

CUDA_DEVICE_FUNCTION void createRadianceQuery(
    const float3 &positionInWorld, const float3 &normalInWorld, const float3 &scatteredDirInWorld,
    float roughness, const float3 &diffuseReflectance, const float3 &specularReflectance,
    RadianceQuery* query) {
    float phi, theta;
    query->position = positionInWorld;
    convertToPolar(normalInWorld, &phi, &theta);
    query->normal_phi = phi;
    query->normal_theta = theta;
    convertToPolar(scatteredDirInWorld, &phi, &theta);
    query->vOut_phi = phi;
    query->vOut_theta = theta;
    query->roughness = roughness;
    query->diffuseReflectance = diffuseReflectance;
    query->specularReflectance = specularReflectance;
}

static constexpr bool useSolidAngleSampling = false;
static constexpr bool useImplicitLightSampling = true;
static constexpr bool useExplicitLightSampling = true;
static constexpr bool useMultipleImportanceSampling = useImplicitLightSampling && useExplicitLightSampling;
static_assert(useImplicitLightSampling || useExplicitLightSampling, "Invalid configuration for light sampling.");

CUDA_DEVICE_FUNCTION float3 performNextEventEstimation(
    const float3 &shadingPoint, const float3 &vOutLocal, const ReferenceFrame &shadingFrame, const BSDF &bsdf,
    PCG32RNG &rng) {
    float3 ret = make_float3(0.0f);
    if constexpr (useExplicitLightSampling) {
        float uLight = rng.getFloat0cTo1o();
        bool selectEnvLight = false;
        float probToSampleCurLightType = 1.0f;
        if (plp.s->envLightTexture && plp.f->enableEnvLight) {
            if (uLight < probToSampleEnvLight) {
                probToSampleCurLightType = probToSampleEnvLight;
                uLight /= probToSampleCurLightType;
                selectEnvLight = true;
            }
            else {
                probToSampleCurLightType = 1.0f - probToSampleEnvLight;
                uLight = (uLight - probToSampleEnvLight) / probToSampleCurLightType;
            }
        }
        LightSample lightSample;
        float areaPDensity;
        sampleLight<useSolidAngleSampling>(
            shadingPoint,
            uLight, selectEnvLight, rng.getFloat0cTo1o(), rng.getFloat0cTo1o(),
            &lightSample, &areaPDensity);
        areaPDensity *= probToSampleCurLightType;
        float misWeight = 1.0f;
        if constexpr (useMultipleImportanceSampling) {
            float3 shadowRay = lightSample.atInfinity ?
                lightSample.position :
                (lightSample.position - shadingPoint);
            float dist2 = sqLength(shadowRay);
            shadowRay /= std::sqrt(dist2);
            float3 vInLocal = shadingFrame.toLocal(shadowRay);
            float lpCos = std::fabs(dot(shadowRay, lightSample.normal));
            float bsdfPDensity = bsdf.evaluatePDF(vOutLocal, vInLocal) * lpCos / dist2;
            if (!isfinite(bsdfPDensity))
                bsdfPDensity = 0.0f;
            float lightPDensity = areaPDensity;
            misWeight = pow2(lightPDensity) / (pow2(bsdfPDensity) + pow2(lightPDensity));
        }
        if (areaPDensity > 0.0f)
            ret = performDirectLighting<true>(
                shadingPoint, vOutLocal, shadingFrame, bsdf, lightSample) * (misWeight / areaPDensity);
    }

    return ret;
}

CUDA_DEVICE_FUNCTION void pathTrace_rayGen_generic() {
    uint2 launchIndex = make_uint2(optixGetLaunchIndex().x, optixGetLaunchIndex().y);

    uint32_t bufIdx = plp.f->bufferIndex;
    GBuffer0 gBuffer0 = plp.s->GBuffer0[bufIdx].read(launchIndex);
    GBuffer1 gBuffer1 = plp.s->GBuffer1[bufIdx].read(launchIndex);
    GBuffer2 gBuffer2 = plp.s->GBuffer2[bufIdx].read(launchIndex);

    float3 positionInWorld = gBuffer0.positionInWorld;
    float3 shadingNormalInWorld = gBuffer1.normalInWorld;
    float2 texCoord = make_float2(gBuffer0.texCoord_x, gBuffer1.texCoord_y);
    uint32_t materialSlot = gBuffer2.materialSlot;

    const PerspectiveCamera &camera = plp.f->camera;

    uint32_t linearTileIndex;
    const uint2 tileSize = *plp.s->tileSize;
    const uint32_t numPixelsInTile = tileSize.x * tileSize.y;
    {
        uint2 numTiles = (plp.s->imageSize + tileSize - 1) / tileSize;
        uint2 tileIndex = launchIndex / tileSize;
        linearTileIndex = tileIndex.y * numTiles.x + tileIndex.x;
    }

    bool useEnvLight = plp.s->envLightTexture && plp.f->enableEnvLight;
    float3 contribution = make_float3(0.001f, 0.001f, 0.001f);
    bool renderingPathEndsWithCache = false;
    uint32_t pathLength = 1;
    if (materialSlot != 0xFFFFFFFF) {
        bool isTrainingPath;
        {
            uint2 localIndex = launchIndex % tileSize;
            uint32_t localLinearIndex = localIndex.y * tileSize.x + localIndex.x;
            isTrainingPath = (localLinearIndex + *plp.s->offsetToSelectTrainingPath) % numPixelsInTile == 0;
        }

        float3 alpha = make_float3(1.0f);
        float initImportance = sRGB_calcLuminance(alpha);
        PCG32RNG rng = plp.s->rngBuffer.read(launchIndex);

        // JP: 最初の交点におけるシェーディング。
        // EN: Shading on the first hit.
        float3 vIn;
        float dirPDensity;
        float primaryPathSpread;
        float3 localThroughput;
        uint32_t trainDataIndex;
        {
            const MaterialData &mat = plp.s->materialDataBuffer[materialSlot];

            // TODO?: Use true geometric normal.
            float3 geometricNormalInWorld = shadingNormalInWorld;
            float3 vOut = camera.position - positionInWorld;
            float primaryDist2 = sqLength(vOut);
            vOut /= std::sqrt(primaryDist2);
            float primaryDotVN = dot(vOut, geometricNormalInWorld);
            float frontHit = primaryDotVN >= 0.0f ? 1.0f : -1.0f;

            primaryPathSpread = primaryDist2 / (4 * Pi * std::fabs(primaryDotVN));

            ReferenceFrame shadingFrame(shadingNormalInWorld);
            positionInWorld = offsetRayOriginNaive(positionInWorld, frontHit * geometricNormalInWorld);
            float3 vOutLocal = shadingFrame.toLocal(vOut);

            // JP: 光源を直接見ている場合の寄与を蓄積。
            // EN: Accumulate the contribution from a light source directly seeing.
            contribution = make_float3(0.0f);
            if (vOutLocal.z > 0 && mat.emittance) {
                float4 texValue = tex2DLod<float4>(mat.emittance, texCoord.x, texCoord.y, 0.0f);
                float3 emittance = make_float3(texValue);
                contribution += alpha * emittance / Pi;
            }

            BSDF bsdf;
            bsdf.setup(mat, texCoord);

            // Next event estimation (explicit light sampling) on the first hit.
            float3 directContNEE = performNextEventEstimation(
                positionInWorld, vOutLocal, shadingFrame, bsdf, rng);
            contribution += alpha * directContNEE;

            // generate a next ray.
            float3 vInLocal;
            localThroughput = bsdf.sampleThroughput(
                vOutLocal, rng.getFloat0cTo1o(), rng.getFloat0cTo1o(),
                &vInLocal, &dirPDensity);
            alpha *= localThroughput;
            vIn = shadingFrame.fromLocal(vInLocal);

            // JP: 訓練データエントリーの確保。
            // EN: 
            if (isTrainingPath) {
                trainDataIndex = atomicAdd(plp.s->numTrainingData, 1u);

                if (trainDataIndex < maxNumTrainingDataPerFrame) {
                    float roughness;
                    float3 diffuseReflectance, specularReflectance;
                    bsdf.getSurfaceParameters(
                        &diffuseReflectance, &specularReflectance, &roughness);

                    RadianceQuery radQuery;
                    createRadianceQuery(
                        positionInWorld, shadingFrame.normal, vOut,
                        roughness, diffuseReflectance, specularReflectance,
                        &radQuery);
                    plp.s->trainRadianceQueryBuffer[trainDataIndex] = radQuery;

                    TrainingVertexInfo vertInfo;
                    vertInfo.localThroughput = localThroughput;
                    vertInfo.prevVertexDataIndex = invalidVertexDataIndex;
                    plp.s->trainVertexInfoBuffer[trainDataIndex] = vertInfo;

                    // JP: 現在の頂点に対する直接照明(NEE)によるScattered Radianceでターゲット値を初期化。
                    // EN:
                    plp.s->trainTargetBuffer[trainDataIndex] = directContNEE;
                }
                else {
                    trainDataIndex = invalidVertexDataIndex;
                }
            }
        }

        // Path extension loop
        PathTraceWriteOnlyPayload woPayload = {};
        PathTraceWriteOnlyPayload* woPayloadPtr = &woPayload;
        PathTraceReadWritePayload rwPayload = {};
        PathTraceReadWritePayload* rwPayloadPtr = &rwPayload;
        rwPayload.rng = rng;
        rwPayload.initImportance = initImportance;
        rwPayload.alpha = alpha;
        rwPayload.contribution = contribution;
        rwPayload.prevDirPDensity = dirPDensity;
        rwPayload.linearTileIndex = linearTileIndex;
        rwPayload.primaryPathSpread = primaryPathSpread;
        rwPayload.curSqrtPathSpread = 0.0f;
        rwPayload.prevLocalThroughput = localThroughput;
        rwPayload.prevTrainDataIndex = trainDataIndex;
        rwPayload.renderingPathEndsWithCache = false;
        rwPayload.isTrainingPath = isTrainingPath;
        rwPayload.trainingSuffixEndsWithCache = false;
        rwPayload.pathLength = 1;
        float3 rayOrg = positionInWorld;
        float3 rayDir = vIn;
        while (true) {
            bool isValidSampling = rwPayload.prevDirPDensity > 0.0f && isfinite(rwPayload.prevDirPDensity);
            if (!isValidSampling)
                break;

            ++rwPayload.pathLength;
            if (rwPayload.pathLength >= plp.f->maxPathLength)
                rwPayload.maxLengthTerminate = true;
            rwPayload.terminate = true;
            // JP: 経路長制限に到達したときに、implicit light samplingを使わない場合はClosest-hit program内
            //     で行うことが無いので終了する。
            // EN: Nothing to do in the closest-hit program when reaching the path length limit
            //     in the case implicit light sampling is unused.
            if constexpr (!useImplicitLightSampling) {
                if (rwPayload.maxLengthTerminate)
                    break;
                // Russian roulette
                float continueProb = std::fmin(sRGB_calcLuminance(rwPayload.alpha) / rwPayload.initImportance, 1.0f);
                if (rwPayload.rng.getFloat0cTo1o() >= continueProb)
                    break;
                rwPayload.alpha /= continueProb;
            }

            constexpr RayType pathTraceRayType = RayType_PathTrace;
            optixu::trace<PathTraceRayPayloadSignature>(
                plp.f->travHandle, rayOrg, rayDir,
                0.0f, FLT_MAX, 0.0f, 0xFF, OPTIX_RAY_FLAG_NONE,
                pathTraceRayType, NumRayTypes, pathTraceRayType,
                woPayloadPtr, rwPayloadPtr);
            if (rwPayload.terminate)
                break;
            rayOrg = woPayload.nextOrigin;
            rayDir = woPayload.nextDirection;
        }
        contribution = rwPayload.contribution;

        plp.s->rngBuffer.write(launchIndex, rwPayload.rng);

        renderingPathEndsWithCache = rwPayload.renderingPathEndsWithCache;
        pathLength = rwPayload.pathLength;
        if (rwPayload.isTrainingPath && !rwPayload.trainingSuffixEndsWithCache) {
            TrainingSuffixTerminalInfo terminalInfo;
            terminalInfo.prevVertexDataIndex = rwPayload.prevTrainDataIndex;
            terminalInfo.hasQuery = false;
            plp.s->trainSuffixTerminalInfoBuffer[rwPayload.linearTileIndex] = terminalInfo;
        }
    }
    else {
        // JP: 環境光源を直接見ている場合の寄与を蓄積。
        // EN: Accumulate the contribution from the environmental light source directly seeing.
        if (useEnvLight) {
            float u = texCoord.x, v = texCoord.y;
            float4 texValue = tex2DLod<float4>(plp.s->envLightTexture, u, v, 0.0f);
            float3 luminance = plp.f->envLightPowerCoeff * make_float3(texValue);
            contribution = luminance;
        }
    }

    uint32_t linearIndex = launchIndex.y * plp.s->imageSize.x + launchIndex.x;

    //// JP: 
    //// EN: 
    //RadianceQuery radQuery;
    //radQuery.position = make_float3(0.0f, 0.0f, 0.0f);
    //radQuery.vOut = make_float3(0.0f, 0.0f, 0.0f);
    //radQuery.normal = make_float3(0.0f, 0.0f, 0.0f);
    //radQuery.diffuseReflectance = make_float3(0.0f, 0.0f, 0.0f);
    //radQuery.specularReflectance = make_float3(0.0f, 0.0f, 0.0f);
    //radQuery.roughness = 0.0f;
    //plp.s->inferenceRadianceQueryBuffer[linearIndex] = radQuery;

    // JP: 無限遠にレイが飛んだか、ロシアンルーレットによってパストレースが完了したケース。
    // EN: 
    if (!renderingPathEndsWithCache) {
        TerminalInfo terminalInfo;
        terminalInfo.alpha = make_float3(0.0f, 0.0f, 0.0f);
        terminalInfo.pathLength = pathLength;
        terminalInfo.hasQuery = false;
        plp.s->inferenceTerminalInfoBuffer[linearIndex] = terminalInfo;
    }

    plp.s->perFrameContributionBuffer[linearIndex] = contribution;
}

CUDA_DEVICE_FUNCTION void pathTrace_closestHit_generic() {
    uint2 launchIndex = make_uint2(optixGetLaunchIndex().x, optixGetLaunchIndex().y);

    auto sbtr = HitGroupSBTRecordData::get();
    const InstanceData &inst = plp.f->instanceDataBuffer[optixGetInstanceId()];
    const GeometryInstanceData &geomInst = sbtr.geomInstData;

    PathTraceWriteOnlyPayload* woPayload;
    PathTraceReadWritePayload* rwPayload;
    PathTraceRayPayloadSignature::get(&woPayload, &rwPayload);
    PCG32RNG &rng = rwPayload->rng;

    const float3 rayOrigin = optixGetWorldRayOrigin();

    auto hp = HitPointParameter::get();
    float3 positionInWorld;
    float3 shadingNormalInWorld;
    float3 texCoord0DirInWorld;
    float3 geometricNormalInWorld;
    float2 texCoord;
    float hypAreaPDensity;
    computeSurfacePoint<useMultipleImportanceSampling, useSolidAngleSampling>(
        inst, geomInst, hp.primIndex, hp.b1, hp.b2,
        rayOrigin,
        &positionInWorld, &shadingNormalInWorld, &texCoord0DirInWorld,
        &geometricNormalInWorld, &texCoord, &hypAreaPDensity);
    if constexpr (!useMultipleImportanceSampling)
        (void)hypAreaPDensity;

    const MaterialData &mat = plp.s->materialDataBuffer[geomInst.materialSlot];

    float3 vOut = normalize(-optixGetWorldRayDirection());
    float frontHit = dot(vOut, geometricNormalInWorld) >= 0.0f ? 1.0f : -1.0f;

    ReferenceFrame shadingFrame(shadingNormalInWorld, texCoord0DirInWorld);
    float3 modLocalNormal = mat.readModifiedNormal(mat.normal, texCoord, mat.normalDimension);
    if (plp.f->enableBumpMapping)
        applyBumpMapping(modLocalNormal, &shadingFrame);
    positionInWorld = offsetRayOrigin(positionInWorld, frontHit * geometricNormalInWorld);
    float3 vOutLocal = shadingFrame.toLocal(vOut);

    float dist2 = squaredDistance(rayOrigin, positionInWorld);
    rwPayload->curSqrtPathSpread += std::sqrt(dist2 / (rwPayload->prevDirPDensity * std::fabs(vOutLocal.z)));

    if constexpr (useImplicitLightSampling) {
        // Implicit Light Sampling
        if (vOutLocal.z > 0 && mat.emittance) {
            float4 texValue = tex2DLod<float4>(mat.emittance, texCoord.x, texCoord.y, 0.0f);
            float3 emittance = make_float3(texValue);
            float misWeight = 1.0f;
            if constexpr (useMultipleImportanceSampling) {
                float lightPDensity = hypAreaPDensity * dist2 / vOutLocal.z;
                float bsdfPDensity = rwPayload->prevDirPDensity;
                misWeight = pow2(bsdfPDensity) / (pow2(bsdfPDensity) + pow2(lightPDensity));
            }
            float3 directContImplicit = emittance * (misWeight / Pi);
            rwPayload->contribution += rwPayload->alpha * directContImplicit;

            // JP: 1つ前の頂点に対する直接照明(Implicit)によるScattered Radianceをターゲット値に加算。
            // EN: 
            if (rwPayload->isTrainingPath && rwPayload->prevTrainDataIndex != invalidVertexDataIndex) {
                plp.s->trainTargetBuffer[rwPayload->prevTrainDataIndex] +=
                    rwPayload->prevLocalThroughput * directContImplicit;
            }
        }

        // Russian roulette
        float continueProb = std::fmin(sRGB_calcLuminance(rwPayload->alpha) / rwPayload->initImportance, 1.0f);
        if (rng.getFloat0cTo1o() >= continueProb || rwPayload->maxLengthTerminate)
            return;
        rwPayload->alpha /= continueProb;
    }

    BSDF bsdf;
    bsdf.setup(mat, texCoord);

    // Path termination into the neural radiance cache.
    bool pathIsSpreadEnough =
        pow2(rwPayload->curSqrtPathSpread) > pathTerminationFactor * rwPayload->primaryPathSpread;
    if (pathIsSpreadEnough) {
        uint32_t linearIndex = launchIndex.y * plp.s->imageSize.x + launchIndex.x;

        float roughness;
        float3 diffuseReflectance, specularReflectance;
        bsdf.getSurfaceParameters(
            &diffuseReflectance, &specularReflectance, &roughness);

        // JP: Radianceクエリーのための情報を記録する。
        // EN: 
        RadianceQuery radQuery;
        createRadianceQuery(
            positionInWorld, shadingFrame.normal, vOut,
            roughness, diffuseReflectance, specularReflectance,
            &radQuery);

        if (!rwPayload->renderingPathEndsWithCache) {
            plp.s->inferenceRadianceQueryBuffer[linearIndex] = radQuery;

            TerminalInfo terminalInfo;
            terminalInfo.alpha = rwPayload->alpha;
            terminalInfo.pathLength = rwPayload->pathLength;
            terminalInfo.hasQuery = true;
            plp.s->inferenceTerminalInfoBuffer[linearIndex] = terminalInfo;

            rwPayload->renderingPathEndsWithCache = true;
            if (rwPayload->isTrainingPath)
                rwPayload->curSqrtPathSpread = 0;
            else
                return;
        }
        else {
            // JP: 訓練データを既に十分生成している場合は既に強制的にTraining Suffixは終了したことになっている。
            if (!rwPayload->trainingSuffixEndsWithCache) {
                // JP: 
                // EN: 
                uint32_t offset = plp.s->imageSize.x * plp.s->imageSize.y;
                plp.s->inferenceRadianceQueryBuffer[offset + rwPayload->linearTileIndex] = radQuery;

                TrainingSuffixTerminalInfo terminalInfo;
                terminalInfo.prevVertexDataIndex = rwPayload->prevTrainDataIndex;
                terminalInfo.hasQuery = true;
                plp.s->trainSuffixTerminalInfoBuffer[rwPayload->linearTileIndex] = terminalInfo;

                rwPayload->trainingSuffixEndsWithCache = true;
            }
            return;
        }
    }

    // Next Event Estimation (Explicit Light Sampling)
    float3 directContNEE = performNextEventEstimation(
        positionInWorld, vOutLocal, shadingFrame, bsdf, rng);
    rwPayload->contribution += rwPayload->alpha * directContNEE;

    // generate a next ray.
    float3 vInLocal;
    float dirPDensity;
    float3 localThroughput = bsdf.sampleThroughput(
        vOutLocal, rng.getFloat0cTo1o(), rng.getFloat0cTo1o(),
        &vInLocal, &dirPDensity);
    rwPayload->alpha *= localThroughput;
    float3 vIn = shadingFrame.fromLocal(vInLocal);

    woPayload->nextOrigin = positionInWorld;
    woPayload->nextDirection = vIn;
    rwPayload->prevDirPDensity = dirPDensity;
    rwPayload->prevLocalThroughput = localThroughput;
    rwPayload->terminate = false;

    // JP: 訓練データエントリーの確保。
    // EN: 
    if (rwPayload->isTrainingPath && !rwPayload->trainingSuffixEndsWithCache) {
        uint32_t trainDataIndex = atomicAdd(plp.s->numTrainingData, 1u);

        float roughness;
        float3 diffuseReflectance, specularReflectance;
        bsdf.getSurfaceParameters(
            &diffuseReflectance, &specularReflectance, &roughness);

        RadianceQuery radQuery;
        createRadianceQuery(
            positionInWorld, shadingFrame.normal, vOut,
            roughness, diffuseReflectance, specularReflectance,
            &radQuery);

        if (trainDataIndex < maxNumTrainingDataPerFrame) {
            plp.s->trainRadianceQueryBuffer[trainDataIndex] = radQuery;

            TrainingVertexInfo vertInfo;
            vertInfo.localThroughput = localThroughput;
            vertInfo.prevVertexDataIndex = rwPayload->prevTrainDataIndex;
            plp.s->trainVertexInfoBuffer[trainDataIndex] = vertInfo;

            // JP: 現在の頂点に対する直接照明(NEE)によるScattered Radianceでターゲット値を初期化。
            // EN:
            plp.s->trainTargetBuffer[trainDataIndex] = directContNEE;

            rwPayload->prevTrainDataIndex = trainDataIndex;
        }
        else {
            // JP: 
            // EN: 
            uint32_t offset = plp.s->imageSize.x * plp.s->imageSize.y;
            plp.s->inferenceRadianceQueryBuffer[offset + rwPayload->linearTileIndex] = radQuery;

            TrainingSuffixTerminalInfo terminalInfo;
            terminalInfo.prevVertexDataIndex = rwPayload->prevTrainDataIndex;
            terminalInfo.hasQuery = true;
            plp.s->trainSuffixTerminalInfoBuffer[rwPayload->linearTileIndex] = terminalInfo;

            rwPayload->trainingSuffixEndsWithCache = true;
        }
    }
}

CUDA_DEVICE_KERNEL void RT_RG_NAME(pathTrace)() {
    pathTrace_rayGen_generic();
}

CUDA_DEVICE_KERNEL void RT_CH_NAME(pathTrace)() {
    pathTrace_closestHit_generic();
}

CUDA_DEVICE_KERNEL void RT_MS_NAME(pathTrace)() {
    if constexpr (useImplicitLightSampling) {
        if (!plp.s->envLightTexture || !plp.f->enableEnvLight)
            return;

        PathTraceReadWritePayload* rwPayload;
        PathTraceRayPayloadSignature::get(nullptr, &rwPayload);

        float3 rayDir = normalize(optixGetWorldRayDirection());
        float posPhi, theta;
        toPolarYUp(rayDir, &posPhi, &theta);

        float phi = posPhi + plp.f->envLightRotation;
        phi = phi - floorf(phi / (2 * Pi)) * 2 * Pi;
        float2 texCoord = make_float2(phi / (2 * Pi), theta / Pi);

        // Implicit Light Sampling
        float4 texValue = tex2DLod<float4>(plp.s->envLightTexture, texCoord.x, texCoord.y, 0.0f);
        float3 luminance = plp.f->envLightPowerCoeff * make_float3(texValue);
        float misWeight = 1.0f;
        if constexpr (useMultipleImportanceSampling) {
            float uvPDF = plp.s->envLightImportanceMap.evaluatePDF(texCoord.x, texCoord.y);
            float hypAreaPDensity = uvPDF / (2 * Pi * Pi * std::sin(theta));
            float lightPDensity = probToSampleEnvLight * hypAreaPDensity;
            float bsdfPDensity = rwPayload->prevDirPDensity;
            misWeight = pow2(bsdfPDensity) / (pow2(bsdfPDensity) + pow2(lightPDensity));
        }
        float3 directContImplicit = misWeight * luminance;
        rwPayload->contribution += rwPayload->alpha * directContImplicit;

        // JP: 1つ前の頂点に対する直接照明(Implicit)によるScattered Radianceをターゲット値に加算。
        // EN: 
        if (rwPayload->isTrainingPath) {
            plp.s->trainTargetBuffer[rwPayload->prevTrainDataIndex] +=
                rwPayload->prevLocalThroughput * directContImplicit;
        }
    }
}
