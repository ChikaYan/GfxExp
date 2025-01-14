﻿/*

コマンドラインオプション例 / Command line option example:
You can load a 3D model for example by downloading from the internet.
(e.g. https://casual-effects.com/data/)

(1) -cam-pos 7.534, -0.067, -1.612 -cam-yaw -61
    -name scene -obj ZeroDay_v1/MEASURE_SEVEN/MEASURE_SEVEN_COLORED_LIGHTS.fbx 1.0 simple_pbr -brightness 2.0
    -inst scene

    * Zero-Day from Open Research Content Archive (ORCA)
      https://developer.nvidia.com/orca/beeple-zero-day

(2) -cam-pos -9.5 5 0 -cam-yaw 90
    -name sponza -obj crytek_sponza/sponza.obj 0.01 trad
    -name rectlight -emittance 600 600 600 -rectangle 1 1 -begin-pos 0 15 0 -inst rectlight
    -name rectlight0 -emittance 600 0 0 -rectangle 1 1
    -name rectlight1 -emittance 0 600 0 -rectangle 1 1
    -name rectlight2 -emittance 0 0 600 -rectangle 1 1
    -name rectlight3 -emittance 100 100 100 -rectangle 1 1
    -begin-pos 0 0 0.36125 -inst sponza
    -begin-pos -5 13.1 0 -end-pos 5 13.1 0 -freq 5 -time 0.0 -inst rectlight0
    -begin-pos -5 13 0 -end-pos 5 13 0 -freq 10 -time 2.5 -inst rectlight1
    -begin-pos -5 12.9 0 -end-pos 5 12.9 0 -freq 15 -time 7.5 -inst rectlight2
    -begin-pos -5 7 -4.8 -begin-pitch -30 -end-pos 5 7 -4.8 -end-pitch -30 -freq 5 -inst rectlight3
    -begin-pos 5 7 4.8 -begin-pitch 30 -end-pos -5 7 4.8 -end-pitch 30 -freq 5 -inst rectlight3

JP: このプログラムはNeural Radiance Caching (NRC) [1]の実装例です。
    NRCは位置や出射方向、物体表面のパラメターを入力、輝度を出力とするニューラルネットワークです。
    レンダリング時にはパストレーシングによって経路を構築しますが、
    ある経路長より先から得られる寄与をキャッシュからのクエリーによって置き換えることで、
    少しのバイアスと引き換えに低い分散の推定値を得ることができます。
    さらに経路長が短くなることでシーンによっては1フレームの時間も短くなり得ます。
    NRCは比較的小さなネットワークであり、トレーニングはレンダリングの最中に行うオンラインラーニングとすることで、
    「適応による汎化」を実現、推論の実行時間もリアルタイムレンダリングに適した短いものとなります。
    ニューラルネットワーク部分にはtiny-cuda-nn [2]というライブラリーを使用しています。
    ※TuringアーキテクチャーのGPUでも動くと思いますが、現状RTX 3080 (Ampere)でしか動作確認していません。
      CMakeを使わずにこのサンプルをビルドするには外部ライブラリを先に手動でビルドしておく必要があります。
      tiny-cuda-nnのビルドの調整やnetwork_interface.cuのTCNN_MIN_GPU_ARCHの変更などが必要だと思います。
    ※デフォルトではBRDFにOptiXのCallable ProgramやCUDAの関数ポインターを使用した汎用的な実装になっており、
      性能上のオーバーヘッドが著しいため、純粋な性能を見る上では common_shared.h の USE_HARD_CODED_BSDF_FUNCTIONS
      を有効化したほうがよいかもしれません。

EN: This program is an example implementation of Neural Radiance Caching (NRC) [1].
    NRC is a neural network where the inputs are a position and an outgoing direction, surface parameters,
    and the output is radiance. It constructs paths based on path tracing when rendering,
    but replaces contributions given from beyond a certain path length by a query to the cache.
    This achieves low variance estimates at the cost of a little bias.
    Additionally, one frame time can even be reduced depending on the scene thanks to path shortening.
    NRC is a relatively small network, and training is online learning during rendering.
    This achieve "generalization via adaptation", and short inference time appropriate to real-time rendering.
    This program uses tiny-cuda-nn [2] for the neural network part.
    * I have tested only with RTX 3080 (Ampere) while I think the program would work with
      Turing architecture GPU as well.
      It requires to manually build external libaries before building the program when not using CMake.
      Some adjustments for building tiny-cuda-nn and changing TCNN_MIN_GPU_ARCH in network_interface.cu
      are required.
    * The program is generic implementation with OptiX's callable program and CUDA's function pointer,
      and has significant performance overhead, therefore it may be recommended to enable USE_HARD_CODED_BSDF_FUNCTIONS
      in common_shared.h to see pure performance.

[1] Real-time Neural Radiance Caching for Path Tracing
    https://research.nvidia.com/publication/2021-06_Real-time-Neural-Radiance
[2] Tiny CUDA Neural Networks
    https://github.com/NVlabs/tiny-cuda-nn

*/

#include "neural_radiance_caching_shared.h"
#include "../common/common_host.h"
#include "network_interface.h"

// Include glfw3.h after our OpenGL definitions
#include "../utils/gl_util.h"
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <cassert>
#include <random>

#include "json.hpp"


enum class GBufferEntryPoint {
    setupGBuffers = 0,
};
enum class PathTracingEntryPoint {
    Baseline,
    NRC,
    visualizePrediction,
//     performInitialRIS,
//     performInitialAndTemporalRISBiased,
//     performInitialAndTemporalRISUnbiased,
//     performSpatialRISBiased,
//     performSpatialRISUnbiased,
};
// enum class ReSTIREntryPoint {
//     performInitialRIS,
//     performInitialAndTemporalRISBiased,
//     performInitialAndTemporalRISUnbiased,
//     performSpatialRISBiased,
//     performSpatialRISUnbiased,
//     shading,
// };

// enum class ReSTIRRenderer {
//     OriginalReSTIRBiased = 0,
//     OriginalReSTIRUnbiased,
//     RearchitectedReSTIRBiased,
//     RearchitectedReSTIRUnbiased,
// };

struct GPUEnvironment {
    CUcontext cuContext;
    optixu::Context optixContext;

    CUmodule cudaModule;
    cudau::Kernel kernelPreprocessNRC;
    cudau::Kernel kernelAccumulateInferredRadianceValues;
    cudau::Kernel kernelPropagateRadianceValues;
    cudau::Kernel kernelDecomposeTrainingData;
    cudau::Kernel kernelShuffleTrainingData;
    cudau::Kernel kernelWarpDyCoords;
    cudau::Kernel kernelPerturbCoords;
    CUdeviceptr plpPtr;

    template <typename EntryPointType>
    struct Pipeline {
        optixu::Pipeline optixPipeline;
        optixu::Module optixModule;
        std::unordered_map<EntryPointType, optixu::ProgramGroup> entryPoints;
        std::unordered_map<std::string, optixu::ProgramGroup> programs;
        std::vector<optixu::ProgramGroup> callablePrograms;
        cudau::Buffer sbt;
        cudau::Buffer hitGroupSbt;

        void setEntryPoint(EntryPointType et) {
            optixPipeline.setRayGenerationProgram(entryPoints.at(et));
        }
    };

    Pipeline<GBufferEntryPoint> gBuffer;
    // Pipeline<ReSTIREntryPoint> restir;
    Pipeline<PathTracingEntryPoint> pathTracing;

    optixu::Material optixDefaultMaterial;

    void initialize() {
        int32_t cuDeviceCount;
        CUDADRV_CHECK(cuInit(0));
        CUDADRV_CHECK(cuDeviceGetCount(&cuDeviceCount));
        CUDADRV_CHECK(cuCtxCreate(&cuContext, 0, 0));
        CUDADRV_CHECK(cuCtxSetCurrent(cuContext));

        optixContext = optixu::Context::create(cuContext/*, 4, DEBUG_SELECT(true, false)*/);

        CUDADRV_CHECK(cuModuleLoad(
            &cudaModule,
            (getExecutableDirectory() / "neural_radiance_caching/ptxes/nrc_setup_kernels.ptx").string().c_str()));
        kernelPreprocessNRC =
            cudau::Kernel(cudaModule, "preprocessNRC", cudau::dim3(32), 0);
        kernelAccumulateInferredRadianceValues =
            cudau::Kernel(cudaModule, "accumulateInferredRadianceValues", cudau::dim3(32), 0);
        kernelPropagateRadianceValues =
            cudau::Kernel(cudaModule, "propagateRadianceValues", cudau::dim3(32), 0);
        kernelDecomposeTrainingData =
            cudau::Kernel(cudaModule, "decomposeTrainingData", cudau::dim3(32), 0);
        kernelShuffleTrainingData =
            cudau::Kernel(cudaModule, "shuffleTrainingData", cudau::dim3(32), 0);
        kernelWarpDyCoords =
            cudau::Kernel(cudaModule, "warpDyCoords", cudau::dim3(32), 0);
        kernelPerturbCoords =
            cudau::Kernel(cudaModule, "perturbCoords", cudau::dim3(32), 0);

        size_t plpSize;
        CUDADRV_CHECK(cuModuleGetGlobal(&plpPtr, &plpSize, cudaModule, "plp"));
        Assert(sizeof(shared::PipelineLaunchParameters) == plpSize, "Unexpected plp size.");

        optixDefaultMaterial = optixContext.createMaterial();
        optixu::Module emptyModule;

        {
            Pipeline<GBufferEntryPoint> &pipeline = gBuffer;
            optixu::Pipeline &p = pipeline.optixPipeline;
            optixu::Module &m = pipeline.optixModule;
            p = optixContext.createPipeline();

            p.setPipelineOptions(
                std::max({
                    shared::PrimaryRayPayloadSignature::numDwords
                         }),
                optixu::calcSumDwords<float2>(),
                "plp", sizeof(shared::PipelineLaunchParameters),
                false, OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_LEVEL_INSTANCING,
                OPTIX_EXCEPTION_FLAG_STACK_OVERFLOW | OPTIX_EXCEPTION_FLAG_TRACE_DEPTH |
                DEBUG_SELECT(OPTIX_EXCEPTION_FLAG_DEBUG, OPTIX_EXCEPTION_FLAG_NONE),
                OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE);

            m = p.createModuleFromPTXString(
                readTxtFile(getExecutableDirectory() / "neural_radiance_caching/ptxes/optix_gbuffer_kernels.ptx"),
                OPTIX_COMPILE_DEFAULT_MAX_REGISTER_COUNT,
                DEBUG_SELECT(OPTIX_COMPILE_OPTIMIZATION_LEVEL_0, OPTIX_COMPILE_OPTIMIZATION_DEFAULT),
                DEBUG_SELECT(OPTIX_COMPILE_DEBUG_LEVEL_FULL, OPTIX_COMPILE_DEBUG_LEVEL_NONE));

            pipeline.entryPoints[GBufferEntryPoint::setupGBuffers] = p.createRayGenProgram(
                m, RT_RG_NAME_STR("setupGBuffers"));

            pipeline.programs["hitgroup"] = p.createHitProgramGroupForTriangleIS(
                m, RT_CH_NAME_STR("setupGBuffers"),
                emptyModule, nullptr);
            pipeline.programs["miss"] = p.createMissProgram(
                m, RT_MS_NAME_STR("setupGBuffers"));

            pipeline.programs["emptyHitGroup"] = p.createEmptyHitProgramGroup();

            pipeline.setEntryPoint(GBufferEntryPoint::setupGBuffers);
            p.setNumMissRayTypes(shared::GBufferRayType::NumTypes);
            p.setMissProgram(shared::GBufferRayType::Primary, pipeline.programs.at("miss"));

            p.setNumCallablePrograms(NumCallablePrograms);
            pipeline.callablePrograms.resize(NumCallablePrograms);
            for (int i = 0; i < NumCallablePrograms; ++i) {
                optixu::ProgramGroup program = p.createCallableProgramGroup(
                    m, callableProgramEntryPoints[i],
                    emptyModule, nullptr);
                pipeline.callablePrograms[i] = program;
                p.setCallableProgram(i, program);
            }
            std::cout << "linking gbuffer\n";
            p.link(1, DEBUG_SELECT(OPTIX_COMPILE_DEBUG_LEVEL_FULL, OPTIX_COMPILE_DEBUG_LEVEL_NONE));

            optixDefaultMaterial.setHitGroup(shared::GBufferRayType::Primary, pipeline.programs.at("hitgroup"));
            for (uint32_t rayType = shared::GBufferRayType::NumTypes; rayType < shared::maxNumRayTypes; ++rayType)
                optixDefaultMaterial.setHitGroup(rayType, pipeline.programs.at("emptyHitGroup"));

            size_t sbtSize;
            p.generateShaderBindingTableLayout(&sbtSize);
            pipeline.sbt.initialize(cuContext, Scene::bufferType, sbtSize, 1);
            pipeline.sbt.setMappedMemoryPersistent(true);
            p.setShaderBindingTable(pipeline.sbt, pipeline.sbt.getMappedPointer());
        }

        {
            Pipeline<PathTracingEntryPoint> &pipeline = pathTracing;
            optixu::Pipeline &p = pipeline.optixPipeline;
            optixu::Module &m = pipeline.optixModule;
            p = optixContext.createPipeline();

            p.setPipelineOptions(
                std::max({
                    shared::PathTraceRayPayloadSignature<false>::numDwords,
                    shared::PathTraceRayPayloadSignature<true>::numDwords,
                    shared::VisibilityRayPayloadSignature::numDwords
                         }),
                optixu::calcSumDwords<float2>(),
                "plp", sizeof(shared::PipelineLaunchParameters),
                false, OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_LEVEL_INSTANCING,
                OPTIX_EXCEPTION_FLAG_STACK_OVERFLOW | OPTIX_EXCEPTION_FLAG_TRACE_DEPTH |
                DEBUG_SELECT(OPTIX_EXCEPTION_FLAG_DEBUG, OPTIX_EXCEPTION_FLAG_NONE),
                OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE);

            m = p.createModuleFromPTXString(
                readTxtFile(getExecutableDirectory() / "neural_radiance_caching/ptxes/optix_pathtracing_kernels.ptx"),
                OPTIX_COMPILE_DEFAULT_MAX_REGISTER_COUNT,
                DEBUG_SELECT(OPTIX_COMPILE_OPTIMIZATION_LEVEL_0, OPTIX_COMPILE_OPTIMIZATION_DEFAULT),
                DEBUG_SELECT(OPTIX_COMPILE_DEBUG_LEVEL_FULL, OPTIX_COMPILE_DEBUG_LEVEL_NONE));

            pipeline.entryPoints[PathTracingEntryPoint::Baseline] =
                p.createRayGenProgram(m, RT_RG_NAME_STR("pathTraceBaseline"));
            pipeline.entryPoints[PathTracingEntryPoint::NRC] =
                p.createRayGenProgram(m, RT_RG_NAME_STR("pathTraceNRC"));
            pipeline.entryPoints[PathTracingEntryPoint::visualizePrediction] =
                p.createRayGenProgram(m, RT_RG_NAME_STR("visualizePrediction"));
            // pipeline.entryPoints[PathTracingEntryPoint::performInitialRIS] =
            //     p.createRayGenProgram(m, RT_RG_NAME_STR("performInitialRIS"));
            // pipeline.entryPoints[PathTracingEntryPoint::performInitialAndTemporalRISBiased] =
            //     p.createRayGenProgram(m, RT_RG_NAME_STR("performInitialAndTemporalRISBiased"));
            // pipeline.entryPoints[PathTracingEntryPoint::performInitialAndTemporalRISUnbiased] =
            //     p.createRayGenProgram(m, RT_RG_NAME_STR("performInitialAndTemporalRISUnbiased"));
            // pipeline.entryPoints[PathTracingEntryPoint::performSpatialRISBiased] =
            //     p.createRayGenProgram(m, RT_RG_NAME_STR("performSpatialRISBiased"));
            // pipeline.entryPoints[PathTracingEntryPoint::performSpatialRISUnbiased] =
            //     p.createRayGenProgram(m, RT_RG_NAME_STR("performSpatialRISUnbiased"));

            pipeline.programs[RT_MS_NAME_STR("pathTraceBaseline")] = p.createMissProgram(
                m, RT_MS_NAME_STR("pathTraceBaseline"));
            pipeline.programs[RT_CH_NAME_STR("pathTraceBaseline")] = p.createHitProgramGroupForTriangleIS(
                m, RT_CH_NAME_STR("pathTraceBaseline"),
                emptyModule, nullptr);

            pipeline.programs[RT_MS_NAME_STR("pathTraceNRC")] = p.createMissProgram(
                m, RT_MS_NAME_STR("pathTraceNRC"));
            pipeline.programs[RT_CH_NAME_STR("pathTraceNRC")] = p.createHitProgramGroupForTriangleIS(
                m, RT_CH_NAME_STR("pathTraceNRC"),
                emptyModule, nullptr);

            pipeline.programs[RT_AH_NAME_STR("visibility")] = p.createHitProgramGroupForTriangleIS(
                emptyModule, nullptr,
                m, RT_AH_NAME_STR("visibility"));

            pipeline.programs["emptyMiss"] = p.createMissProgram(emptyModule, nullptr);

            p.setNumMissRayTypes(shared::PathTracingRayType::NumTypes);
            p.setMissProgram(
                shared::PathTracingRayType::Baseline, pipeline.programs.at(RT_MS_NAME_STR("pathTraceBaseline")));
            p.setMissProgram(
                shared::PathTracingRayType::NRC, pipeline.programs.at(RT_MS_NAME_STR("pathTraceNRC")));
            p.setMissProgram(shared::PathTracingRayType::Visibility, pipeline.programs.at("emptyMiss"));

            p.setNumCallablePrograms(NumCallablePrograms);
            pipeline.callablePrograms.resize(NumCallablePrograms);
            for (int i = 0; i < NumCallablePrograms; ++i) {
                optixu::ProgramGroup program = p.createCallableProgramGroup(
                    m, callableProgramEntryPoints[i],
                    emptyModule, nullptr);
                pipeline.callablePrograms[i] = program;
                p.setCallableProgram(i, program);
            }

            std::cout << "linking nrc\n";
            p.link(2, DEBUG_SELECT(OPTIX_COMPILE_DEBUG_LEVEL_FULL, OPTIX_COMPILE_DEBUG_LEVEL_NONE));

            optixDefaultMaterial.setHitGroup(
                shared::PathTracingRayType::Baseline, pipeline.programs.at(RT_CH_NAME_STR("pathTraceBaseline")));
            optixDefaultMaterial.setHitGroup(
                shared::PathTracingRayType::NRC, pipeline.programs.at(RT_CH_NAME_STR("pathTraceNRC")));
            optixDefaultMaterial.setHitGroup(
                shared::PathTracingRayType::Visibility, pipeline.programs.at(RT_AH_NAME_STR("visibility")));

            size_t sbtSize;
            p.generateShaderBindingTableLayout(&sbtSize);
            pipeline.sbt.initialize(cuContext, Scene::bufferType, sbtSize, 1);
            pipeline.sbt.setMappedMemoryPersistent(true);
            p.setShaderBindingTable(pipeline.sbt, pipeline.sbt.getMappedPointer());
        }

        std::vector<void*> callablePointers(NumCallablePrograms);
        for (int i = 0; i < NumCallablePrograms; ++i) {
            CUdeviceptr symbolPtr;
            size_t symbolSize;
            CUDADRV_CHECK(cuModuleGetGlobal(&symbolPtr, &symbolSize, cudaModule,
                                            callableProgramPointerNames[i]));
            void* funcPtrOnDevice;
            Assert(symbolSize == sizeof(funcPtrOnDevice), "Unexpected symbol size");
            CUDADRV_CHECK(cuMemcpyDtoH(&funcPtrOnDevice, symbolPtr, sizeof(funcPtrOnDevice)));
            callablePointers[i] = funcPtrOnDevice;
        }

        CUdeviceptr callableToPointerMapPtr;
        size_t callableToPointerMapSize;
        CUDADRV_CHECK(cuModuleGetGlobal(
            &callableToPointerMapPtr, &callableToPointerMapSize, cudaModule,
            "c_callableToPointerMap"));
        CUDADRV_CHECK(cuMemcpyHtoD(callableToPointerMapPtr, callablePointers.data(),
                                   callableToPointerMapSize));
    }

    void finalize() {
        {
            Pipeline<PathTracingEntryPoint> &pipeline = pathTracing;
            pipeline.hitGroupSbt.finalize();
            pipeline.sbt.finalize();
            for (int i = 0; i < NumCallablePrograms; ++i)
                pipeline.callablePrograms[i].destroy();
            for (auto &pair : pipeline.programs)
                pair.second.destroy();
            for (auto &pair : pipeline.entryPoints)
                pair.second.destroy();
            pipeline.optixModule.destroy();
            pipeline.optixPipeline.destroy();
        }

        {
            Pipeline<GBufferEntryPoint> &pipeline = gBuffer;
            pipeline.hitGroupSbt.finalize();
            pipeline.sbt.finalize();
            for (int i = 0; i < NumCallablePrograms; ++i)
                pipeline.callablePrograms[i].destroy();
            for (auto &pair : pipeline.programs)
                pair.second.destroy();
            for (auto &pair : pipeline.entryPoints)
                pair.second.destroy();
            pipeline.optixModule.destroy();
            pipeline.optixPipeline.destroy();
        }

        optixDefaultMaterial.destroy();

        CUDADRV_CHECK(cuModuleUnload(cudaModule));

        optixContext.destroy();

        //CUDADRV_CHECK(cuCtxDestroy(cuContext));
    }
};

class CsvLogger{
    public:
        std::ofstream logFile;
        std::string logPath;

    CsvLogger(std::string logPath){
        this->logPath = logPath;
        std::cout << "Logging at: " << logPath << std::endl;
        logFile.open(logPath);
        // write header
        logFile << "id, loss, time/all, time/restir, time/path_tracing, time/nrc_infer, time/nrc_train" << "\n";
        // logFile.close();
    }

    void log(
        int id,
        float loss,
        float time_all,
        float time_restir,
        float time_path_tracing,
        float time_nrc_infer,
        float time_nrc_train
    ){
        // logFile.open(logPath, std::ios_base::app);
        logFile << id << ",";
        logFile << loss << ","; 
        logFile << time_all << ","; 
        logFile << time_restir << ","; 
        logFile << time_path_tracing << ","; 
        logFile << time_nrc_infer << ","; 
        logFile << time_nrc_train;
        logFile << "\n";
        // logFile.close();
    }

    ~CsvLogger(){
        logFile.close();
    }

};

nlohmann::json logQueryToJson(shared::RadianceQuery query){
    
    nlohmann::json jsonQuery;

    jsonQuery["position"] = {
        query.position.x, 
        query.position.y, 
        query.position.z
        }; 

    jsonQuery["motion"] = {
        query.motion.x, 
        query.motion.y, 
        query.motion.z
        }; 

    jsonQuery["diffuse_reflectance"] = {
        query.diffuseReflectance.x, 
        query.diffuseReflectance.y, 
        query.diffuseReflectance.z
        }; 

    jsonQuery["specular_reflectance"] = {
        query.specularReflectance.x, 
        query.specularReflectance.y, 
        query.specularReflectance.z
        }; 

    jsonQuery["normal_phi"] = 
        query.normal_phi; 

    jsonQuery["normal_theta"] = 
        query.normal_theta; 

    jsonQuery["vOut_phi"] = 
        query.vOut_phi; 

    jsonQuery["vOut_theta"] = 
        query.vOut_theta; 

    jsonQuery["roughness"] = 
        query.roughness;     

    return jsonQuery;
}


struct KeyState {
    uint64_t timesLastChanged[5];
    bool statesLastChanged[5];
    uint32_t lastIndex;

    KeyState() : lastIndex(0) {
        for (int i = 0; i < 5; ++i) {
            timesLastChanged[i] = 0;
            statesLastChanged[i] = false;
        }
    }

    void recordStateChange(bool state, uint64_t time) {
        bool lastState = statesLastChanged[lastIndex];
        if (state == lastState)
            return;

        lastIndex = (lastIndex + 1) % 5;
        statesLastChanged[lastIndex] = !lastState;
        timesLastChanged[lastIndex] = time;
    }

    bool getState(int32_t goBack = 0) const {
        Assert(goBack >= -4 && goBack <= 0, "goBack must be in the range [-4, 0].");
        return statesLastChanged[(lastIndex + goBack + 5) % 5];
    }

    uint64_t getTime(int32_t goBack = 0) const {
        Assert(goBack >= -4 && goBack <= 0, "goBack must be in the range [-4, 0].");
        return timesLastChanged[(lastIndex + goBack + 5) % 5];
    }
};

static KeyState g_keyForward;
static KeyState g_keyBackward;
static KeyState g_keyLeftward;
static KeyState g_keyRightward;
static KeyState g_keyUpward;
static KeyState g_keyDownward;
static KeyState g_keyTiltLeft;
static KeyState g_keyTiltRight;
static KeyState g_keyFasterPosMovSpeed;
static KeyState g_keySlowerPosMovSpeed;
static KeyState g_buttonRotate;
static double g_mouseX;
static double g_mouseY;

static float g_initBrightness = 0.0f;
static float g_cameraPositionalMovingSpeed = 0.0f;
static float g_cameraDirectionalMovingSpeed;
static float g_cameraTiltSpeed;
static Quaternion g_cameraOrientation;
static Quaternion g_tempCameraOrientation;
static float3 g_cameraPosition;
static std::filesystem::path g_envLightTexturePath;

static PositionEncoding g_positionEncoding = PositionEncoding::HashGrid;
static uint32_t g_numHiddenLayers = 8;
static float g_learningRate = 1e-2f;
static std::string exp_name = "test";
static bool useNRC = true;
static long rngSeed = 591842031321323413;
static int32_t maxPathLength = 5;
static int32_t SPP = 1;
static shared::BufferToDisplay bufferTypeToDisplay = shared::BufferToDisplay::NoisyBeauty;
static bool nrcOnlyRaw = false; // show raw output of network without factorization
static bool nrcOnlyEmissive = false; // also add emmisive term to nrc only result

static NeuralRadianceCacheConfig NRCConfig;

static uint64_t frameStop = 50;
static uint64_t saveImgEvery = 1;

static bool g_takeScreenShot = false;

static bool useSeparateNRC = false;

static float log10RadianceScale = 1.f;

static bool warpDyCoord = false;

static bool perturbSmooth = false;

static bool savePyCache = false;

static int camMoveX = 0;
static int camMoveY = 0;
static int camMoveZ = 0;

static float perturbSmoothRange = 0.1f;

static int perturbSmoothTimes = 1;

static int perturbSmoothAfter = -1;

// static ReSTIRRenderer curRenderer = ReSTIRRenderer::OriginalReSTIRBiased;

struct MeshGeometryInfo {
    std::filesystem::path path;
    float preScale;
    MaterialConvention matConv;
};

struct RectangleGeometryInfo {
    float dimX;
    float dimZ;
    float3 emittance;
    std::filesystem::path emitterTexPath;
};

struct MeshInstanceInfo {
    std::string name;
    float3 beginPosition;
    float3 endPosition;
    float beginScale;
    float endScale;
    Quaternion beginOrientation;
    Quaternion endOrientation;
    float frequency;
    float initTime;
};

using MeshInfo = std::variant<MeshGeometryInfo, RectangleGeometryInfo>;
static std::map<std::string, MeshInfo> g_meshInfos;
static std::vector<MeshInstanceInfo> g_meshInstInfos;

static void parseCommandline(int32_t argc, const char* argv[]) {
    std::string name;

    Quaternion camOrientation = Quaternion();

    float3 beginPosition = float3(0.0f, 0.0f, 0.0f);
    float3 endPosition = float3(NAN, NAN, NAN);
    Quaternion beginOrientation = Quaternion();
    Quaternion endOrientation = Quaternion(NAN, NAN, NAN, NAN);
    float beginScale = 1.0f;
    float endScale = NAN;
    float frequency = 5.0f;
    float initTime = 0.0f;
    float3 emittance = float3(0.0f, 0.0f, 0.0f);
    std::filesystem::path rectEmitterTexPath;

    for (int i = 0; i < argc; ++i) {
        const char* arg = argv[i];

        const auto computeOrientation = [&argc, &argv, &i](const char* arg, Quaternion* ori) {
            if (!allFinite(*ori))
                *ori = Quaternion();
            if (strncmp(arg, "-roll", 6) == 0) {
                if (i + 1 >= argc) {
                    hpprintf("Invalid option.\n");
                    exit(EXIT_FAILURE);
                }
                *ori = qRotateZ(atof(argv[i + 1]) * pi_v<float> / 180) * *ori;
                i += 1;
            }
            else if (strncmp(arg, "-pitch", 7) == 0) {
                if (i + 1 >= argc) {
                    hpprintf("Invalid option.\n");
                    exit(EXIT_FAILURE);
                }
                *ori = qRotateX(atof(argv[i + 1]) * pi_v<float> / 180) * *ori;
                i += 1;
            }
            else if (strncmp(arg, "-yaw", 5) == 0) {
                if (i + 1 >= argc) {
                    hpprintf("Invalid option.\n");
                    exit(EXIT_FAILURE);
                }
                *ori = qRotateY(atof(argv[i + 1]) * pi_v<float> / 180) * *ori;
                i += 1;
            }
        };

        if (strncmp(arg, "-", 1) != 0)
            continue;

        if (strncmp(arg, "-screenshot", 12) == 0) {
            g_takeScreenShot = true;
        }
        else if (strncmp(arg, "-cam-pos", 9) == 0) {
            if (i + 3 >= argc) {
                hpprintf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }
            g_cameraPosition = float3(atof(argv[i + 1]), atof(argv[i + 2]), atof(argv[i + 3]));
            i += 3;
        }
        else if (strncmp(arg, "-cam-roll", 10) == 0 ||
                 strncmp(arg, "-cam-pitch", 11) == 0 ||
                 strncmp(arg, "-cam-yaw", 9) == 0) {
            computeOrientation(arg + 4, &camOrientation);
        }
        else if (strncmp(arg, "-brightness", 12) == 0) {
            if (i + 1 >= argc) {
                hpprintf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }
            g_initBrightness = std::fmin(std::fmax(std::atof(argv[i + 1]), -5.0f), 5.0f);
            i += 1;
        }
        else if (strncmp(arg, "-env-texture", 13) == 0) {
            if (i + 1 >= argc) {
                hpprintf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }
            g_envLightTexturePath = argv[i + 1];
            i += 1;
        }
        else if (strncmp(arg, "-name", 6) == 0) {
            if (i + 1 >= argc) {
                hpprintf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }
            name = argv[i + 1];
            i += 1;
        }
        else if (0 == strncmp(arg, "-emittance", 11)) {
            if (i + 3 >= argc) {
                printf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }
            emittance = float3(atof(argv[i + 1]), atof(argv[i + 2]), atof(argv[i + 3]));
            if (!allFinite(emittance)) {
                printf("Invalid value.\n");
                exit(EXIT_FAILURE);
            }
            i += 3;
        }
        else if (0 == strncmp(arg, "-rect-emitter-tex", 18)) {
            if (i + 1 >= argc) {
                printf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }
            rectEmitterTexPath = argv[i + 1];
            i += 1;
        }
        else if (0 == strncmp(arg, "-obj", 5)) {
            if (i + 3 >= argc) {
                printf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }

            MeshInfo info = MeshGeometryInfo();
            auto &mesh = std::get<MeshGeometryInfo>(info);
            mesh.path = std::filesystem::path(argv[i + 1]);
            mesh.preScale = atof(argv[i + 2]);
            std::string matConv = argv[i + 3];
            if (matConv == "trad") {
                mesh.matConv = MaterialConvention::Traditional;
            }
            else if (matConv == "simple_pbr") {
                mesh.matConv = MaterialConvention::SimplePBR;
            }
            else {
                printf("Invalid material convention.\n");
                exit(EXIT_FAILURE);
            }

            g_meshInfos[name] = info;

            i += 3;
        }
        else if (0 == strncmp(arg, "-rectangle", 11)) {
            if (i + 2 >= argc) {
                printf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }

            MeshInfo info = RectangleGeometryInfo();
            auto &rect = std::get<RectangleGeometryInfo>(info);
            rect.dimX = atof(argv[i + 1]);
            rect.dimZ = atof(argv[i + 2]);
            rect.emittance = emittance;
            rect.emitterTexPath = rectEmitterTexPath;
            g_meshInfos[name] = info;

            emittance = float3(0.0f, 0.0f, 0.0f);
            rectEmitterTexPath = "";

            i += 2;
        }
        else if (0 == strncmp(arg, "-begin-pos", 11)) {
            if (i + 3 >= argc) {
                printf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }
            beginPosition = float3(atof(argv[i + 1]), atof(argv[i + 2]), atof(argv[i + 3]));
            if (!allFinite(beginPosition)) {
                printf("Invalid value.\n");
                exit(EXIT_FAILURE);
            }
            i += 3;
        }
        else if (strncmp(arg, "-begin-roll", 10) == 0 ||
                 strncmp(arg, "-begin-pitch", 11) == 0 ||
                 strncmp(arg, "-begin-yaw", 9) == 0) {
            computeOrientation(arg + 6, &beginOrientation);
        }
        else if (0 == strncmp(arg, "-begin-scale", 13)) {
            if (i + 1 >= argc) {
                printf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }
            beginScale = atof(argv[i + 1]);
            if (!isfinite(beginScale)) {
                printf("Invalid value.\n");
                exit(EXIT_FAILURE);
            }
            i += 1;
        }
        else if (0 == strncmp(arg, "-end-pos", 9)) {
            if (i + 3 >= argc) {
                printf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }
            endPosition = float3(atof(argv[i + 1]), atof(argv[i + 2]), atof(argv[i + 3]));
            if (!allFinite(endPosition)) {
                printf("Invalid value.\n");
                exit(EXIT_FAILURE);
            }
            i += 3;
        }
        else if (strncmp(arg, "-end-roll", 10) == 0 ||
                 strncmp(arg, "-end-pitch", 11) == 0 ||
                 strncmp(arg, "-end-yaw", 9) == 0) {
            computeOrientation(arg + 4, &endOrientation);
        }
        else if (0 == strncmp(arg, "-end-scale", 11)) {
            if (i + 1 >= argc) {
                printf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }
            endScale = atof(argv[i + 1]);
            if (!isfinite(endScale)) {
                printf("Invalid value.\n");
                exit(EXIT_FAILURE);
            }
            i += 1;
        }
        else if (0 == strncmp(arg, "-freq", 6)) {
            if (i + 1 >= argc) {
                printf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }
            frequency = atof(argv[i + 1]);
            if (!isfinite(frequency)) {
                printf("Invalid value.\n");
                exit(EXIT_FAILURE);
            }
            i += 1;
        }
        else if (0 == strncmp(arg, "-time", 6)) {
            if (i + 1 >= argc) {
                printf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }
            initTime = atof(argv[i + 1]);
            if (!isfinite(initTime)) {
                printf("Invalid value.\n");
                exit(EXIT_FAILURE);
            }
            i += 1;
        }
        else if (0 == strncmp(arg, "-inst", 6)) {
            if (i + 1 >= argc) {
                printf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }

            MeshInstanceInfo info;
            info.name = argv[i + 1];
            info.beginPosition = beginPosition;
            info.beginOrientation = beginOrientation;
            info.beginScale = beginScale;
            info.endPosition = allFinite(endPosition) ? endPosition : beginPosition;
            info.endOrientation = endOrientation.allFinite() ? endOrientation : beginOrientation;
            info.endScale = std::isfinite(endScale) ? endScale : beginScale;
            info.frequency = frequency;
            info.initTime = initTime;
            g_meshInstInfos.push_back(info);

            beginPosition = float3(0.0f, 0.0f, 0.0f);
            endPosition = float3(NAN, NAN, NAN);
            beginOrientation = Quaternion();
            endOrientation = Quaternion(NAN, NAN, NAN, NAN);
            beginScale = 1.0f;
            endScale = NAN;
            frequency = 5.0f;
            initTime = 0.0f;

            i += 1;
        }
        else if (strncmp(arg, "-position-encoding", 19) == 0) {
            if (i + 1 >= argc) {
                printf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }
            const char* enc = argv[i + 1];
            if (strncmp(enc, "tri-wave", 8) == 0) {
                g_positionEncoding = PositionEncoding::TriangleWave;
            }
            else if (strncmp(enc, "hash-grid", 10) == 0) {
                g_positionEncoding = PositionEncoding::HashGrid;
            }
            else {
                printf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }
            i += 1;
        }
        else if (0 == strncmp(arg, "-num-hidden-layers", 19)) {
            if (i + 1 >= argc) {
                printf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }
            g_numHiddenLayers = atoi(argv[i + 1]);
            i += 1;
        }
        else if (0 == strncmp(arg, "-learning-rate", 15)) {
            if (i + 1 >= argc) {
                printf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }
            g_learningRate = atof(argv[i + 1]);
            if (!isfinite(g_learningRate)) {
                printf("Invalid value.\n");
                exit(EXIT_FAILURE);
            }
            i += 1;
        }
        else if (0 == strncmp(arg, "-exp_name", 10)) {
            if (i + 1 >= argc) {
                printf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }
            exp_name = argv[i + 1];
            i += 1;
        }
        else if (0 == strncmp(arg, "-rnd_seed", 10)) {
            if (i + 1 >= argc) {
                printf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }
            rngSeed = std::stol(argv[i + 1]);
            i += 1;
        }
        else if (0 == strncmp(arg, "-max_path_len", 14)) {
            if (i + 1 >= argc) {
                printf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }
            maxPathLength = std::stol(argv[i + 1]);
            i += 1;
        }
        else if (0 == strncmp(arg, "-spp", 5)) {
            if (i + 1 >= argc) {
                printf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }
            SPP = std::stol(argv[i + 1]);
            i += 1;
        }
        else if (0 == strncmp(arg, "-frame_num", 11)) {
            if (i + 1 >= argc) {
                printf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }
            frameStop = std::stol(argv[i + 1]);
            i += 1;
        }
        else if (0 == strncmp(arg, "-save_img_every", 16)) {
            if (i + 1 >= argc) {
                printf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }
            saveImgEvery = std::stol(argv[i + 1]);
            i += 1;
        }
        else if (0 == strncmp(arg, "-hash_n_levels", 15)) {
            if (i + 1 >= argc) {
                printf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }
            NRCConfig.hashNLevels = std::stol(argv[i + 1]);
            i += 1;
        }
        else if (0 == strncmp(arg, "-tri_n_frequency", 17)) {
            if (i + 1 >= argc) {
                printf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }
            NRCConfig.triNFrequency = std::stol(argv[i + 1]);
            i += 1;
        }
        else if (0 == strncmp(arg, "-motion_n_frequency", 20)) {
            if (i + 1 >= argc) {
                printf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }
            NRCConfig.motionFrequency = std::stol(argv[i + 1]);
            i += 1;
        }
        else if (0 == strncmp(arg, "-net_width", 11)) {
            if (i + 1 >= argc) {
                printf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }
            NRCConfig.hiddenLayerWidth = std::stol(argv[i + 1]);
            i += 1;
        }
        else if (0 == strncmp(arg, "-radiance_scale", 16)) {
            if (i + 1 >= argc) {
                printf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }
            log10RadianceScale = std::atof(argv[i + 1]);
            i += 1;
        }
        else if (0 == strncmp(arg, "-use_separate_nrc", 18)) {
            useSeparateNRC = true;
        }
        else if (strncmp(arg, "-render_mode", 13) == 0) {
            if (i + 1 >= argc) {
                printf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }
            const char* enc = argv[i + 1];
            if (strncmp(enc, "pt", 2) == 0) {
                useNRC = false;
            }
            else if (strncmp(enc, "separate_nrc_diffuse", 20) == 0) {
                bufferTypeToDisplay = shared::BufferToDisplay::SeparateNRCDiffuse;
            }
            else if (strncmp(enc, "separate_nrc_specular", 21) == 0) {
                bufferTypeToDisplay = shared::BufferToDisplay::SeparateNRCSpecular;
            }
            else if (strncmp(enc, "nrc_only_raw", 12) == 0) {
                bufferTypeToDisplay = shared::BufferToDisplay::NRCOnlyRaw;
            }
            else if (strncmp(enc, "nrc_only_emit", 13) == 0) {
                bufferTypeToDisplay = shared::BufferToDisplay::NRCOnlyEmissive;
            }
            else if (strncmp(enc, "nrc_only", 8) == 0) {
                bufferTypeToDisplay = shared::BufferToDisplay::NRCOnly;
            }
            else if (strncmp(enc, "nrc", 3) == 0) {
                bufferTypeToDisplay = shared::BufferToDisplay::NoisyBeauty;
            }
            else {
                printf("Invalid option.\n");
                exit(EXIT_FAILURE);
            }
            i += 1;
        }
        else if (0 == strncmp(arg, "-unbiased_restir", 17)) {
            // curRenderer = ReSTIRRenderer::OriginalReSTIRUnbiased;
        }
        else if (0 == strncmp(arg, "-warp_dynamic_coord", 18)) {
            // warp query coordinates on dynamic objects into a canonical space
            // at the moment only support one dynamic object 
            warpDyCoord = true;
        }
        else if (0 == strncmp(arg, "-perturb_smooth_range", 23)) {
            // enforcing smoothness in MLP prediction by perturbing train query and encourage same output
            perturbSmoothRange = atof(argv[i + 1]);
            i += 1;
        }
        else if (0 == strncmp(arg, "-perturb_smooth_times", 23)) {
            perturbSmoothTimes = std::stol(argv[i + 1]);
            i += 1;
        }
        else if (0 == strncmp(arg, "-perturb_smooth_after", 23)) {
            perturbSmoothAfter = std::stol(argv[i + 1]);
            i += 1;
        }
        else if (0 == strncmp(arg, "-perturb_smooth", 15)) {
            // enforcing smoothness in MLP prediction by perturbing train query and encourage same output
            perturbSmooth = true;
        }
        else if (0 == strncmp(arg, "-save_py_cache", 15)) {
            // save radiance querys to run the neural networks in python
            savePyCache = true;
        }
        else if (0 == strncmp(arg, "-cam-move-x", 11)) {
            camMoveX = std::stol(argv[i + 1]);
            i += 1;
        }
        else if (0 == strncmp(arg, "-cam-move-y", 11)) {
            camMoveY = std::stol(argv[i + 1]);
            i += 1;
        }
        else if (0 == strncmp(arg, "-cam-move-z", 11)) {
            camMoveZ = std::stol(argv[i + 1]);
            i += 1;
        }
        else if (0 == strncmp(arg, "-cam-speed", 11)) {
            g_cameraPositionalMovingSpeed = std::atof(argv[i + 1]);
            i += 1;
        }
        else {
            printf("Unknown option: %s.\n", arg);
            exit(EXIT_FAILURE);
        }
    }

    g_cameraOrientation = camOrientation;
}



static void glfw_error_callback(int32_t error, const char* description) {
    hpprintf("Error %d: %s\n", error, description);
}



namespace ImGui {
    template <typename EnumType>
    bool RadioButtonE(const char* label, EnumType* v, EnumType v_button) {
        return RadioButton(label, reinterpret_cast<int*>(v), static_cast<int>(v_button));
    }

    bool InputLog2Int(const char* label, int* v, int max_v, int num_digits = 3) {
        float buttonSize = GetFrameHeight();
        float itemInnerSpacingX = GetStyle().ItemInnerSpacing.x;

        BeginGroup();
        PushID(label);

        ImGui::AlignTextToFramePadding();
        SetNextItemWidth(std::max(1.0f, CalcItemWidth() - (buttonSize + itemInnerSpacingX) * 2));
        Text("%s: %*u", label, num_digits, 1 << *v);
        bool changed = false;
        SameLine(0, itemInnerSpacingX);
        if (Button("-", ImVec2(buttonSize, buttonSize))) {
            *v = std::max(*v - 1, 0);
            changed = true;
        }
        SameLine(0, itemInnerSpacingX);
        if (Button("+", ImVec2(buttonSize, buttonSize))) {
            *v = std::min(*v + 1, max_v);
            changed = true;
        }

        PopID();
        EndGroup();

        return changed;
    }
}

int32_t main(int32_t argc, const char* argv[]) try {
    const std::filesystem::path exeDir = getExecutableDirectory();

    parseCommandline(argc, argv);

    // ----------------------------------------------------------------
    // JP: OpenGL, GLFWの初期化。
    // EN: Initialize OpenGL and GLFW.

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        hpprintf("Failed to initialize GLFW.\n");
        return -1;
    }

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();

    constexpr bool enableGLDebugCallback = DEBUG_SELECT(true, false);

    // JP: OpenGL 4.6 Core Profileのコンテキストを作成する。
    // EN: Create an OpenGL 4.6 core profile context.
    const uint32_t OpenGLMajorVersion = 4;
    const uint32_t OpenGLMinorVersion = 6;
    const char* glsl_version = "#version 460";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, OpenGLMajorVersion);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, OpenGLMinorVersion);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    if constexpr (enableGLDebugCallback)
        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);

    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);

    // int32_t renderTargetSizeX = 1920;
    // int32_t renderTargetSizeY = 1080;
    int32_t renderTargetSizeX = 512;
    int32_t renderTargetSizeY = 512;
    // int32_t renderTargetSizeX = 256;
    // int32_t renderTargetSizeY = 256;

    // JP: ウインドウの初期化。
    // EN: Initialize a window.
    float contentScaleX, contentScaleY;
    // glfwGetMonitorContentScale(monitor, &contentScaleX, &contentScaleY);
    // std::cout << "contentScaleX: " << contentScaleX << "\n";
    // std::cout << "contentScaleY: " << contentScaleY << "\n";
    contentScaleX = 1920/renderTargetSizeX;
    contentScaleY = 1080/renderTargetSizeY;
    float UIScaling = contentScaleX;
    GLFWwindow* window = glfwCreateWindow(static_cast<int32_t>(1920),
                                          static_cast<int32_t>(1080),
                                          "Neural Radiance Caching", NULL, NULL);
    glfwSetWindowUserPointer(window, nullptr);
    if (!window) {
        hpprintf("Failed to create a GLFW window.\n");
        glfwTerminate();
        return -1;
    }

    int32_t curFBWidth;
    int32_t curFBHeight;
    glfwGetFramebufferSize(window, &curFBWidth, &curFBHeight);

    glfwMakeContextCurrent(window);

    glfwSwapInterval(1); // Enable vsync



    // JP: gl3wInit()は何らかのOpenGLコンテキストが作られた後に呼ぶ必要がある。
    // EN: gl3wInit() must be called after some OpenGL context has been created.
    int32_t gl3wRet = gl3wInit();
    if (!gl3wIsSupported(OpenGLMajorVersion, OpenGLMinorVersion)) {
        hpprintf("gl3w doesn't support OpenGL %u.%u\n", OpenGLMajorVersion, OpenGLMinorVersion);
        glfwTerminate();
        return -1;
    }

    if constexpr (enableGLDebugCallback) {
        glu::enableDebugCallback(true);
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, false);
    }

    // END: Initialize OpenGL and GLFW.
    // ----------------------------------------------------------------



    // ----------------------------------------------------------------
    // JP: 入力コールバックの設定。
    // EN: Set up input callbacks.

    glfwSetMouseButtonCallback(
        window,
        [](GLFWwindow* window, int32_t button, int32_t action, int32_t mods) {
            uint64_t &frameIndex = *(uint64_t*)glfwGetWindowUserPointer(window);

            switch (button) {
            case GLFW_MOUSE_BUTTON_MIDDLE: {
                devPrintf("Mouse Middle\n");
                g_buttonRotate.recordStateChange(action == GLFW_PRESS, frameIndex);
                break;
            }
            default:
                break;
            }
        });
    glfwSetCursorPosCallback(
        window,
        [](GLFWwindow* window, double x, double y) {
            g_mouseX = x;
            g_mouseY = y;
        });
    glfwSetKeyCallback(
        window,
        [](GLFWwindow* window, int32_t key, int32_t scancode, int32_t action, int32_t mods) {
            uint64_t &frameIndex = *(uint64_t*)glfwGetWindowUserPointer(window);

            switch (key) {
            case GLFW_KEY_W: {
                g_keyForward.recordStateChange(action == GLFW_PRESS || action == GLFW_REPEAT, frameIndex);
                break;
            }
            case GLFW_KEY_S: {
                g_keyBackward.recordStateChange(action == GLFW_PRESS || action == GLFW_REPEAT, frameIndex);
                break;
            }
            case GLFW_KEY_A: {
                g_keyLeftward.recordStateChange(action == GLFW_PRESS || action == GLFW_REPEAT, frameIndex);
                break;
            }
            case GLFW_KEY_D: {
                g_keyRightward.recordStateChange(action == GLFW_PRESS || action == GLFW_REPEAT, frameIndex);
                break;
            }
            case GLFW_KEY_R: {
                g_keyUpward.recordStateChange(action == GLFW_PRESS || action == GLFW_REPEAT, frameIndex);
                break;
            }
            case GLFW_KEY_F: {
                g_keyDownward.recordStateChange(action == GLFW_PRESS || action == GLFW_REPEAT, frameIndex);
                break;
            }
            case GLFW_KEY_Q: {
                g_keyTiltLeft.recordStateChange(action == GLFW_PRESS || action == GLFW_REPEAT, frameIndex);
                break;
            }
            case GLFW_KEY_E: {
                g_keyTiltRight.recordStateChange(action == GLFW_PRESS || action == GLFW_REPEAT, frameIndex);
                break;
            }
            case GLFW_KEY_T: {
                g_keyFasterPosMovSpeed.recordStateChange(action == GLFW_PRESS || action == GLFW_REPEAT, frameIndex);
                break;
            }
            case GLFW_KEY_G: {
                g_keySlowerPosMovSpeed.recordStateChange(action == GLFW_PRESS || action == GLFW_REPEAT, frameIndex);
                break;
            }
            default:
                break;
            }
        });

    // END: Set up input callbacks.
    // ----------------------------------------------------------------



    // ----------------------------------------------------------------
    // JP: ImGuiの初期化。
    // EN: Initialize ImGui.

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // Enable Gamepad Controls
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Setup style
    // JP: ガンマ補正が有効なレンダーターゲットで、同じUIの見た目を得るためにデガンマされたスタイルも用意する。
    // EN: Prepare a degamma-ed style to have the identical UI appearance on gamma-corrected render target.
    ImGuiStyle guiStyle, guiStyleWithGamma;
    ImGui::StyleColorsDark(&guiStyle);
    guiStyleWithGamma = guiStyle;
    const auto degamma = [](const ImVec4 &color) {
        return ImVec4(sRGB_degamma_s(color.x),
                      sRGB_degamma_s(color.y),
                      sRGB_degamma_s(color.z),
                      color.w);
    };
    for (int i = 0; i < ImGuiCol_COUNT; ++i) {
        guiStyleWithGamma.Colors[i] = degamma(guiStyleWithGamma.Colors[i]);
    }
    ImGui::GetStyle() = guiStyleWithGamma;

    // END: Initialize ImGui.
    // ----------------------------------------------------------------



    GPUEnvironment gpuEnv;
    gpuEnv.initialize();

    Scene scene;
    scene.initialize(
        getExecutableDirectory() / "neural_radiance_caching/ptxes",
        gpuEnv.cuContext, gpuEnv.optixContext, shared::maxNumRayTypes, gpuEnv.optixDefaultMaterial);

    CUstream cuStream;
    CUDADRV_CHECK(cuStreamCreate(&cuStream, 0));

    // ----------------------------------------------------------------
    // JP: シーンのセットアップ。
    // EN: Setup a scene.

    scene.map();

    for (auto it = g_meshInfos.cbegin(); it != g_meshInfos.cend(); ++it) {
        const MeshInfo &info = it->second;

        if (std::holds_alternative<MeshGeometryInfo>(info)) {
            const auto &meshInfo = std::get<MeshGeometryInfo>(info);

            createTriangleMeshes(it->first,
                                 meshInfo.path, meshInfo.matConv,
                                 scale4x4(meshInfo.preScale),
                                 gpuEnv.cuContext, &scene);
        }
        else if (std::holds_alternative<RectangleGeometryInfo>(info)) {
            const auto &rectInfo = std::get<RectangleGeometryInfo>(info);

            createRectangleLight(it->first,
                                 rectInfo.dimX, rectInfo.dimZ,
                                 float3(0.01f),
                                 rectInfo.emitterTexPath, rectInfo.emittance, Matrix4x4(),
                                 gpuEnv.cuContext, &scene);
        }
    }

    Matrix4x4 initDyObjMat; 
    Matrix4x4 currentDyObjMat; // to ward dynamic object to canonical space
    Matrix4x4 transMat;
    CUdeviceptr transMatDevicePtr;
    CUDADRV_CHECK(cuMemAlloc(&transMatDevicePtr, sizeof(transMat)));

    for (int i = 0; i < g_meshInstInfos.size(); ++i) {
        const MeshInstanceInfo &info = g_meshInstInfos[i];
        const Mesh* mesh = scene.meshes.at(info.name);
        for (int j = 0; j < mesh->groupInsts.size(); ++j) {
            const Mesh::GeometryGroupInstance &groupInst = mesh->groupInsts[j];

            Matrix4x4 instXfm =
                Matrix4x4(info.beginScale * info.beginOrientation.toMatrix3x3(), info.beginPosition);
            Instance* inst = createInstance(gpuEnv.cuContext, &scene, groupInst, instXfm);
            scene.insts.push_back(inst);

            scene.initialSceneAabb.unify(instXfm * groupInst.transform * groupInst.geomGroup->aabb);

            if (info.beginPosition != info.endPosition ||
                info.beginOrientation != info.endOrientation ||
                info.beginScale != info.endScale) {
                auto controller = new InstanceController(
                    inst,
                    info.beginScale, info.beginOrientation, info.beginPosition,
                    info.endScale, info.endOrientation, info.endPosition,
                    info.frequency, info.initTime);
                scene.instControllers.push_back(controller);
                initDyObjMat = inst->matM2W;
            }
        }
    }

    

    float3 sceneDim = scene.initialSceneAabb.maxP - scene.initialSceneAabb.minP;
    if (g_cameraPositionalMovingSpeed == 0.0f) g_cameraPositionalMovingSpeed = 0.003f * std::max({ sceneDim.x, sceneDim.y, sceneDim.z });
    g_cameraDirectionalMovingSpeed = 0.0015f;
    g_cameraTiltSpeed = 0.025f;

    scene.unmap();

    scene.setupASes(gpuEnv.cuContext);
    CUDADRV_CHECK(cuStreamSynchronize(0));

    uint32_t totalNumEmitterPrimitives = 0;
    for (int i = 0; i < scene.insts.size(); ++i) {
        const Instance* inst = scene.insts[i];
        totalNumEmitterPrimitives += inst->geomGroupInst.geomGroup->numEmitterPrimitives;
    }
    hpprintf("%u emitter primitives\n", totalNumEmitterPrimitives);

    // JP: 環境光テクスチャーを読み込んで、サンプルするためのCDFを計算する。
    // EN: Read a environmental texture, then compute a CDF to sample it.
    cudau::Array envLightArray;
    CUtexObject envLightTexture = 0;
    RegularConstantContinuousDistribution2D envLightImportanceMap;
    if (!g_envLightTexturePath.empty())
        loadEnvironmentalTexture(g_envLightTexturePath, gpuEnv.cuContext,
                                 &envLightArray, &envLightTexture, &envLightImportanceMap);

    scene.setupLightGeomDistributions();

    CUdeviceptr sceneAABBOnDevice;
    CUDADRV_CHECK(cuMemAlloc(&sceneAABBOnDevice, sizeof(AABB)));
    CUDADRV_CHECK(cuMemcpyHtoD(sceneAABBOnDevice, &scene.initialSceneAabb, sizeof(scene.initialSceneAabb)));

    // END: Setup a scene.
    // ----------------------------------------------------------------



    // ----------------------------------------------------------------
    // JP: NRCの訓練データなどに関わるバッファーを初期化する。
    // EN: Initialize NRC training-related buffers.

    // const uint32_t maxNumTrainingSuffixes = renderTargetSizeX * renderTargetSizeY / 16;
    const uint32_t maxNumTrainingSuffixes = renderTargetSizeX * renderTargetSizeY;
    // const uint32_t maxNumTrainingSuffixes = 0;
    
    cudau::TypedBuffer<uint32_t> numTrainingData[2];
    cudau::TypedBuffer<uint32_t> numIntermediateTrainingData[2];
    cudau::TypedBuffer<uint2> tileSize[2];
    cudau::TypedBuffer<float3AsOrderedInt> targetMinMax[2];
    cudau::TypedBuffer<float3> targetAvg[2];
    for (int i = 0; i < 2; ++i) {
        numTrainingData[i].initialize(gpuEnv.cuContext, Scene::bufferType, 1, 0);
        numIntermediateTrainingData[i].initialize(gpuEnv.cuContext, Scene::bufferType, 1, 0);
        tileSize[i].initialize(gpuEnv.cuContext, Scene::bufferType, 1, uint2(1, 1));
        targetMinMax[i].initialize(gpuEnv.cuContext, Scene::bufferType, 2);
        targetAvg[i].initialize(gpuEnv.cuContext, Scene::bufferType, 1);
    }


    uintptr_t offsetToSelectUnbiasedTileOnDevice;
    CUDADRV_CHECK(cuMemAlloc(&offsetToSelectUnbiasedTileOnDevice, sizeof(uint32_t)));

    uintptr_t offsetToSelectTrainingPathOnDevice;
    CUDADRV_CHECK(cuMemAlloc(&offsetToSelectTrainingPathOnDevice, sizeof(uint32_t)));

    cudau::TypedBuffer<shared::RadianceQuery> trainRadianceQueryBuffer[2];
    cudau::TypedBuffer<float3> trainTargetBuffer[2];
    cudau::TypedBuffer<float3> separateNrcBuffer[2];
    cudau::TypedBuffer<shared::TrainingVertexInfo> trainVertexInfoBuffer;
    cudau::TypedBuffer<shared::TrainingSuffixTerminalInfo> trainSuffixTerminalInfoBuffer;
    cudau::TypedBuffer<shared::LinearCongruentialGenerator> dataShufflerBuffer;
    for (int i = 0; i < 2; ++i) {
        trainRadianceQueryBuffer[i].initialize(
            gpuEnv.cuContext, Scene::bufferType, shared::trainBufferSize);
        trainTargetBuffer[i].initialize(
            gpuEnv.cuContext, Scene::bufferType, shared::trainBufferSize);
        separateNrcBuffer[i].initialize(
            gpuEnv.cuContext, Scene::bufferType, shared::trainBufferSize);
    }
    trainVertexInfoBuffer.initialize(
        gpuEnv.cuContext, Scene::bufferType, shared::trainBufferSize);
    trainSuffixTerminalInfoBuffer.initialize(
        gpuEnv.cuContext, Scene::bufferType, maxNumTrainingSuffixes);
    dataShufflerBuffer.initialize(
        gpuEnv.cuContext, Scene::bufferType, shared::trainBufferSize);
    {
        shared::LinearCongruentialGenerator lcg;
        lcg.setState(471313181);
        shared::LinearCongruentialGenerator* dataShufflers = dataShufflerBuffer.map();
        for (int i = 0; i < shared::numTrainingDataPerFrame; ++i) {
            lcg.next();
            dataShufflers[i] = lcg;
        }
        dataShufflerBuffer.unmap();
    }

    NeuralRadianceCache neuralRadianceCache;
    NeuralRadianceCache neuralRadianceCacheSpecular;
    NRCConfig.posEnc = g_positionEncoding;
    NRCConfig.numHiddenLayers = g_numHiddenLayers;
    NRCConfig.learningRate = g_learningRate;

    std::cout << NRCConfig.numHiddenLayers << "\n";
    std::cout << NRCConfig.hiddenLayerWidth << "\n";

    neuralRadianceCache.initialize(NRCConfig);
    neuralRadianceCacheSpecular.initialize(NRCConfig);

    // END: Initialize NRC training-related buffers.
    // ----------------------------------------------------------------


    // // ----------------------------------------------------------------
    // // Initialize ReSTIR
    // cudau::TypedBuffer<shared::PCG32RNG> lightPreSamplingRngs;
    // cudau::TypedBuffer<shared::PreSampledLight> preSampledLights;

    // constexpr uint32_t numPreSampledLights = shared::numLightSubsets * shared::lightSubsetSize;
    // lightPreSamplingRngs.initialize(gpuEnv.cuContext, Scene::bufferType, numPreSampledLights);
    // {
    //     shared::PCG32RNG* rngs = lightPreSamplingRngs.map();
    //     std::mt19937_64 rngSeed(894213312210);
    //     for (int i = 0; i < numPreSampledLights; ++i)
    //         rngs[i].setState(rngSeed());
    //     lightPreSamplingRngs.unmap();
    // }
    // preSampledLights.initialize(gpuEnv.cuContext, Scene::bufferType, numPreSampledLights);

    // // ----------------------------------------------------------------


    // ----------------------------------------------------------------
    // JP: スクリーン関連のバッファーを初期化。
    // EN: Initialize screen-related buffers.

    cudau::Array gBuffer0[2];
    cudau::Array gBuffer1[2];
    cudau::Array gBuffer2[2];
    
    cudau::Array beautyAccumBuffer;
    cudau::Array albedoAccumBuffer;
    cudau::Array normalAccumBuffer;

    // optixu::HostBlockBuffer2D<shared::Reservoir<shared::LightSample>, 0> reservoirBuffer[2];
    // cudau::Array reservoirInfoBuffer[2];
    // cudau::Array sampleVisibilityBuffer[2];

    cudau::TypedBuffer<float4> linearBeautyBuffer;
    cudau::TypedBuffer<float4> linearAlbedoBuffer;
    cudau::TypedBuffer<float4> linearNormalBuffer;
    cudau::TypedBuffer<float2> linearFlowBuffer;
    cudau::TypedBuffer<float4> linearDenoisedBeautyBuffer;

    cudau::Array rngBuffer;

    cudau::TypedBuffer<shared::RadianceQuery> inferenceRadianceQueryBuffer;
    cudau::TypedBuffer<shared::TerminalInfo> inferenceTerminalInfoBuffer;
    cudau::TypedBuffer<float3> inferredRadianceBuffer;
    cudau::TypedBuffer<float3> inferredRadianceBuffer2;
    cudau::TypedBuffer<float3> perFrameContributionBuffer;
    cudau::TypedBuffer<float3> inferredTrainRadianceBuffer; // for perturb smooth
    // curand::curandState *curand_state;
    // cudaMalloc(&curand_state, sizeof(curand::curandState));

    std::random_device dev;
    std::mt19937 global_rng(dev());
    std::uniform_real_distribution<float> float_dist(-perturbSmoothRange, perturbSmoothRange);

    CUdeviceptr trainedQueryPerturbDevicePtr;
    float3 trainedQueryPerturbs[50000];

    CUDADRV_CHECK(cuMemAlloc(&trainedQueryPerturbDevicePtr, sizeof(trainedQueryPerturbs)));

    // for (int i=0; i<16; ++i)
    //     std::cout << float_dist(global_rng) << "\n";


    const auto initializeScreenRelatedBuffers = [&]() {
        uint32_t numPixels = renderTargetSizeX * renderTargetSizeY;

        for (int i = 0; i < 2; ++i) {
            gBuffer0[i].initialize2D(
                gpuEnv.cuContext, cudau::ArrayElementType::UInt32, (sizeof(shared::GBuffer0) + 3) / 4,
                cudau::ArraySurface::Enable, cudau::ArrayTextureGather::Disable,
                renderTargetSizeX, renderTargetSizeY, 1);
            gBuffer1[i].initialize2D(
                gpuEnv.cuContext, cudau::ArrayElementType::UInt32, (sizeof(shared::GBuffer1) + 3) / 4,
                cudau::ArraySurface::Enable, cudau::ArrayTextureGather::Disable,
                renderTargetSizeX, renderTargetSizeY, 1);
            gBuffer2[i].initialize2D(
                gpuEnv.cuContext, cudau::ArrayElementType::UInt32, (sizeof(shared::GBuffer2) + 3) / 4,
                cudau::ArraySurface::Enable, cudau::ArrayTextureGather::Disable,
                renderTargetSizeX, renderTargetSizeY, 1);

            // reservoirBuffer[i].initialize(gpuEnv.cuContext, Scene::bufferType,
            //                               renderTargetSizeX, renderTargetSizeY);
            // reservoirInfoBuffer[i].initialize2D(
            //     gpuEnv.cuContext, cudau::ArrayElementType::UInt32, (sizeof(shared::ReservoirInfo) + 3) / 4,
            //     cudau::ArraySurface::Enable, cudau::ArrayTextureGather::Disable,
            //     renderTargetSizeX, renderTargetSizeY, 1);

            // sampleVisibilityBuffer[i].initialize2D(
            //     gpuEnv.cuContext, cudau::ArrayElementType::UInt32, (sizeof(shared::SampleVisibility) + 3) / 4,
            //     cudau::ArraySurface::Enable, cudau::ArrayTextureGather::Disable,
            //     renderTargetSizeX, renderTargetSizeY, 1);
        }

        beautyAccumBuffer.initialize2D(gpuEnv.cuContext, cudau::ArrayElementType::Float32, 4,
                                       cudau::ArraySurface::Enable, cudau::ArrayTextureGather::Disable,
                                       renderTargetSizeX, renderTargetSizeY, 1);
        albedoAccumBuffer.initialize2D(gpuEnv.cuContext, cudau::ArrayElementType::Float32, 4,
                                       cudau::ArraySurface::Enable, cudau::ArrayTextureGather::Disable,
                                       renderTargetSizeX, renderTargetSizeY, 1);
        normalAccumBuffer.initialize2D(gpuEnv.cuContext, cudau::ArrayElementType::Float32, 4,
                                       cudau::ArraySurface::Enable, cudau::ArrayTextureGather::Disable,
                                       renderTargetSizeX, renderTargetSizeY, 1);

        linearBeautyBuffer.initialize(gpuEnv.cuContext, Scene::bufferType, numPixels);
        linearAlbedoBuffer.initialize(gpuEnv.cuContext, Scene::bufferType, numPixels);
        linearNormalBuffer.initialize(gpuEnv.cuContext, Scene::bufferType, numPixels);
        linearFlowBuffer.initialize(gpuEnv.cuContext, Scene::bufferType, numPixels);
        linearDenoisedBeautyBuffer.initialize(gpuEnv.cuContext, Scene::bufferType, numPixels);

        rngBuffer.initialize2D(
            gpuEnv.cuContext, cudau::ArrayElementType::UInt32, (sizeof(shared::PCG32RNG) + 3) / 4,
            cudau::ArraySurface::Enable, cudau::ArrayTextureGather::Disable,
            renderTargetSizeX, renderTargetSizeY, 1);
        {
            auto rngs = rngBuffer.map<shared::PCG32RNG>();
            std::mt19937_64 rngSeed(591842031321323413);
            for (int y = 0; y < renderTargetSizeY; ++y) {
                for (int x = 0; x < renderTargetSizeX; ++x) {
                    shared::PCG32RNG &rng = rngs[y * renderTargetSizeX + x];
                    rng.setState(rngSeed());
                }
            }
            rngBuffer.unmap();
        }


        uint32_t inferenceBatchSize =
            ((numPixels + maxNumTrainingSuffixes) + 255) / 256 * 256;
        inferenceRadianceQueryBuffer.initialize(
            gpuEnv.cuContext, Scene::bufferType, inferenceBatchSize);
        inferenceTerminalInfoBuffer.initialize(gpuEnv.cuContext, Scene::bufferType, numPixels);
        inferredRadianceBuffer.initialize(
            gpuEnv.cuContext, Scene::bufferType, inferenceBatchSize);
        inferredTrainRadianceBuffer.initialize(
            gpuEnv.cuContext, Scene::bufferType, inferenceBatchSize);
        inferredRadianceBuffer2.initialize(
            gpuEnv.cuContext, Scene::bufferType, inferenceBatchSize);
        perFrameContributionBuffer.initialize(gpuEnv.cuContext, Scene::bufferType, numPixels);
    };
    const auto finalizeScreenRelatedBuffers = [&]() {
        perFrameContributionBuffer.finalize();
        inferredRadianceBuffer.finalize();
        inferredTrainRadianceBuffer.finalize();
        inferredRadianceBuffer2.finalize();
        inferenceTerminalInfoBuffer.finalize();
        inferenceRadianceQueryBuffer.finalize();

        rngBuffer.finalize();

        linearDenoisedBeautyBuffer.finalize();
        linearFlowBuffer.finalize();
        linearNormalBuffer.finalize();
        linearAlbedoBuffer.finalize();
        linearBeautyBuffer.finalize();

        normalAccumBuffer.finalize();
        albedoAccumBuffer.finalize();
        beautyAccumBuffer.finalize();

        for (int i = 1; i >= 0; --i) {
            gBuffer2[i].finalize();
            gBuffer1[i].finalize();
            gBuffer0[i].finalize();
        }
    };

    const auto resizeScreenRelatedBuffers = [&](uint32_t width, uint32_t height) {
        uint32_t numPixels = width * height;

        for (int i = 0; i < 2; ++i) {
            gBuffer0[i].resize(width, height);
            gBuffer1[i].resize(width, height);
            gBuffer2[i].resize(width, height);

            // reservoirBuffer[i].resize(renderTargetSizeX, renderTargetSizeY);
            // reservoirInfoBuffer[i].resize(renderTargetSizeX, renderTargetSizeY);

            // sampleVisibilityBuffer[i].resize(renderTargetSizeX, renderTargetSizeY);
        }

        beautyAccumBuffer.resize(width, height);
        albedoAccumBuffer.resize(width, height);
        normalAccumBuffer.resize(width, height);

        linearBeautyBuffer.resize(numPixels);
        linearAlbedoBuffer.resize(numPixels);
        linearNormalBuffer.resize(numPixels);
        linearFlowBuffer.resize(numPixels);
        linearDenoisedBeautyBuffer.resize(numPixels);

        rngBuffer.resize(width, height);
        {
            auto rngs = rngBuffer.map<shared::PCG32RNG>();
            std::mt19937_64 rngSeed(591842031321323413);
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    shared::PCG32RNG &rng = rngs[y * renderTargetSizeX + x];
                    rng.setState(rngSeed());
                }
            }
            rngBuffer.unmap();
        }



        uint32_t inferenceBatchSize =
            ((numPixels + maxNumTrainingSuffixes) + 255) / 256 * 256;
        inferenceRadianceQueryBuffer.resize(inferenceBatchSize);
        inferenceTerminalInfoBuffer.resize(numPixels);
        inferredRadianceBuffer.resize(inferenceBatchSize);
        inferredTrainRadianceBuffer.resize(inferenceBatchSize);
        inferredRadianceBuffer2.resize(inferenceBatchSize);
        perFrameContributionBuffer.resize(numPixels);
    };

    initializeScreenRelatedBuffers();

    // END: Initialize screen-related buffers.
    // ----------------------------------------------------------------



    // ----------------------------------------------------------------
    // JP: デノイザーのセットアップ。
    //     Temporalデノイザーを使用する。
    // EN: Setup a denoiser.
    //     Use the temporal denoiser.

    constexpr bool useTiledDenoising = false; // Change this to true to use tiled denoising.
    constexpr uint32_t tileWidth = useTiledDenoising ? 256 : 0;
    constexpr uint32_t tileHeight = useTiledDenoising ? 256 : 0;
    optixu::Denoiser denoiser = gpuEnv.optixContext.createDenoiser(
        OPTIX_DENOISER_MODEL_KIND_TEMPORAL, true, true);
    optixu::DenoiserSizes denoiserSizes;
    uint32_t numTasks;
    denoiser.prepare(
        renderTargetSizeX, renderTargetSizeY, tileWidth, tileHeight,
        &denoiserSizes, &numTasks);
    hpprintf("Denoiser State Buffer: %llu bytes\n", denoiserSizes.stateSize);
    hpprintf("Denoiser Scratch Buffer: %llu bytes\n", denoiserSizes.scratchSize);
    hpprintf("Compute Intensity Scratch Buffer: %llu bytes\n",
             denoiserSizes.scratchSizeForComputeNormalizer);
    cudau::Buffer denoiserStateBuffer;
    cudau::Buffer denoiserScratchBuffer;
    denoiserStateBuffer.initialize(
        gpuEnv.cuContext, Scene::bufferType, denoiserSizes.stateSize, 1);
    denoiserScratchBuffer.initialize(
        gpuEnv.cuContext, Scene::bufferType,
        std::max(denoiserSizes.scratchSize, denoiserSizes.scratchSizeForComputeNormalizer), 1);

    std::vector<optixu::DenoisingTask> denoisingTasks(numTasks);
    denoiser.getTasks(denoisingTasks.data());

    denoiser.setupState(cuStream, denoiserStateBuffer, denoiserScratchBuffer);

    // JP: デノイザーは入出力にリニアなバッファーを必要とするため結果をコピーする必要がある。
    // EN: Denoiser requires linear buffers as input/output, so we need to copy the results.
    CUmodule moduleCopyBuffers;
    CUDADRV_CHECK(cuModuleLoad(
        &moduleCopyBuffers,
        (getExecutableDirectory() / "neural_radiance_caching/ptxes/copy_buffers.ptx").string().c_str()));
    cudau::Kernel kernelCopyToLinearBuffers(
        moduleCopyBuffers, "copyToLinearBuffers", cudau::dim3(8, 8), 0);
    cudau::Kernel kernelSumToLinearBuffers(
        moduleCopyBuffers, "sumToLinearBuffers", cudau::dim3(8, 8), 0);
    cudau::Kernel kernelAvgLinearBuffers(
        moduleCopyBuffers, "avgLinearBuffers", cudau::dim3(8, 8), 0);
    cudau::Kernel kernelVisualizeToOutputBuffer(
        moduleCopyBuffers, "visualizeToOutputBuffer", cudau::dim3(8, 8), 0);

    uintptr_t plpPtrOnDeviceForCopyBuffers;
    {
        size_t plpSizeForCopyBuffers;
        CUDADRV_CHECK(cuModuleGetGlobal(
            &plpPtrOnDeviceForCopyBuffers, &plpSizeForCopyBuffers, moduleCopyBuffers, "plp"));
        Assert(sizeof(shared::PipelineLaunchParameters) == plpSizeForCopyBuffers, "Unexpected plp size.");
    }

    CUdeviceptr hdrNormalizer;
    CUDADRV_CHECK(cuMemAlloc(&hdrNormalizer, sizeof(float)));

    // END: Setup a denoiser.
    // ----------------------------------------------------------------



    // JP: OpenGL用バッファーオブジェクトからCUDAバッファーを生成する。
    // EN: Create a CUDA buffer from an OpenGL buffer instObject0.
    glu::Texture2D outputTexture;
    cudau::Array outputArray;
    cudau::InteropSurfaceObjectHolder<2> outputBufferSurfaceHolder;
    outputTexture.initialize(GL_RGBA32F, renderTargetSizeX, renderTargetSizeY, 1);
    outputArray.initializeFromGLTexture2D(gpuEnv.cuContext, outputTexture.getHandle(),
                                          cudau::ArraySurface::Enable, cudau::ArrayTextureGather::Disable);
    outputBufferSurfaceHolder.initialize({ &outputArray });

    glu::Sampler outputSampler;
    outputSampler.initialize(glu::Sampler::MinFilter::Nearest, glu::Sampler::MagFilter::Nearest,
                             glu::Sampler::WrapMode::Repeat, glu::Sampler::WrapMode::Repeat);



    // JP: フルスクリーンクアッド(or 三角形)用の空のVAO。
    // EN: Empty VAO for full screen qud (or triangle).
    glu::VertexArray vertexArrayForFullScreen;
    vertexArrayForFullScreen.initialize();

    // JP: OptiXの結果をフレームバッファーにコピーするシェーダー。
    // EN: Shader to copy OptiX result to a frame buffer.
    glu::GraphicsProgram drawOptiXResultShader;
    drawOptiXResultShader.initializeVSPS(
        readTxtFile(exeDir / "neural_radiance_caching/shaders/drawOptiXResult.vert"),
        readTxtFile(exeDir / "neural_radiance_caching/shaders/drawOptiXResult.frag"));


    // // ----------------------------------------------------------------
    // // JP: Spatial Reuseで使用する近傍ピクセルへの方向をLow-discrepancy数列から作成しておく。
    // // EN: Generate directions to neighboring pixels used in spatial reuse from a low-discrepancy sequence.
    // const auto computeHaltonSequence = [](uint32_t base, uint32_t idx) {
    //     const float recBase = 1.0f / base;
    //     float ret = 0.0f;
    //     float scale = 1.0f;
    //     while (idx) {
    //         scale *= recBase;
    //         ret += (idx % base) * scale;
    //         idx /= base;
    //     }
    //     return ret;
    // };
    // const auto concentricSampleDisk = [](float u0, float u1, float* dx, float* dy) {
    //     float r, theta;
    //     float sx = 2 * u0 - 1;
    //     float sy = 2 * u1 - 1;

    //     if (sx == 0 && sy == 0) {
    //         *dx = 0;
    //         *dy = 0;
    //         return;
    //     }
    //     if (sx >= -sy) { // region 1 or 2
    //         if (sx > sy) { // region 1
    //             r = sx;
    //             theta = sy / sx;
    //         }
    //         else { // region 2
    //             r = sy;
    //             theta = 2 - sx / sy;
    //         }
    //     }
    //     else { // region 3 or 4
    //         if (sx > sy) {/// region 4
    //             r = -sy;
    //             theta = 6 + sx / sy;
    //         }
    //         else {// region 3
    //             r = -sx;
    //             theta = 4 + sy / sx;
    //         }
    //     }
    //     theta *= pi_v<float> / 4;
    //     *dx = r * cos(theta);
    //     *dy = r * sin(theta);
    // };
    // std::vector<float2> spatialNeighborDeltasOnHost(1024);
    // for (int i = 0; i < spatialNeighborDeltasOnHost.size(); ++i) {
    //     float2 delta;
    //     concentricSampleDisk(computeHaltonSequence(2, i), computeHaltonSequence(3, i), &delta.x, &delta.y);
    //     spatialNeighborDeltasOnHost[i] = delta;
    //     //printf("%g, %g\n", delta.x, delta.y);
    // }
    // cudau::TypedBuffer<float2> spatialNeighborDeltas(
    //     gpuEnv.cuContext, Scene::bufferType, spatialNeighborDeltasOnHost);

    // // End generate directions to neighboring pixels used in spatial reuse from a low-discrepancy sequence.
    // // ----------------------------------------------------------------



    shared::StaticPipelineLaunchParameters staticPlp = {};
    {
        staticPlp.imageSize = int2(renderTargetSizeX, renderTargetSizeY);
        staticPlp.rngBuffer = rngBuffer.getSurfaceObject(0);

        staticPlp.GBuffer0[0] = gBuffer0[0].getSurfaceObject(0);
        staticPlp.GBuffer0[1] = gBuffer0[1].getSurfaceObject(0);
        staticPlp.GBuffer1[0] = gBuffer1[0].getSurfaceObject(0);
        staticPlp.GBuffer1[1] = gBuffer1[1].getSurfaceObject(0);
        staticPlp.GBuffer2[0] = gBuffer2[0].getSurfaceObject(0);
        staticPlp.GBuffer2[1] = gBuffer2[1].getSurfaceObject(0);

        staticPlp.materialDataBuffer = scene.materialDataBuffer.getDevicePointer();
        staticPlp.geometryInstanceDataBuffer = scene.geomInstDataBuffer.getDevicePointer();
        envLightImportanceMap.getDeviceType(&staticPlp.envLightImportanceMap);
        staticPlp.envLightTexture = envLightTexture;

        staticPlp.sceneAABB = reinterpret_cast<AABB*>(sceneAABBOnDevice);

        staticPlp.maxNumTrainingSuffixes = maxNumTrainingSuffixes;
        for (int i = 0; i < 2; ++i) {
            staticPlp.numTrainingData[i] = numTrainingData[i].getDevicePointer();
            staticPlp.numIntermediateTrainingData[i] = numIntermediateTrainingData[i].getDevicePointer();
            staticPlp.tileSize[i] = tileSize[i].getDevicePointer();
            staticPlp.targetMinMax[i] = targetMinMax[i].getDevicePointer();
            staticPlp.targetAvg[i] = targetAvg[i].getDevicePointer();
        }
        staticPlp.offsetToSelectUnbiasedTile = reinterpret_cast<uint32_t*>(offsetToSelectUnbiasedTileOnDevice);
        staticPlp.offsetToSelectTrainingPath = reinterpret_cast<uint32_t*>(offsetToSelectTrainingPathOnDevice);
        staticPlp.inferenceRadianceQueryBuffer = inferenceRadianceQueryBuffer.getDevicePointer();
        staticPlp.inferenceTerminalInfoBuffer = inferenceTerminalInfoBuffer.getDevicePointer();
        staticPlp.inferredRadianceBuffer = inferredRadianceBuffer.getDevicePointer();
        staticPlp.inferredRadianceBuffer2 = inferredRadianceBuffer2.getDevicePointer();
        staticPlp.perFrameContributionBuffer = perFrameContributionBuffer.getDevicePointer();
        for (int i = 0; i < 2; ++i) {
            staticPlp.trainRadianceQueryBuffer[i] = trainRadianceQueryBuffer[i].getDevicePointer();
            staticPlp.trainTargetBuffer[i] = trainTargetBuffer[i].getDevicePointer();
        }
        staticPlp.trainVertexInfoBuffer = trainVertexInfoBuffer.getDevicePointer();
        staticPlp.trainSuffixTerminalInfoBuffer = trainSuffixTerminalInfoBuffer.getDevicePointer();
        staticPlp.dataShufflerBuffer = dataShufflerBuffer.getDevicePointer();

        staticPlp.beautyAccumBuffer = beautyAccumBuffer.getSurfaceObject(0);
        staticPlp.albedoAccumBuffer = albedoAccumBuffer.getSurfaceObject(0);
        staticPlp.normalAccumBuffer = normalAccumBuffer.getSurfaceObject(0);



        staticPlp.numTiles = int2((renderTargetSizeX + shared::tileSizeX - 1) / shared::tileSizeX,
                                  (renderTargetSizeY + shared::tileSizeY - 1) / shared::tileSizeY);

        printf("presample \n");
        // staticPlp.lightPreSamplingRngs = lightPreSamplingRngs.getDevicePointer();
        // staticPlp.preSampledLights = preSampledLights.getDevicePointer();

        // printf("reservoirBuffer \n");
        // staticPlp.reservoirBuffer[0] = reservoirBuffer[0].getBlockBuffer2D();
        // staticPlp.reservoirBuffer[1] = reservoirBuffer[1].getBlockBuffer2D();
        // printf("reservoirInfoBuffer \n");
        // staticPlp.reservoirInfoBuffer[0] = reservoirInfoBuffer[0].getSurfaceObject(0);
        // staticPlp.reservoirInfoBuffer[1] = reservoirInfoBuffer[1].getSurfaceObject(0);
        // printf("sampleVisibilityBuffer \n");
        // staticPlp.sampleVisibilityBuffer[0] = sampleVisibilityBuffer[0].getSurfaceObject(0);
        // staticPlp.sampleVisibilityBuffer[1] = sampleVisibilityBuffer[1].getSurfaceObject(0);
        // printf("spatialNeighborDeltas \n");
        // staticPlp.spatialNeighborDeltas = spatialNeighborDeltas.getDevicePointer();

        staticPlp.beautyAccumBuffer = beautyAccumBuffer.getSurfaceObject(0);
        staticPlp.albedoAccumBuffer = albedoAccumBuffer.getSurfaceObject(0);
        staticPlp.normalAccumBuffer = normalAccumBuffer.getSurfaceObject(0);
    }
    CUdeviceptr staticPlpOnDevice;
    CUDADRV_CHECK(cuMemAlloc(&staticPlpOnDevice, sizeof(staticPlp)));
    CUDADRV_CHECK(cuMemcpyHtoD(staticPlpOnDevice, &staticPlp, sizeof(staticPlp)));

    shared::PerFramePipelineLaunchParameters perFramePlp = {};
    {    
        perFramePlp.travHandle = scene.ias.getHandle();
        perFramePlp.camera.fovY = 50 * pi_v<float> / 180;
        perFramePlp.camera.aspect = static_cast<float>(renderTargetSizeX) / renderTargetSizeY;
        perFramePlp.camera.position = g_cameraPosition;
        perFramePlp.camera.orientation = g_cameraOrientation.toMatrix3x3();
        perFramePlp.prevCamera = perFramePlp.camera;
        perFramePlp.envLightPowerCoeff = 0;
        perFramePlp.envLightRotation = 0;
        perFramePlp.useSeparateNRC = useSeparateNRC;
    }

    CUdeviceptr perFramePlpOnDevice;
    CUDADRV_CHECK(cuMemAlloc(&perFramePlpOnDevice, sizeof(perFramePlp)));
    CUDADRV_CHECK(cuMemcpyHtoD(perFramePlpOnDevice, &perFramePlp, sizeof(perFramePlp)));
    
    shared::PipelineLaunchParameters plp;
    plp.s = reinterpret_cast<shared::StaticPipelineLaunchParameters*>(staticPlpOnDevice);
    plp.f = reinterpret_cast<shared::PerFramePipelineLaunchParameters*>(perFramePlpOnDevice);

    gpuEnv.gBuffer.hitGroupSbt.initialize(
        gpuEnv.cuContext, Scene::bufferType, scene.hitGroupSbtSize, 1);
    gpuEnv.gBuffer.hitGroupSbt.setMappedMemoryPersistent(true);
    gpuEnv.gBuffer.optixPipeline.setScene(scene.optixScene);
    gpuEnv.gBuffer.optixPipeline.setHitGroupShaderBindingTable(
        gpuEnv.gBuffer.hitGroupSbt, gpuEnv.gBuffer.hitGroupSbt.getMappedPointer());

    // gpuEnv.restir.hitGroupSbt.initialize(
    //     gpuEnv.cuContext, Scene::bufferType, scene.hitGroupSbtSize, 1);
    // gpuEnv.restir.hitGroupSbt.setMappedMemoryPersistent(true);
    // gpuEnv.restir.optixPipeline.setScene(scene.optixScene);
    // gpuEnv.restir.optixPipeline.setHitGroupShaderBindingTable(
    //     gpuEnv.restir.hitGroupSbt, gpuEnv.restir.hitGroupSbt.getMappedPointer());

    gpuEnv.pathTracing.hitGroupSbt.initialize(
        gpuEnv.cuContext, Scene::bufferType, scene.hitGroupSbtSize, 1);
    gpuEnv.pathTracing.hitGroupSbt.setMappedMemoryPersistent(true);
    gpuEnv.pathTracing.optixPipeline.setScene(scene.optixScene);
    gpuEnv.pathTracing.optixPipeline.setHitGroupShaderBindingTable(
        gpuEnv.pathTracing.hitGroupSbt, gpuEnv.pathTracing.hitGroupSbt.getMappedPointer());

    shared::PickInfo initPickInfo = {};
    initPickInfo.hit = false;
    initPickInfo.instSlot = 0xFFFFFFFF;
    initPickInfo.geomInstSlot = 0xFFFFFFFF;
    initPickInfo.matSlot = 0xFFFFFFFF;
    initPickInfo.primIndex = 0xFFFFFFFF;
    initPickInfo.positionInWorld = make_float3(0.0f);
    initPickInfo.albedo = make_float3(0.0f);
    initPickInfo.diffuseTexture = make_float3(0.0f);
    initPickInfo.specularTexture = make_float3(0.0f);
    initPickInfo.roughness = 0.f;
    initPickInfo.emittance = make_float3(0.0f);
    initPickInfo.normalInWorld = make_float3(0.0f);
    cudau::TypedBuffer<shared::PickInfo> pickInfos[2];
    pickInfos[0].initialize(gpuEnv.cuContext, Scene::bufferType, 1, initPickInfo);
    pickInfos[1].initialize(gpuEnv.cuContext, Scene::bufferType, 1, initPickInfo);

    CUdeviceptr plpOnDevice;
    CUDADRV_CHECK(cuMemAlloc(&plpOnDevice, sizeof(plp)));



    struct GPUTimer {
        cudau::Timer frame;
        cudau::Timer update;
        cudau::Timer computePDFTexture;
        cudau::Timer setupGBuffers;
        cudau::Timer performInitialAndTemporalRIS;
        cudau::Timer performSpatialRIS;
        cudau::Timer shading;
        cudau::Timer preprocessNRC;
        cudau::Timer pathTrace;
        cudau::Timer infer;
        cudau::Timer accumulateInferredRadiances;
        cudau::Timer propagateRadiances;
        cudau::Timer shuffleTrainingData;
        cudau::Timer train;
        cudau::Timer visualizeCache;
        cudau::Timer denoise;

        void initialize(CUcontext context) {
            frame.initialize(context);
            update.initialize(context);
            computePDFTexture.initialize(context);
            setupGBuffers.initialize(context);
            performInitialAndTemporalRIS.initialize(context);
            performSpatialRIS.initialize(context);
            shading.initialize(context);
            preprocessNRC.initialize(context);
            pathTrace.initialize(context);
            infer.initialize(context);
            accumulateInferredRadiances.initialize(context);
            propagateRadiances.initialize(context);
            shuffleTrainingData.initialize(context);
            train.initialize(context);
            visualizeCache.initialize(context);
            denoise.initialize(context);
        }
        void finalize() {
            denoise.finalize();
            visualizeCache.finalize();
            train.finalize();
            shuffleTrainingData.finalize();
            propagateRadiances.finalize();
            accumulateInferredRadiances.finalize();
            infer.finalize();
            pathTrace.finalize();
            preprocessNRC.finalize();
            setupGBuffers.finalize();
            computePDFTexture.finalize();
            update.finalize();
            frame.finalize();
        }
    };

    std::mt19937 perFrameRng(72139121);

    GPUTimer gpuTimers[2];
    gpuTimers[0].initialize(gpuEnv.cuContext);
    gpuTimers[1].initialize(gpuEnv.cuContext);
    uint64_t frameIndex = 0;
    glfwSetWindowUserPointer(window, &frameIndex);
    int32_t requestedSize[2];
    uint32_t numAccumFrames = 0;
    uint32_t saveFrameID = 0;
    // std::string exp_name = "test";
    std::string expPath = "exp/" + exp_name;
    std::string imgPath = expPath + "/imgs";
    const bool logExp = true;

    // uint32_t lastSpatialNeighborBaseIndex = 0;
    // uint32_t lastReservoirIndex = 1;

    std::filesystem::create_directories(imgPath);
    
    CsvLogger csvLogger = CsvLogger(expPath + "/log.csv");

    // log scene dim
    std::ofstream myfile;
    myfile.open(expPath + "/scene_dim.txt");
    myfile << sceneDim.x << " " << sceneDim.y << " " << sceneDim.z;
    myfile.close();


    while ( (frameStop < 0) || (frameIndex <= frameStop)) {
    // while (true){
        uint32_t bufferIndex = frameIndex % 2;

        GPUTimer &curGPUTimer = gpuTimers[bufferIndex];

        perFramePlp.prevCamera = perFramePlp.camera;

        if (glfwWindowShouldClose(window))
            break;
        glfwPollEvents();

        bool resized = false;
        int32_t newFBWidth;
        int32_t newFBHeight;
        glfwGetFramebufferSize(window, &newFBWidth, &newFBHeight);
        if (newFBWidth != curFBWidth || newFBHeight != curFBHeight) {
            curFBWidth = newFBWidth;
            curFBHeight = newFBHeight;

            // renderTargetSizeX = curFBWidth / UIScaling;
            // renderTargetSizeY = curFBHeight / UIScaling;
            requestedSize[0] = renderTargetSizeX;
            requestedSize[1] = renderTargetSizeY;

            glFinish();
            CUDADRV_CHECK(cuStreamSynchronize(cuStream));

            resizeScreenRelatedBuffers(renderTargetSizeX, renderTargetSizeY);

            {
                optixu::DenoiserSizes denoiserSizes;
                uint32_t numTasks;
                denoiser.prepare(
                    renderTargetSizeX, renderTargetSizeY, tileWidth, tileHeight,
                    &denoiserSizes, &numTasks);
                hpprintf("Denoiser State Buffer: %llu bytes\n", denoiserSizes.stateSize);
                hpprintf("Denoiser Scratch Buffer: %llu bytes\n", denoiserSizes.scratchSize);
                hpprintf("Compute Intensity Scratch Buffer: %llu bytes\n",
                         denoiserSizes.scratchSizeForComputeNormalizer);
                denoiserStateBuffer.resize(denoiserSizes.stateSize, 1);
                denoiserScratchBuffer.resize(std::max(
                    denoiserSizes.scratchSize, denoiserSizes.scratchSizeForComputeNormalizer), 1);

                denoisingTasks.resize(numTasks);
                denoiser.getTasks(denoisingTasks.data());

                denoiser.setupState(cuStream, denoiserStateBuffer, denoiserScratchBuffer);
            }

            outputTexture.finalize();
            outputArray.finalize();
            outputTexture.initialize(GL_RGBA32F, renderTargetSizeX, renderTargetSizeY, 1);
            outputArray.initializeFromGLTexture2D(gpuEnv.cuContext, outputTexture.getHandle(),
                                                  cudau::ArraySurface::Enable, cudau::ArrayTextureGather::Disable);

            // EN: update the pipeline parameters.
            staticPlp.imageSize = int2(renderTargetSizeX, renderTargetSizeY);
            staticPlp.rngBuffer = rngBuffer.getSurfaceObject(0);
            staticPlp.GBuffer0[0] = gBuffer0[0].getSurfaceObject(0);
            staticPlp.GBuffer0[1] = gBuffer0[1].getSurfaceObject(0);
            staticPlp.GBuffer1[0] = gBuffer1[0].getSurfaceObject(0);
            staticPlp.GBuffer1[1] = gBuffer1[1].getSurfaceObject(0);
            staticPlp.GBuffer2[0] = gBuffer2[0].getSurfaceObject(0);
            staticPlp.GBuffer2[1] = gBuffer2[1].getSurfaceObject(0);
            staticPlp.inferenceRadianceQueryBuffer = inferenceRadianceQueryBuffer.getDevicePointer();
            staticPlp.inferenceTerminalInfoBuffer = inferenceTerminalInfoBuffer.getDevicePointer();
            staticPlp.inferredRadianceBuffer = inferredRadianceBuffer.getDevicePointer();
            if (useSeparateNRC)
                staticPlp.inferredRadianceBuffer2 = inferredRadianceBuffer2.getDevicePointer();
            staticPlp.perFrameContributionBuffer = perFrameContributionBuffer.getDevicePointer();
            staticPlp.beautyAccumBuffer = beautyAccumBuffer.getSurfaceObject(0);
            staticPlp.albedoAccumBuffer = albedoAccumBuffer.getSurfaceObject(0);
            staticPlp.normalAccumBuffer = normalAccumBuffer.getSurfaceObject(0);
            perFramePlp.camera.aspect = (float)renderTargetSizeX / renderTargetSizeY;

            // staticPlp.reservoirBuffer[0] = reservoirBuffer[0].getBlockBuffer2D();
            // staticPlp.reservoirBuffer[1] = reservoirBuffer[1].getBlockBuffer2D();
            // staticPlp.reservoirInfoBuffer[0] = reservoirInfoBuffer[0].getSurfaceObject(0);
            // staticPlp.reservoirInfoBuffer[1] = reservoirInfoBuffer[1].getSurfaceObject(0);
            // staticPlp.sampleVisibilityBuffer[0] = sampleVisibilityBuffer[0].getSurfaceObject(0);
            // staticPlp.sampleVisibilityBuffer[1] = sampleVisibilityBuffer[1].getSurfaceObject(0);

            CUDADRV_CHECK(cuMemcpyHtoD(staticPlpOnDevice, &staticPlp, sizeof(staticPlp)));

            resized = true;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();



        bool operatingCamera;
        bool cameraIsActuallyMoving;
        static bool operatedCameraOnPrevFrame = false;
        {
            const auto decideDirection = [](const KeyState& a, const KeyState& b) {
                int32_t dir = 0;
                if (a.getState() == true) {
                    if (b.getState() == true)
                        dir = 0;
                    else
                        dir = 1;
                }
                else {
                    if (b.getState() == true)
                        dir = -1;
                    else
                        dir = 0;
                }
                return dir;
            };

            int32_t trackZ = decideDirection(g_keyForward, g_keyBackward);
            int32_t trackX = decideDirection(g_keyLeftward, g_keyRightward);
            int32_t trackY = decideDirection(g_keyUpward, g_keyDownward);
            if (camMoveX != 0) trackX = camMoveX;
            if (camMoveY != 0) trackY = camMoveY;
            if (camMoveZ != 0) trackZ = camMoveZ;
            int32_t tiltZ = decideDirection(g_keyTiltRight, g_keyTiltLeft);
            int32_t adjustPosMoveSpeed = decideDirection(g_keyFasterPosMovSpeed, g_keySlowerPosMovSpeed);

            g_cameraPositionalMovingSpeed *= 1.0f + 0.02f * adjustPosMoveSpeed;
            g_cameraPositionalMovingSpeed = std::clamp(g_cameraPositionalMovingSpeed, 1e-6f, 1e+6f);

            static double deltaX = 0, deltaY = 0;
            static double lastX, lastY;
            static double g_prevMouseX = g_mouseX, g_prevMouseY = g_mouseY;
            if (g_buttonRotate.getState() == true) {
                if (g_buttonRotate.getTime() == frameIndex) {
                    lastX = g_mouseX;
                    lastY = g_mouseY;
                }
                else {
                    deltaX = g_mouseX - lastX;
                    deltaY = g_mouseY - lastY;
                }
            }

            float deltaAngle = std::sqrt(deltaX * deltaX + deltaY * deltaY);
            float3 axis = float3(deltaY, -deltaX, 0);
            axis /= deltaAngle;
            if (deltaAngle == 0.0f)
                axis = float3(1, 0, 0);

            g_cameraOrientation = g_cameraOrientation * qRotateZ(g_cameraTiltSpeed * tiltZ);
            g_tempCameraOrientation =
                g_cameraOrientation *
                qRotate(g_cameraDirectionalMovingSpeed * deltaAngle, axis);
            g_cameraPosition +=
                g_tempCameraOrientation.toMatrix3x3() *
                (g_cameraPositionalMovingSpeed * float3(trackX, trackY, trackZ));
            if (g_buttonRotate.getState() == false && g_buttonRotate.getTime() == frameIndex) {
                g_cameraOrientation = g_tempCameraOrientation;
                deltaX = 0;
                deltaY = 0;
            }

            operatingCamera = (g_keyForward.getState() || g_keyBackward.getState() ||
                               g_keyLeftward.getState() || g_keyRightward.getState() ||
                               g_keyUpward.getState() || g_keyDownward.getState() ||
                               g_keyTiltLeft.getState() || g_keyTiltRight.getState() ||
                               g_buttonRotate.getState());
            cameraIsActuallyMoving = (trackZ != 0 || trackX != 0 || trackY != 0 ||
                                      tiltZ != 0 || (g_mouseX != g_prevMouseX) || (g_mouseY != g_prevMouseY))
                && operatingCamera;

            g_prevMouseX = g_mouseX;
            g_prevMouseY = g_mouseY;

            perFramePlp.camera.position = g_cameraPosition;
            perFramePlp.camera.orientation = g_tempCameraOrientation.toMatrix3x3();
        }



        bool resetAccumulation = false;
        
        // Camera Window

        static bool applyToneMapAndGammaCorrection = true;
        static float brightness = g_initBrightness;
        static bool enableEnvLight = true;
        static float log10EnvLightPowerCoeff = 0.0f;
        static float envLightRotation = 0.0f;
        {
            ImGui::Begin("Camera / Env", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

            ImGui::Text("W/A/S/D/R/F: Move, Q/E: Tilt");
            ImGui::Text("Mouse Middle Drag: Rotate");

            ImGui::InputFloat3("Position", reinterpret_cast<float*>(&g_cameraPosition));
            static float rollPitchYaw[3];
            g_tempCameraOrientation.toEulerAngles(&rollPitchYaw[0], &rollPitchYaw[1], &rollPitchYaw[2]);
            rollPitchYaw[0] *= 180 / pi_v<float>;
            rollPitchYaw[1] *= 180 / pi_v<float>;
            rollPitchYaw[2] *= 180 / pi_v<float>;
            if (ImGui::InputFloat3("Roll/Pitch/Yaw", rollPitchYaw))
                g_cameraOrientation = qFromEulerAngles(rollPitchYaw[0] * pi_v<float> / 180,
                                                       rollPitchYaw[1] * pi_v<float> / 180,
                                                       rollPitchYaw[2] * pi_v<float> / 180);
            ImGui::Text("Pos. Speed (T/G): %g", g_cameraPositionalMovingSpeed);
            ImGui::SliderFloat("Brightness", &brightness, -5.0f, 5.0f);

            ImGui::AlignTextToFramePadding();
            ImGui::Text("Screen Shot:");
            ImGui::SameLine();
            bool saveSS_LDR = ImGui::Button("SDR");
            ImGui::SameLine();
            bool saveSS_HDR = ImGui::Button("HDR");
            ImGui::SameLine();
            if (ImGui::Button("Both"))
                saveSS_LDR = saveSS_HDR = true;
            if (saveSS_LDR || saveSS_HDR) {
                CUDADRV_CHECK(cuStreamSynchronize(cuStream));
                auto rawImage = new float4[renderTargetSizeX * renderTargetSizeY];
                glGetTextureSubImage(
                    outputTexture.getHandle(), 0,
                    0, 0, 0, renderTargetSizeX, renderTargetSizeY, 1,
                    GL_RGBA, GL_FLOAT, sizeof(float4) * renderTargetSizeX * renderTargetSizeY, rawImage);

                if (saveSS_LDR) {
                    SDRImageSaverConfig config;
                    config.brightnessScale = std::pow(10.0f, brightness);
                    config.applyToneMap = applyToneMapAndGammaCorrection;
                    config.apply_sRGB_gammaCorrection = applyToneMapAndGammaCorrection;
                    saveImage("output.png", renderTargetSizeX, renderTargetSizeY, rawImage,
                              config);
                }
                if (saveSS_HDR)
                    saveImageHDR("output.exr", renderTargetSizeX, renderTargetSizeY,
                                 std::pow(10.0f, brightness), rawImage);
                delete[] rawImage;
            }

            if (!g_envLightTexturePath.empty()) {
                ImGui::Separator();

                resetAccumulation |= ImGui::Checkbox("Enable Env Light", &enableEnvLight);
                resetAccumulation |= ImGui::SliderFloat("Env Power", &log10EnvLightPowerCoeff, -5.0f, 5.0f);
                resetAccumulation |= ImGui::SliderAngle("Env Rotation", &envLightRotation);
            }

            ImGui::End();
        }



        // struct ReSTIRConfigs {
        //     int32_t log2NumCandidateSamples;
        //     bool enableTemporalReuse = true;
        //     bool enableSpatialReuse = true;
        //     int32_t numSpatialReusePasses;
        //     int32_t numSpatialNeighbors;
        //     float spatialNeighborRadius = 20.0f;
        //     float radiusThresholdForSpatialVisReuse = 10.0f;
        //     bool useLowDiscrepancySpatialNeighbors = true;
        //     bool reuseVisibility = true;
        //     bool reuseVisibilityForTemporal = true;
        //     bool reuseVisibilityForSpatiotemporal = false;

        //     ReSTIRConfigs(uint32_t _log2NumCandidateSamples,
        //                   uint32_t _numSpatialReusePasses, uint32_t _numSpatialNeighbors) :
        //         log2NumCandidateSamples(_log2NumCandidateSamples),
        //         numSpatialReusePasses(_numSpatialReusePasses),
        //         numSpatialNeighbors(_numSpatialNeighbors) {}
        // };


        // static ReSTIRConfigs orgRestirBiasedConfigs(5, 2, 5);
        // static ReSTIRConfigs orgRestirUnbiasedConfigs(5, 1, 3);
        // static ReSTIRConfigs rearchRestirBiasedConfigs(5, 1, 1);
        // static ReSTIRConfigs rearchRestirUnbiasedConfigs(5, 1, 1);

        // static ReSTIRConfigs* curRendererConfigs = &orgRestirBiasedConfigs;
        // static float spatialVisibilityReuseRatio = 50.0f;

        static bool useTemporalDenosier = true;
        static float motionVectorScale = -1.0f;
        // static bool animate = /*true*/false;
        static bool animate = true;
        static bool enableAccumulation = /*true*/false;
        static int32_t log2MaxNumAccums = 16;
        static bool enableJittering = false;
        static bool enableBumpMapping = false;
        bool lastFrameWasAnimated = false;
        // static bool infBounces = false;
        static bool infBounces = true;
        
        

        static bool visualizeTrainingPath = false;
        static bool train = true;
        bool stepTrain = false;
        static bool showLossValue = true;
        static float lossValue = 0.0f;
        static float lossValueSpecular = 0.0f;
        static bool prevTrainDone = false;
        static bool debugSwitches[] = {
            false, false, false, false, false, false, false, false
        };
        {
            ImGui::Begin("Debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

            if (ImGui::Button(animate ? "Stop" : "Play")) {
                if (animate)
                    lastFrameWasAnimated = true;
                animate = !animate;
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset Accum"))
                resetAccumulation = true;
            ImGui::Checkbox("Enable Accumulation", &enableAccumulation);
            ImGui::InputLog2Int("#MaxNumAccum", &log2MaxNumAccums, 16, 5);
            resetAccumulation |= ImGui::Checkbox("Enable Jittering", &enableJittering);
            resetAccumulation |= ImGui::Checkbox("Enable Bump Mapping", &enableBumpMapping);

            ImGui::Separator();
            ImGui::Text("Cursor Info: %.1lf, %.1lf", g_mouseX, g_mouseY);
            shared::PickInfo pickInfoOnHost;
            pickInfos[bufferIndex].read(&pickInfoOnHost, 1, cuStream);
            ImGui::Text("Hit: %s", pickInfoOnHost.hit ? "True" : "False");
            ImGui::Text("Instance: %u", pickInfoOnHost.instSlot);
            ImGui::Text("Geometry Instance: %u", pickInfoOnHost.geomInstSlot);
            ImGui::Text("Primitive Index: %u", pickInfoOnHost.primIndex);
            ImGui::Text("Material: %u", pickInfoOnHost.matSlot);
            ImGui::Text("Position: %.3f, %.3f, %.3f",
                        pickInfoOnHost.positionInWorld.x,
                        pickInfoOnHost.positionInWorld.y,
                        pickInfoOnHost.positionInWorld.z);
            ImGui::Text("Normal: %.3f, %.3f, %.3f",
                        pickInfoOnHost.normalInWorld.x,
                        pickInfoOnHost.normalInWorld.y,
                        pickInfoOnHost.normalInWorld.z);
            ImGui::Text("Albedo: %.3f, %.3f, %.3f",
                        pickInfoOnHost.albedo.x,
                        pickInfoOnHost.albedo.y,
                        pickInfoOnHost.albedo.z);
            ImGui::Text("Diffuse: %.3f, %.3f, %.3f",
                        pickInfoOnHost.diffuseTexture.x,
                        pickInfoOnHost.diffuseTexture.y,
                        pickInfoOnHost.diffuseTexture.z);
            ImGui::Text("Specular: %.3f, %.3f, %.3f",
                        pickInfoOnHost.specularTexture.x,
                        pickInfoOnHost.specularTexture.y,
                        pickInfoOnHost.specularTexture.z);
            ImGui::Text("Roughness: %.3f",
                        pickInfoOnHost.roughness);
            ImGui::Text("Emittance: %.3f, %.3f, %.3f",
                        pickInfoOnHost.emittance.x,
                        pickInfoOnHost.emittance.y,
                        pickInfoOnHost.emittance.z);

            ImGui::Separator();

            uint32_t numTrainingDataOnHost = 0;
            uint2 tileSizeOnHost = uint2(0, 0);
            float3AsOrderedInt targetMinMaxAsOrderedIntOnHost[2];
            float3 targetAvgOnHost;
            if (useNRC) {
                numTrainingData[bufferIndex].read(&numTrainingDataOnHost, 1, cuStream);
                tileSize[bufferIndex].read(&tileSizeOnHost, 1, cuStream);
                targetMinMax[bufferIndex].read(targetMinMaxAsOrderedIntOnHost, 2, cuStream);
                targetAvg[bufferIndex].read(&targetAvgOnHost, 1, cuStream);
            }
            float3 targetMinMaxOnHost[2] = {
                static_cast<float3>(targetMinMaxAsOrderedIntOnHost[0]),
                static_cast<float3>(targetMinMaxAsOrderedIntOnHost[1])
            };
            ImGui::Text("#Training Data: %6u", numTrainingDataOnHost);
            ImGui::Text("Tile Size: %2u x %2u", tileSizeOnHost.x, tileSizeOnHost.y);
            ImGui::Text("Target Range:");
            ImGui::Text("  Min: %10.4f, %10.4f, %10.4f",
                        targetMinMaxOnHost[0].x, targetMinMaxOnHost[0].y, targetMinMaxOnHost[0].z);
            ImGui::Text("  Max: %10.4f, %10.4f, %10.4f",
                        targetMinMaxOnHost[1].x, targetMinMaxOnHost[1].y, targetMinMaxOnHost[1].z);
            ImGui::Text("  Avg: %10.4f, %10.4f, %10.4f",
                        targetAvgOnHost.x, targetAvgOnHost.y, targetAvgOnHost.z);
            bool prevShowLossValue = showLossValue;
            ImGui::Checkbox("Show Loss", &showLossValue);
            if (showLossValue && prevShowLossValue) {
                static float log10Losses[100] = {};
                static uint32_t startOffset = 0;
                if (prevTrainDone) {
                    log10Losses[startOffset] = std::log10(lossValue);
                    startOffset = (startOffset + 1) % IM_ARRAYSIZE(log10Losses);
                }

                char overlayText[100];
                sprintf_s(overlayText, "%.6f", lossValue);

                ImGui::PlotLines(
                    "Loss", log10Losses, IM_ARRAYSIZE(log10Losses), startOffset,
                    overlayText, -3.0f, 3.0f, ImVec2(0, 80.0f));
            }

            ImGui::Separator();

            if (ImGui::BeginTabBar("MyTabBar")) {
                if (ImGui::BeginTabItem("Renderer")) {
                    resetAccumulation |= ImGui::Checkbox("Infinite-Bounce", &infBounces);
                    if (!infBounces)
                        resetAccumulation |= ImGui::SliderInt("Max Path Length", &maxPathLength, 2, 15);

                    bool tempUseNRC = useNRC;
                    if (ImGui::RadioButton("Baseline Path Tracing", !useNRC)) {
                        useNRC = false;
                        if (bufferTypeToDisplay == shared::BufferToDisplay::RenderingPathLength ||
                            bufferTypeToDisplay == shared::BufferToDisplay::NRCOnly || 
                            bufferTypeToDisplay == shared::BufferToDisplay::NRCOnlyEmissive ||
                            bufferTypeToDisplay == shared::BufferToDisplay::NRCOnlyRaw ||
                            bufferTypeToDisplay == shared::BufferToDisplay::SeparateNRCDiffuse ||
                            bufferTypeToDisplay == shared::BufferToDisplay::SeparateNRCSpecular)
                            bufferTypeToDisplay = shared::BufferToDisplay::NoisyBeauty;
                    }
                    if (ImGui::RadioButton("Path Tracing + NRC", useNRC))
                        useNRC = true;
                    resetAccumulation |= useNRC != tempUseNRC;

                    if (useNRC) {
                        if (ImGui::Button(train ? "Stop Training" : "Start Training"))
                            train = !train;
                        ImGui::SameLine();
                        if (ImGui::Button("Step")) {
                            train = false;
                            stepTrain = true;
                        }
                        ImGui::SameLine();
                        bool resetNN = ImGui::Button("Reset");

                        ImGui::Text("Radiance Scale (Log10): %.2e", std::pow(10.0f, log10RadianceScale));
                        resetAccumulation |= ImGui::SliderFloat(
                            "##RadianceScale", &log10RadianceScale, -5, 5, "%.3f", ImGuiSliderFlags_AlwaysClamp);

                        PositionEncoding prevEncoding = g_positionEncoding;
                        ImGui::Text("Position Encoding");
                        ImGui::RadioButtonE("Triangle Wave", &g_positionEncoding, PositionEncoding::TriangleWave);
                        ImGui::RadioButtonE("Hash Grid", &g_positionEncoding, PositionEncoding::HashGrid);

                        ImGui::Text("MLP Num Hidden Layers");
                        uint32_t prevNumHiddenLayers = g_numHiddenLayers;

                        float prevLearningRate = g_learningRate;
                        {
                            static int32_t log10LearningRate =
                                static_cast<int32_t>(std::round(std::log10(g_learningRate)));
                            ImGui::Text("Init Learning Rate: %.0e", g_learningRate);
                            ImGui::SliderInt("##InitLearningRate", &log10LearningRate, -5, -1,
                                             "", ImGuiSliderFlags_AlwaysClamp);
                            g_learningRate = std::pow(10, log10LearningRate);
                        }

                        resetNN |=
                            g_positionEncoding != prevEncoding ||
                            g_numHiddenLayers != prevNumHiddenLayers ||
                            g_learningRate != prevLearningRate;
                        if (resetNN) {
                            neuralRadianceCache.finalize();
                            NRCConfig.posEnc = g_positionEncoding;
                            NRCConfig.numHiddenLayers = g_numHiddenLayers;
                            NRCConfig.learningRate = g_learningRate;
                            std::cout << NRCConfig.numHiddenLayers << "\n";
                            std::cout << NRCConfig.hiddenLayerWidth << "\n";
                            neuralRadianceCache.initialize(NRCConfig);
                            resetAccumulation = true;
                        }
                    }

                    ImGui::PushID("Debug Switches");
                    for (int i = lengthof(debugSwitches) - 1; i >= 0; --i) {
                        ImGui::PushID(i);
                        resetAccumulation |= ImGui::Checkbox("", &debugSwitches[i]);
                        ImGui::PopID();
                        if (i > 0)
                            ImGui::SameLine();
                    }
                    ImGui::PopID();

                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Visualize")) {
                    ImGui::Checkbox("Visualize Training Paths", &visualizeTrainingPath);
                    ImGui::Text("Buffer to Display");
                    ImGui::RadioButtonE(
                        "Noisy Beauty", &bufferTypeToDisplay, shared::BufferToDisplay::NoisyBeauty);
                    ImGui::RadioButtonE("Albedo", &bufferTypeToDisplay, shared::BufferToDisplay::Albedo);
                    ImGui::RadioButtonE("Normal", &bufferTypeToDisplay, shared::BufferToDisplay::Normal);
                    ImGui::RadioButtonE("Motion Vector", &bufferTypeToDisplay, shared::BufferToDisplay::Flow);
                    if (useNRC) {
                        ImGui::RadioButtonE("Rendering Path Length", &bufferTypeToDisplay, shared::BufferToDisplay::RenderingPathLength);
                        ImGui::RadioButtonE("Directly Visualized Prediction", &bufferTypeToDisplay, shared::BufferToDisplay::NRCOnly);
                        ImGui::RadioButtonE("NRC Only Emissive", &bufferTypeToDisplay, shared::BufferToDisplay::NRCOnlyEmissive);
                        ImGui::RadioButtonE("NRC Only Raw", &bufferTypeToDisplay, shared::BufferToDisplay::NRCOnlyRaw);
                        ImGui::RadioButtonE("Separate NRC Only Diffuse", &bufferTypeToDisplay, shared::BufferToDisplay::SeparateNRCDiffuse);
                        ImGui::RadioButtonE("Separate NRC Only Specular", &bufferTypeToDisplay, shared::BufferToDisplay::SeparateNRCSpecular);
                    }
                    ImGui::RadioButtonE(
                        "Denoised Beauty", &bufferTypeToDisplay, shared::BufferToDisplay::DenoisedBeauty);

                    if (ImGui::Checkbox("Temporal Denoiser", &useTemporalDenosier)) {
                        CUDADRV_CHECK(cuStreamSynchronize(cuStream));
                        denoiser.destroy();

                        OptixDenoiserModelKind modelKind = useTemporalDenosier ?
                            OPTIX_DENOISER_MODEL_KIND_TEMPORAL :
                            OPTIX_DENOISER_MODEL_KIND_HDR;
                        denoiser = gpuEnv.optixContext.createDenoiser(modelKind, true, true);

                        optixu::DenoiserSizes denoiserSizes;
                        uint32_t numTasks;
                        denoiser.prepare(
                            renderTargetSizeX, renderTargetSizeY, tileWidth, tileHeight,
                            &denoiserSizes, &numTasks);
                        hpprintf("Denoiser State Buffer: %llu bytes\n", denoiserSizes.stateSize);
                        hpprintf("Denoiser Scratch Buffer: %llu bytes\n", denoiserSizes.scratchSize);
                        hpprintf("Compute Intensity Scratch Buffer: %llu bytes\n",
                                 denoiserSizes.scratchSizeForComputeNormalizer);
                        denoiserStateBuffer.resize(denoiserSizes.stateSize, 1);
                        denoiserScratchBuffer.resize(std::max(
                            denoiserSizes.scratchSize, denoiserSizes.scratchSizeForComputeNormalizer), 1);

                        denoisingTasks.resize(numTasks);
                        denoiser.getTasks(denoisingTasks.data());

                        denoiser.setupState(cuStream, denoiserStateBuffer, denoiserScratchBuffer);
                    }

                    ImGui::SliderFloat("Motion Vector Scale", &motionVectorScale, -2.0f, 2.0f);

                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }

            ImGui::Separator();

            ImGui::End();
        }

        // Stats Window
        {
            ImGui::Begin("Stats", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

#if !defined(USE_HARD_CODED_BSDF_FUNCTIONS)
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 300);
            ImGui::TextColored(
                ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
                "BSDF callables are enabled.\n"
                "USE_HARD_CODED_BSDF_FUNCTIONS is recommended for better performance.");
            ImGui::PopTextWrapPos();
#endif

            static MovingAverageTime cudaFrameTime;
            static MovingAverageTime updateTime;
            static MovingAverageTime computePDFTextureTime;
            static MovingAverageTime setupGBuffersTime;
            static MovingAverageTime preprocessNRCTime;
            static MovingAverageTime pathTraceTime;
            static MovingAverageTime inferTime;
            static MovingAverageTime accumulateInferredRadiancesTime;
            static MovingAverageTime propagateRadiancesTime;
            static MovingAverageTime shuffleTrainingDataTime;
            static MovingAverageTime trainTime;
            static MovingAverageTime visualizeCacheTime;
            static MovingAverageTime denoiseTime;

            cudaFrameTime.append(curGPUTimer.frame.report());
            updateTime.append(curGPUTimer.update.report());
            computePDFTextureTime.append(curGPUTimer.computePDFTexture.report());
            setupGBuffersTime.append(curGPUTimer.setupGBuffers.report());
            preprocessNRCTime.append(curGPUTimer.preprocessNRC.report());
            pathTraceTime.append(curGPUTimer.pathTrace.report());
            inferTime.append(curGPUTimer.infer.report());
            accumulateInferredRadiancesTime.append(curGPUTimer.accumulateInferredRadiances.report());
            propagateRadiancesTime.append(curGPUTimer.propagateRadiances.report());
            shuffleTrainingDataTime.append(curGPUTimer.shuffleTrainingData.report());
            trainTime.append(curGPUTimer.train.report());
            visualizeCacheTime.append(curGPUTimer.visualizeCache.report());
            denoiseTime.append(curGPUTimer.denoise.report());

            //ImGui::SetNextItemWidth(100.0f);
            ImGui::Text("CUDA/OptiX GPU %.3f [ms]:", cudaFrameTime.getAverage());
            ImGui::Text("  update: %.3f [ms]", updateTime.getAverage());
            ImGui::Text("  compute PDF Texture: %.3f [ms]", computePDFTextureTime.getAverage());
            ImGui::Text("  setup G-buffers: %.3f [ms]", setupGBuffersTime.getAverage());
            ImGui::Text("  pre-process NRC: %.3f [ms]", preprocessNRCTime.getAverage());
            ImGui::Text("  pathTrace: %.3f [ms]", pathTraceTime.getAverage());
            ImGui::Text("  inference: %.3f [ms]", inferTime.getAverage());
            ImGui::Text("  accum radiance: %.3f [ms]", accumulateInferredRadiancesTime.getAverage());
            ImGui::Text("  prop radiance: %.3f [ms]", propagateRadiancesTime.getAverage());
            ImGui::Text("  shuffle train data: %.3f [ms]", shuffleTrainingDataTime.getAverage());
            ImGui::Text("  training: %.3f [ms]", trainTime.getAverage());
            if (bufferTypeToDisplay == shared::BufferToDisplay::NRCOnly)
                ImGui::Text("  visualize cache: %.3f [ms]", visualizeCacheTime.getAverage());
            if (bufferTypeToDisplay == shared::BufferToDisplay::DenoisedBeauty)
                ImGui::Text("  denoise: %.3f [ms]", denoiseTime.getAverage());

            ImGui::Text("%u [spp]", std::min(numAccumFrames + 1, (1u << log2MaxNumAccums)));

            ImGui::End();
        }

        applyToneMapAndGammaCorrection =
            bufferTypeToDisplay == shared::BufferToDisplay::NoisyBeauty ||
            bufferTypeToDisplay == shared::BufferToDisplay::NRCOnly ||
            bufferTypeToDisplay == shared::BufferToDisplay::NRCOnlyEmissive ||
            bufferTypeToDisplay == shared::BufferToDisplay::NRCOnlyRaw ||
            bufferTypeToDisplay == shared::BufferToDisplay::SeparateNRCDiffuse ||
            bufferTypeToDisplay == shared::BufferToDisplay::SeparateNRCSpecular ||
            bufferTypeToDisplay == shared::BufferToDisplay::DenoisedBeauty;



        curGPUTimer.frame.start(cuStream);

        // JP: 各インスタンスのトランスフォームを更新する。
        // EN: Update the transform of each instance.
        if (animate || lastFrameWasAnimated) {
            cudau::TypedBuffer<shared::InstanceData> &curInstDataBuffer = scene.instDataBuffer[bufferIndex];
            shared::InstanceData* instDataBufferOnHost = curInstDataBuffer.map();
            for (int i = 0; i < scene.instControllers.size(); ++i) {
                InstanceController* controller = scene.instControllers[i];
                Instance* inst = controller->inst;
                shared::InstanceData &instData = instDataBufferOnHost[inst->instSlot];
                controller->update(instDataBufferOnHost, animate ? 1.0f / 60.0f : 0.0f);
                // TODO: まとめて送る。
                CUDADRV_CHECK(cuMemcpyHtoDAsync(curInstDataBuffer.getCUdeviceptrAt(inst->instSlot),
                                                &instData, sizeof(instData), cuStream));
                currentDyObjMat = inst->matM2W;

                transMat = currentDyObjMat.inverse() * initDyObjMat;
                CUDADRV_CHECK(cuMemcpyHtoD(transMatDevicePtr, &transMat, sizeof(transMat)));
                // std::cout << "Insta: " << i << ", trans: [" << inst->matM2W.m03 << "," << inst->matM2W.m13 << "," << inst->matM2W.m23 << "]\n";
            }
            curInstDataBuffer.unmap();
        }

        // JP: IASのリビルドを行う。
        //     アップデートの代用としてのリビルドでは、インスタンスの追加・削除や
        //     ASビルド設定の変更を行っていないのでmarkDirty()やprepareForBuild()は必要無い。
        // EN: Rebuild the IAS.
        //     Rebuild as the alternative for update doesn't involves
        //     add/remove of instances and changes of AS build settings
        //     so neither of markDirty() nor prepareForBuild() is required.
        curGPUTimer.update.start(cuStream);
        if (animate)
            perFramePlp.travHandle = scene.ias.rebuild(
                cuStream, scene.iasInstanceBuffer, scene.iasMem, scene.asScratchMem);
        curGPUTimer.update.stop(cuStream);

        // JP: 光源となるインスタンスのProbability Textureを計算する。
        // EN: Compute the probability texture for light instances.
        curGPUTimer.computePDFTexture.start(cuStream);
        {
            CUdeviceptr probTexAddr =
                staticPlpOnDevice + offsetof(shared::StaticPipelineLaunchParameters, lightInstDist);
            scene.setupLightInstDistribtion(cuStream, probTexAddr, bufferIndex);
        }
        curGPUTimer.computePDFTexture.stop(cuStream);

        // bool newSequence = resized || frameIndex == 0 || resetAccumulation;
        bool newSequence = frameIndex == 0;
        bool firstAccumFrame =
            animate || !enableAccumulation || cameraIsActuallyMoving || newSequence;
        if (firstAccumFrame)
            numAccumFrames = 0;
        else
            numAccumFrames = std::min(numAccumFrames + 1, (1u << log2MaxNumAccums));
        if (newSequence)
            hpprintf("New sequence started.\n");

        perFramePlp.numAccumFrames = numAccumFrames;
        perFramePlp.frameIndex = frameIndex;
        perFramePlp.instanceDataBuffer = scene.instDataBuffer[bufferIndex].getDevicePointer();
        perFramePlp.radianceScale = std::pow(10.0f, log10RadianceScale);
        perFramePlp.envLightPowerCoeff = std::pow(10.0f, log10EnvLightPowerCoeff);
        perFramePlp.envLightRotation = envLightRotation;
        perFramePlp.mousePosition = int2(static_cast<int32_t>(g_mouseX),
                                         static_cast<int32_t>(g_mouseY));
        perFramePlp.pickInfo = pickInfos[bufferIndex].getDevicePointer();

        perFramePlp.maxPathLength = infBounces ? 0 : maxPathLength;
        perFramePlp.bufferIndex = bufferIndex;
        perFramePlp.resetFlowBuffer = newSequence;
        perFramePlp.enableJittering = enableJittering;
        perFramePlp.enableEnvLight = enableEnvLight;
        perFramePlp.enableBumpMapping = enableBumpMapping;
        for (int i = 0; i < lengthof(debugSwitches); ++i)
            perFramePlp.setDebugSwitch(i, debugSwitches[i]);

        CUDADRV_CHECK(cuMemcpyHtoDAsync(perFramePlpOnDevice, &perFramePlp, sizeof(perFramePlp), cuStream));

        CUDADRV_CHECK(cuMemcpyHtoDAsync(plpOnDevice, &plp, sizeof(plp), cuStream));
        CUDADRV_CHECK(cuMemcpyHtoDAsync(gpuEnv.plpPtr, &plp, sizeof(plp), cuStream));
        CUDADRV_CHECK(cuMemcpyHtoDAsync(plpPtrOnDeviceForCopyBuffers, &plp, sizeof(plp), cuStream));

  
        // uint32_t currentReservoirIndex = (lastReservoirIndex + 1) % 2;
        // //hpprintf("%u\n", currentReservoirIndex);
        // plp.currentReservoirIndex = currentReservoirIndex;
        // plp.spatialNeighborBaseIndex = lastSpatialNeighborBaseIndex;
        // CUDADRV_CHECK(cuMemcpyHtoDAsync(plpOnDevice, &plp, sizeof(plp), cuStream));
        // CUDADRV_CHECK(cuMemcpyHtoDAsync(gpuEnv.plpPtr, &plp, sizeof(plp), cuStream));

        nlohmann::json framePyCache;

        framePyCache["frame_id"] = frameIndex;
        // framePyCache["spp_id"] = sppId;

        for (int sppId = 0; sppId < SPP; ++sppId)    
        {           
            



             // JP: Gバッファーのセットアップ。
            //     ここではレイトレースを使ってGバッファーを生成しているがもちろんラスタライザーで生成可能。
            // EN: Setup the G-buffers.
            //     Generate the G-buffers using ray trace here, but of course this can be done using rasterizer.
            curGPUTimer.setupGBuffers.start(cuStream);
            gpuEnv.gBuffer.optixPipeline.launch(
                cuStream, plpOnDevice, renderTargetSizeX, renderTargetSizeY, 1);
            curGPUTimer.setupGBuffers.stop(cuStream);


            // // std::cout << "restir running\n";
            // {
            //     curGPUTimer.performInitialAndTemporalRIS.start(cuStream);
            //     PathTracingEntryPoint entryPoint = PathTracingEntryPoint::performInitialRIS;
            //     if (curRendererConfigs->enableTemporalReuse && !newSequence) {
            //         entryPoint = curRenderer == ReSTIRRenderer::OriginalReSTIRUnbiased ?
            //             PathTracingEntryPoint::performInitialAndTemporalRISUnbiased :
            //             PathTracingEntryPoint::performInitialAndTemporalRISBiased;

            //     }
            //     // std::cout << "OriginalReSTIRUnbiased: " << (curRenderer == Renderer::OriginalReSTIRUnbiased) << "\n";


            //     // gpuEnv.pathTracing.optixPipeline.link(1, DEBUG_SELECT(OPTIX_COMPILE_DEBUG_LEVEL_FULL, OPTIX_COMPILE_DEBUG_LEVEL_NONE));
            //     gpuEnv.pathTracing.setEntryPoint(entryPoint);
            //     gpuEnv.pathTracing.optixPipeline.launch(
            //         cuStream, plpOnDevice, renderTargetSizeX, renderTargetSizeY, 1);
            //     curGPUTimer.performInitialAndTemporalRIS.stop(cuStream);

            //     // JP: 各ピクセルにおいて(空間的)隣接ピクセルとの間でReservoirの結合を行う。
            //     // EN: For each pixel, combine reservoirs between the current pixel and
            //     //     (Spatially) neighboring pixels.
            //     curGPUTimer.performSpatialRIS.start(cuStream);
            //     if (curRendererConfigs->enableSpatialReuse) {
            //         int32_t numSpatialReusePasses;
            //         PathTracingEntryPoint entryPoint = curRenderer == ReSTIRRenderer::OriginalReSTIRUnbiased ?
            //             PathTracingEntryPoint::performSpatialRISUnbiased :
            //             PathTracingEntryPoint::performSpatialRISBiased;
            //         gpuEnv.pathTracing.setEntryPoint(entryPoint);
            //         numSpatialReusePasses = curRendererConfigs->numSpatialReusePasses;

            //         for (int i = 0; i < numSpatialReusePasses; ++i) {
            //             uint32_t baseIndex =
            //                 lastSpatialNeighborBaseIndex + curRendererConfigs->numSpatialNeighbors * i;
            //             plp.spatialNeighborBaseIndex = baseIndex;
            //             CUDADRV_CHECK(cuMemcpyHtoDAsync(plpOnDevice, &plp, sizeof(plp), cuStream));
            //             gpuEnv.pathTracing.optixPipeline.launch(
            //                 cuStream, plpOnDevice, renderTargetSizeX, renderTargetSizeY, 1);
            //             currentReservoirIndex = (currentReservoirIndex + 1) % 2;
            //             plp.currentReservoirIndex = currentReservoirIndex;
            //         }
            //         lastSpatialNeighborBaseIndex += curRendererConfigs->numSpatialNeighbors * numSpatialReusePasses;
            //     }
            //     curGPUTimer.performSpatialRIS.stop(cuStream);

            //     lastReservoirIndex = currentReservoirIndex;
            // }
            // -----------------------------------------------------------------

            // for (int i=0; i<2; ++i) tileSize[i].read(&tileSizeDebug, 1, cuStream);
            // printf("1.tile size: [%d, %d]\n", tileSizeDebug.x, tileSizeDebug.y);

            // JP: タイルサイズのアップデートやTraining Suffixの終端情報初期化などを行う。
            // EN: Perform update of the tile size and initialization of training suffixes and so on.

            // std::cout << "nrc running\n";
            if (useNRC) {
                curGPUTimer.preprocessNRC.start(cuStream);
                gpuEnv.kernelPreprocessNRC.launchWithThreadDim(
                    cuStream, cudau::dim3(maxNumTrainingSuffixes),
                    perFrameRng(), perFrameRng(), newSequence);
                curGPUTimer.preprocessNRC.stop(cuStream);
            }
            // for (int i=0; i<2; ++i) tileSize[i].read(&tileSizeDebug, 1, cuStream);
            // printf("2.tile size: [%d, %d]\n", tileSizeDebug.x, tileSizeDebug.y);

            // JP: パストレースを行い、Rendering Pathと訓練データの生成を行う。
            // EN: Path trace to generate rendering paths and training data.
            {
                curGPUTimer.pathTrace.start(cuStream);
                CUDADRV_CHECK(cuMemcpyHtoDAsync(plpOnDevice, &plp, sizeof(plp), cuStream));
                PathTracingEntryPoint entryPoint = useNRC ?
                    PathTracingEntryPoint::NRC : PathTracingEntryPoint::Baseline;
                gpuEnv.pathTracing.setEntryPoint(entryPoint);
                // std::cout << "nrc launch\n";
                gpuEnv.pathTracing.optixPipeline.launch(
                    cuStream, plpOnDevice, renderTargetSizeX, renderTargetSizeY, 1);
                curGPUTimer.pathTrace.stop(cuStream);
            }
            if (useNRC) {
                // JP: CUDAではdispatchIndirectのような動的なディスパッチサイズの指定が
                //     サポートされていないので仕方なくGPUと同期して訓練データ数などを取得する。
                //     実際のリアルタイムレンダリング実装の場合は動的なディスパッチサイズ指定を行う必要がある。
                // EN: CUDA does not support dynamic dispatch size like dispatchIndirect,
                //     so it has no choice but to synchronize with the GPU to obtain the number of training data and so on.
                //     Practical real-time rendering implementation requires dynamic dispatch size specification.
                uint32_t numTrainingDataOnHost;
                uint2 tileSizeOnHost;
                numTrainingData[bufferIndex].read(&numTrainingDataOnHost, 1, cuStream);
                tileSize[bufferIndex].read(&tileSizeOnHost, 1, cuStream);
                uint2 numTiles = (uint2(renderTargetSizeX, renderTargetSizeY) + tileSizeOnHost - 1) / tileSizeOnHost;
                uint32_t numInferenceQueries = renderTargetSizeX * renderTargetSizeY + numTiles.x * numTiles.y;
                numInferenceQueries = (numInferenceQueries + 127) / 128 * 128; // round to multiplies of 128

                // JP: Rendering PathとTraining Suffixの終端の輝度を推定する。
                // EN: Predict radiance values at the terminals of rendering paths and training suffixes.
                curGPUTimer.infer.start(cuStream);
                // TODO support pytorch



                // shared::RadianceQuery LogQuery[3];
                // inferenceRadianceQueryBuffer.read(LogQuery, 3, cuStream);

                // for (int pi = 0; pi < 3; pi++){

                //     std::cout << "position: " << LogQuery[pi].position.x << ", " \
                //     << LogQuery[pi].position.y << ", " \
                //     << LogQuery[pi].position.z << "\n";
                //     std::cout << "motion: " << LogQuery[pi].motion.x << ", " \
                //     << LogQuery[pi].motion.y << "\n";
                //     std::cout << "normal_phi: " << LogQuery[pi].normal_phi << "\n";
                //     std::cout << "normal_theta: " << LogQuery[pi].normal_theta << "\n";
                //     std::cout << "vOut_phi: " << LogQuery[pi].vOut_phi << "\n";
                //     std::cout << "vOut_theta: " << LogQuery[pi].vOut_theta << "\n";
                //     std::cout << "roughness: " << LogQuery[pi].roughness << "\n";
                //     std::cout << "diffuseReflectance: " << LogQuery[pi].diffuseReflectance.x << ", " \
                //     << LogQuery[pi].diffuseReflectance.y << ", " \
                //     << LogQuery[pi].diffuseReflectance.z << "\n";
                //     std::cout << "specularReflectance: " << LogQuery[pi].specularReflectance.x << ", " \
                //     << LogQuery[pi].specularReflectance.y << ", " \
                //     << LogQuery[pi].specularReflectance.z << "\n";
                //     std::cout << "#######\n"; 

                // }

                // float* queryfloat = reinterpret_cast<float*>(LogQuery);
                // for (int pi = 0; pi < 17 * 3; pi++){
                //     std::cout << queryfloat[pi] << ", ";
                // }

                // std::cout << "\n"; 
                // std::cout << "<<<<<<<<<<<<<<<<<<<<\n"; 


                if (warpDyCoord) {
                    gpuEnv.kernelWarpDyCoords.launchWithThreadDim(
                        cuStream, cudau::dim3(numInferenceQueries),
                        inferenceRadianceQueryBuffer.getDevicePointer(),
                        transMatDevicePtr
                        );
                }

                // neuralRadianceCache.infer(
                //     cuStream,
                //     reinterpret_cast<float*>(inferenceRadianceQueryBuffer.getDevicePointer()),
                //     numInferenceQueries,
                //     reinterpret_cast<float*>(inferredRadianceBuffer.getDevicePointer()));
                // if (useSeparateNRC)
                //     neuralRadianceCacheSpecular.infer(
                //         cuStream,
                //         reinterpret_cast<float*>(inferenceRadianceQueryBuffer.getDevicePointer()),
                //         numInferenceQueries,
                //         reinterpret_cast<float*>(inferredRadianceBuffer2.getDevicePointer()));
                // curGPUTimer.infer.stop(cuStream);


                // if (savePyCache){
                //     for (int batchID=0; batchID < numInferenceQueries; ++batchID){
                //         shared::RadianceQuery queryBufferHost;
                //         CUDADRV_CHECK(cuMemcpyDtoHAsync(&queryBufferHost, inferenceRadianceQueryBuffer.getCUdeviceptrAt(batchID),
                //                         sizeof(queryBufferHost), cuStream));

                //         framePyCache["pre_train_infer"][batchID] = logQueryToJson(queryBufferHost);
                //     }
                    
                //     for (int batchID=0; batchID < maxNumTrainingSuffixes; ++batchID){
                //         // also cache terminal info
                //         shared::TrainingSuffixTerminalInfo terminalInfoHost;
                //         CUDADRV_CHECK(cuMemcpyDtoHAsync(&terminalInfoHost, trainSuffixTerminalInfoBuffer.getCUdeviceptrAt(batchID),
                //                         sizeof(terminalInfoHost), cuStream));
                //         framePyCache["pre_train_infer"][batchID]["prev_vertex_data_index"] = (long int)terminalInfoHost.prevVertexDataIndex;
                //         framePyCache["pre_train_infer"][batchID]["has_query"] = (int)terminalInfoHost.hasQuery;

                //     }


                //     for (int batchID=0; batchID < shared::trainBufferSize; ++batchID){
                //         shared::TrainingVertexInfo trainVertexInfoHost;
                //         CUDADRV_CHECK(cuMemcpyDtoHAsync(&trainVertexInfoHost, trainVertexInfoBuffer.getCUdeviceptrAt(batchID),
                //                         sizeof(trainVertexInfoHost), cuStream));
                //         framePyCache["train_vertex"][batchID]["prev_vertex_data_index"] = (long int)trainVertexInfoHost.prevVertexDataIndex;
                //         framePyCache["train_vertex"][batchID]["local_throughput"] = {trainVertexInfoHost.localThroughput.x, trainVertexInfoHost.localThroughput.y, trainVertexInfoHost.localThroughput.z};

                //         float3 trainTargetBufferHost;
                //         CUDADRV_CHECK(cuMemcpyDtoHAsync(&trainTargetBufferHost, trainTargetBuffer[0].getCUdeviceptrAt(batchID),
                //                         sizeof(trainTargetBufferHost), cuStream));
                //         framePyCache["train_vertex"][batchID]["target_train_buffer"] = {trainTargetBufferHost.x, trainTargetBufferHost.y, trainTargetBufferHost.z};
                //     }
                
                // }

                // JP: 各ピクセルに推定した輝度を加算して現在のフレームを完成させる。
                // EN: Accumulate the predicted radiance values to the pixels to complete the current frame.
                curGPUTimer.accumulateInferredRadiances.start(cuStream);
                gpuEnv.kernelAccumulateInferredRadianceValues(
                    cuStream,
                    gpuEnv.kernelAccumulateInferredRadianceValues.calcGridDim(renderTargetSizeX * renderTargetSizeY));
                curGPUTimer.accumulateInferredRadiances.stop(cuStream);

                prevTrainDone = false;
                if (train || stepTrain) {
                    prevTrainDone = true;

                    // JP: Training Suffixの終端から輝度を伝播させてTraining Vertexのデータを完成させる。
                    // EN: Propagate the radiance values from the terminals of training suffixes to
                    //     complete training vertex data.
                    curGPUTimer.propagateRadiances.start(cuStream);
                    gpuEnv.kernelPropagateRadianceValues.launchWithThreadDim(
                        cuStream, cudau::dim3(maxNumTrainingSuffixes));
                    curGPUTimer.propagateRadiances.stop(cuStream);

                    // JP: 訓練データの空間的な相関を取り除くためにデータをシャッフルする。
                    // EN: Shuffle the training data to get rid of spatial correlations of the training data.
                    curGPUTimer.shuffleTrainingData.start(cuStream);
                    gpuEnv.kernelShuffleTrainingData.launchWithThreadDim(
                        cuStream, cudau::dim3(shared::numTrainingDataPerFrame));
                    curGPUTimer.shuffleTrainingData.stop(cuStream);



                    if (warpDyCoord) {
                        gpuEnv.kernelWarpDyCoords.launchWithThreadDim(
                            cuStream, cudau::dim3(shared::numTrainingDataPerFrame),
                            trainRadianceQueryBuffer[1].getDevicePointer(),
                            transMatDevicePtr
                            );
                    }

                    // JP: トレーニングの実行。
                    // EN: Perform training.
                    curGPUTimer.train.start(cuStream);
                    {
                        constexpr uint32_t batchSize = shared::numTrainingDataPerFrame / 4;
                        static_assert((batchSize & 0xFF) == 0, "Batch size has to be a multiple of 256.");
                        //const uint32_t targetBatchSize =
                        //    (std::min(numTrainingData, shared::numTrainingDataPerFrame) / 4 + 255) / 256 * 256;
                        uint32_t dataStartIndex = 0;
                        for (int step = 0; step < 4; ++step) {
                            //uint32_t batchSize = std::min(numTrainingData - dataStartIndex, targetBatchSize);
                            //batchSize = batchSize / 256 * 256;

                            if (useSeparateNRC) {
                                neuralRadianceCacheSpecular.infer(
                                    cuStream,
                                    reinterpret_cast<float*>(trainRadianceQueryBuffer[1].getDevicePointer()),
                                    numInferenceQueries,
                                    reinterpret_cast<float*>(separateNrcBuffer[0].getDevicePointer()));

                                neuralRadianceCache.infer(
                                    cuStream,
                                    reinterpret_cast<float*>(trainRadianceQueryBuffer[1].getDevicePointer()),
                                    numInferenceQueries,
                                    reinterpret_cast<float*>(separateNrcBuffer[1].getDevicePointer()));


                                gpuEnv.kernelDecomposeTrainingData.launchWithThreadDim(
                                    cuStream, cudau::dim3(shared::numTrainingDataPerFrame),
                                    reinterpret_cast<float3*>(trainTargetBuffer[1].getDevicePointerAt(dataStartIndex)),
                                    reinterpret_cast<float3*>(separateNrcBuffer[0].getDevicePointerAt(dataStartIndex)),
                                    true
                                    );

                                neuralRadianceCache.train(
                                    cuStream,
                                    reinterpret_cast<float*>(trainRadianceQueryBuffer[1].getDevicePointerAt(dataStartIndex)),
                                    reinterpret_cast<float*>(separateNrcBuffer[0].getDevicePointerAt(dataStartIndex)),
                                    batchSize,
                                    (showLossValue && step == 3) ? &lossValue : nullptr);
    


                                gpuEnv.kernelDecomposeTrainingData.launchWithThreadDim(
                                    cuStream, cudau::dim3(shared::numTrainingDataPerFrame),
                                    reinterpret_cast<float3*>(trainTargetBuffer[1].getDevicePointerAt(dataStartIndex)),
                                    reinterpret_cast<float3*>(separateNrcBuffer[1].getDevicePointerAt(dataStartIndex)),
                                    false
                                    );

                                neuralRadianceCacheSpecular.train(
                                    cuStream,
                                    reinterpret_cast<float*>(trainRadianceQueryBuffer[1].getDevicePointerAt(dataStartIndex)),
                                    reinterpret_cast<float*>(separateNrcBuffer[1].getDevicePointerAt(dataStartIndex)),
                                    batchSize,
                                    (showLossValue && step == 3) ? &lossValueSpecular : nullptr);

                                    lossValue += lossValueSpecular;
                            } else {
                                // assert(batchSize <= inferenceBatchSize);
                                
                                if (perturbSmooth)
                                    neuralRadianceCache.infer(
                                        cuStream,
                                        reinterpret_cast<float*>(trainRadianceQueryBuffer[1].getDevicePointerAt(dataStartIndex)),
                                        batchSize,
                                        reinterpret_cast<float*>(inferredTrainRadianceBuffer.getDevicePointer()));



                                // std::ofstream o("pretty.json");
                                // o << std::setw(4) << framePyCache << std::endl;
                                // exit(0);

                                if (savePyCache){
                                    for (int batchID=0; batchID < batchSize; ++batchID){
                                        // {
                                        //     shared::RadianceQuery trainRadianceQueryBufferHost;
                                        //     CUDADRV_CHECK(cuMemcpyDtoHAsync(&trainRadianceQueryBufferHost, trainRadianceQueryBuffer[1].getCUdeviceptrAt(dataStartIndex+batchID),
                                        //                     sizeof(trainRadianceQueryBufferHost), cuStream));

                                        //     framePyCache["train_query"][sppId][dataStartIndex+batchID] = logQueryToJson(trainRadianceQueryBufferHost);
                                        // }
                                        if (sppId == 0){
                                            shared::RadianceQuery trainRadianceQueryBufferHost;
                                            CUDADRV_CHECK(cuMemcpyDtoHAsync(&trainRadianceQueryBufferHost, trainRadianceQueryBuffer[1].getCUdeviceptrAt(dataStartIndex+batchID),
                                                            sizeof(trainRadianceQueryBufferHost), cuStream));

                                            framePyCache["train_query"][dataStartIndex+batchID] = logQueryToJson(trainRadianceQueryBufferHost);
                                        }

                                        float3 trainTargetBufferHost;
                                        CUDADRV_CHECK(cuMemcpyDtoHAsync(&trainTargetBufferHost, trainTargetBuffer[1].getCUdeviceptrAt(dataStartIndex+batchID),
                                                        sizeof(trainTargetBufferHost), cuStream));
                                        // framePyCache["train_query"][dataStartIndex+batchID]["target"][sppId] = {trainTargetBufferHost.x, trainTargetBufferHost.y, trainTargetBufferHost.z};
                                        framePyCache["train_query"][dataStartIndex+batchID][std::format("target_{:03}", sppId)] = {trainTargetBufferHost.x, trainTargetBufferHost.y, trainTargetBufferHost.z};

                                    }


                                    // const int transfer_query_size = 16384 / 64;
                                    // for (int batchID=0; batchID < batchSize; batchID+=transfer_query_size){
                                    //     if (sppId == 0){
                                    //         shared::RadianceQuery trainRadianceQueryBufferHost[transfer_query_size];
                                    //         CUDADRV_CHECK(cuMemcpyDtoHAsync(trainRadianceQueryBufferHost, trainRadianceQueryBuffer[1].getCUdeviceptrAt(dataStartIndex+batchID),
                                    //                         sizeof(trainRadianceQueryBufferHost), cuStream));
                                    //         for (int i=0; i < transfer_query_size; ++i){
                                    //             framePyCache["train_query"][dataStartIndex+batchID] = logQueryToJson(trainRadianceQueryBufferHost[i]);
                                    //         }

                                    //     }
                                    // }
                                    // const int transfer_target_size = 16384/4;
                                    // // const int transfer_target_size = 4;
                                    // for (int batchID=0; batchID < batchSize; batchID+=transfer_target_size){
                                        
                                    //     float3 trainTargetBufferHost[transfer_target_size];
                                    //     CUDADRV_CHECK(cuMemcpyDtoHAsync(trainTargetBufferHost, trainTargetBuffer[1].getCUdeviceptrAt(dataStartIndex+batchID),
                                    //                     sizeof(trainTargetBufferHost), cuStream));
                                    //     for (int i=0; i < transfer_target_size; ++i){
                                    //         framePyCache["train_query"][dataStartIndex+batchID+i][std::format("target_{:03}", sppId)] = {trainTargetBufferHost[i].x, trainTargetBufferHost[i].y, trainTargetBufferHost[i].z};
                                    //     }
                                    // }


                                }

                                neuralRadianceCache.train(
                                    cuStream,
                                    reinterpret_cast<float*>(trainRadianceQueryBuffer[1].getDevicePointerAt(dataStartIndex)),
                                    reinterpret_cast<float*>(trainTargetBuffer[1].getDevicePointerAt(dataStartIndex)),
                                    batchSize,
                                    (showLossValue && step == 3) ? &lossValue : nullptr);


                                if ((perturbSmooth) && (perturbSmoothAfter < (int)frameIndex)) {
                                    // perturb trained queries to locate at nearby locations
                                    for (int perturbi=0; perturbi < perturbSmoothTimes; ++perturbi){
                                        for (int randi=0; randi < batchSize; ++randi){
                                            trainedQueryPerturbs[randi].x = float_dist(global_rng);
                                            trainedQueryPerturbs[randi].y = float_dist(global_rng);
                                            trainedQueryPerturbs[randi].z = float_dist(global_rng);
                                        }

                                        CUDADRV_CHECK(cuMemcpyHtoD(trainedQueryPerturbDevicePtr, trainedQueryPerturbs, sizeof(float3) * batchSize));

                                        gpuEnv.kernelPerturbCoords.launchWithThreadDim(
                                            cuStream, cudau::dim3(batchSize), 
                                            trainRadianceQueryBuffer[1].getDevicePointerAt(dataStartIndex),
                                            trainedQueryPerturbDevicePtr,
                                            batchSize);

                                        neuralRadianceCache.train(
                                            cuStream,
                                            reinterpret_cast<float*>(trainRadianceQueryBuffer[1].getDevicePointerAt(dataStartIndex)),
                                            reinterpret_cast<float*>(inferredTrainRadianceBuffer.getDevicePointerAt(dataStartIndex)),
                                            batchSize,
                                            (showLossValue && step == 3) ? &lossValue : nullptr);
                                    }
                                }


                            }

                            dataStartIndex += batchSize;
                        }
                    }
                    curGPUTimer.train.stop(cuStream);
                }
            }

            // JP: ニューラルネットワークの推定値を直接可視化する。
            // EN: Directly visualize the predictions of the neural network.
            if (bufferTypeToDisplay == shared::BufferToDisplay::NRCOnly ||
                bufferTypeToDisplay == shared::BufferToDisplay::NRCOnlyEmissive ||
                bufferTypeToDisplay == shared::BufferToDisplay::NRCOnlyRaw ||
                bufferTypeToDisplay == shared::BufferToDisplay::SeparateNRCDiffuse ||
                bufferTypeToDisplay == shared::BufferToDisplay::SeparateNRCSpecular
            ) {
                curGPUTimer.visualizeCache.start(cuStream);

                gpuEnv.pathTracing.setEntryPoint(PathTracingEntryPoint::visualizePrediction);
                gpuEnv.pathTracing.optixPipeline.launch(
                    cuStream, plpOnDevice, renderTargetSizeX, renderTargetSizeY, 1);

                if (warpDyCoord) {
                    gpuEnv.kernelWarpDyCoords.launchWithThreadDim(
                        cuStream, cudau::dim3(renderTargetSizeX * renderTargetSizeY),
                        inferenceRadianceQueryBuffer.getDevicePointer(),
                        transMatDevicePtr
                        );
                }

                neuralRadianceCache.infer(
                    cuStream,
                    reinterpret_cast<float*>(inferenceRadianceQueryBuffer.getDevicePointer()),
                    renderTargetSizeX * renderTargetSizeY,
                    reinterpret_cast<float*>(inferredRadianceBuffer.getDevicePointer()));

                // if (savePyCache){
                //     if (sppId == 0){
                //         for (int batchID=0; batchID < renderTargetSizeX * renderTargetSizeY; ++batchID){
                //             shared::RadianceQuery queryBufferHost;
                //             CUDADRV_CHECK(cuMemcpyDtoHAsync(&queryBufferHost, inferenceRadianceQueryBuffer.getCUdeviceptrAt(batchID),
                //                             sizeof(queryBufferHost), cuStream));

                //             framePyCache["rendering_infer"][batchID] = logQueryToJson(queryBufferHost); 
                //         }
                //     }
                // }

                if (useSeparateNRC)
                    neuralRadianceCacheSpecular.infer(
                        cuStream,
                        reinterpret_cast<float*>(inferenceRadianceQueryBuffer.getDevicePointer()),
                        renderTargetSizeX * renderTargetSizeY,
                        reinterpret_cast<float*>(inferredRadianceBuffer2.getDevicePointer()));

                curGPUTimer.visualizeCache.stop(cuStream);
            }



            // JP: 結果をリニアバッファーにコピーする。(法線の正規化も行う。)
            // EN: Copy the results to the linear buffers (and normalize normals).
            if (sppId == 0){
                kernelCopyToLinearBuffers.launchWithThreadDim(
                    cuStream, cudau::dim3(renderTargetSizeX, renderTargetSizeY),
                    beautyAccumBuffer.getSurfaceObject(0),
                    albedoAccumBuffer.getSurfaceObject(0),
                    normalAccumBuffer.getSurfaceObject(0),
                    gBuffer2[bufferIndex].getSurfaceObject(0),
                    linearBeautyBuffer,
                    linearAlbedoBuffer,
                    linearNormalBuffer,
                    linearFlowBuffer,
                    uint2(renderTargetSizeX, renderTargetSizeY));
            }else{
                kernelSumToLinearBuffers.launchWithThreadDim(
                    cuStream, cudau::dim3(renderTargetSizeX, renderTargetSizeY),
                    beautyAccumBuffer.getSurfaceObject(0),
                    albedoAccumBuffer.getSurfaceObject(0),
                    normalAccumBuffer.getSurfaceObject(0),
                    gBuffer2[bufferIndex].getSurfaceObject(0),
                    linearBeautyBuffer,
                    linearAlbedoBuffer,
                    linearNormalBuffer,
                    linearFlowBuffer,
                    uint2(renderTargetSizeX, renderTargetSizeY));
            }

            if (sppId == SPP-1){
            kernelAvgLinearBuffers.launchWithThreadDim(
                cuStream, cudau::dim3(renderTargetSizeX, renderTargetSizeY),
                linearBeautyBuffer,
                linearAlbedoBuffer,
                linearNormalBuffer,
                linearFlowBuffer,
                uint2(renderTargetSizeX, renderTargetSizeY),
                SPP);
            }

            }

        // write json
        if (savePyCache){
            std::string filename = expPath + std::format("/query_frame_{:03}.json", frameIndex);
            std::ofstream o(filename);
            // o << std::setw(2) << framePyCache << std::endl;
            o << framePyCache << std::endl;
            // exit(0);
        }


        curGPUTimer.denoise.start(cuStream);
        if (bufferTypeToDisplay == shared::BufferToDisplay::DenoisedBeauty) {
            denoiser.computeNormalizer(
                cuStream,
                linearBeautyBuffer, OPTIX_PIXEL_FORMAT_FLOAT4,
                denoiserScratchBuffer, hdrNormalizer);
            //float hdrNormalizerOnHost;
            //CUDADRV_CHECK(cuMemcpyDtoH(&hdrNormalizerOnHost, hdrNormalizer, sizeof(hdrNormalizerOnHost)));
            //printf("%g\n", hdrNormalizerOnHost);

            optixu::DenoiserInputBuffers inputBuffers = {};
            inputBuffers.noisyBeauty = linearBeautyBuffer;
            inputBuffers.albedo = linearAlbedoBuffer;
            inputBuffers.normal = linearNormalBuffer;
            inputBuffers.flow = linearFlowBuffer;
            inputBuffers.previousDenoisedBeauty = newSequence ?
                linearBeautyBuffer : linearDenoisedBeautyBuffer;
            inputBuffers.beautyFormat = OPTIX_PIXEL_FORMAT_FLOAT4;
            inputBuffers.albedoFormat = OPTIX_PIXEL_FORMAT_FLOAT4;
            inputBuffers.normalFormat = OPTIX_PIXEL_FORMAT_FLOAT4;
            inputBuffers.flowFormat = OPTIX_PIXEL_FORMAT_FLOAT2;

            for (int i = 0; i < denoisingTasks.size(); ++i)
                denoiser.invoke(
                    cuStream, denoisingTasks[i], inputBuffers,
                    newSequence, OPTIX_DENOISER_ALPHA_MODE_COPY, hdrNormalizer, 0.0f,
                    linearDenoisedBeautyBuffer, nullptr,
                    optixu::BufferView());
        }
        curGPUTimer.denoise.stop(cuStream);

        outputBufferSurfaceHolder.beginCUDAAccess(cuStream);

        // JP: デノイズ結果や中間バッファーの可視化。
        // EN: Visualize the denosed result or intermediate buffers.
        void* bufferToDisplay = nullptr;
        switch (bufferTypeToDisplay) {
        case shared::BufferToDisplay::NoisyBeauty:
            bufferToDisplay = linearBeautyBuffer.getDevicePointer();
            break;
        case shared::BufferToDisplay::Albedo:
            bufferToDisplay = linearAlbedoBuffer.getDevicePointer();
            break;
        case shared::BufferToDisplay::Normal:
            bufferToDisplay = linearNormalBuffer.getDevicePointer();
            break;
        case shared::BufferToDisplay::Flow:
            bufferToDisplay = linearFlowBuffer.getDevicePointer();
            break;
        case shared::BufferToDisplay::RenderingPathLength:
        case shared::BufferToDisplay::NRCOnly:
        case shared::BufferToDisplay::NRCOnlyEmissive:
        case shared::BufferToDisplay::NRCOnlyRaw:
        case shared::BufferToDisplay::SeparateNRCDiffuse:
        case shared::BufferToDisplay::SeparateNRCSpecular:
            break;
        case shared::BufferToDisplay::DenoisedBeauty:
            bufferToDisplay = linearDenoisedBeautyBuffer.getDevicePointer();
            break;
        default:
            Assert_ShouldNotBeCalled();
            break;
        }
        kernelVisualizeToOutputBuffer.launchWithThreadDim(
            cuStream, cudau::dim3(renderTargetSizeX, renderTargetSizeY),
            visualizeTrainingPath,
            bufferToDisplay, bufferTypeToDisplay,
            0.5f, std::pow(10.0f, motionVectorScale),
            outputBufferSurfaceHolder.getNext());

        outputBufferSurfaceHolder.endCUDAAccess(cuStream, true);

        curGPUTimer.frame.stop(cuStream);



        // ----------------------------------------------------------------
        // JP: OptiXによる描画結果を表示用レンダーターゲットにコピーする。
        // EN: Copy the OptiX rendering results to the display render target.

        if (applyToneMapAndGammaCorrection) {
            glEnable(GL_FRAMEBUFFER_SRGB);
            ImGui::GetStyle() = guiStyleWithGamma;
        }
        else {
            glDisable(GL_FRAMEBUFFER_SRGB);
            ImGui::GetStyle() = guiStyle;
        }

        glViewport(0, 0, curFBWidth, curFBHeight);

        glUseProgram(drawOptiXResultShader.getHandle());

        glUniform2ui(0, curFBWidth, curFBHeight);
        int32_t flags =
            (applyToneMapAndGammaCorrection ? 1 : 0);
        glUniform1i(2, flags);
        glUniform1f(3, std::pow(10.0f, brightness));

        glBindTextureUnit(0, outputTexture.getHandle());
        glBindSampler(0, outputSampler.getHandle());

        glBindVertexArray(vertexArrayForFullScreen.getHandle());
        glDrawArrays(GL_TRIANGLES, 0, 3);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glDisable(GL_FRAMEBUFFER_SRGB);

        // END: Copy the OptiX rendering results to the display render target.
        // ----------------------------------------------------------------

        glfwSwapBuffers(window);

        // logging
        if ((logExp) && (saveImgEvery > 0) && (frameIndex % saveImgEvery == 0)) {
            // save imgs 
            CUDADRV_CHECK(cuStreamSynchronize(cuStream));
            auto rawImage = new float4[renderTargetSizeX * renderTargetSizeY];
            glGetTextureSubImage(
            outputTexture.getHandle(), 0,
            0, 0, 0, renderTargetSizeX, renderTargetSizeY, 1,
            GL_RGBA, GL_FLOAT, sizeof(float4) * renderTargetSizeX * renderTargetSizeY, rawImage);

            SDRImageSaverConfig config;
            config.brightnessScale = std::pow(10.0f, brightness);
            config.applyToneMap = applyToneMapAndGammaCorrection;
            config.apply_sRGB_gammaCorrection = applyToneMapAndGammaCorrection;
            // char outpath[50];
            // sprintf(outpath, "%s/%05d.png", expPath, saveFrameID);
            std::string outpath;
            outpath = std::format("{}/{:05d}.png", imgPath, frameIndex);
                
            // ++saveFrameID;
            saveImage(outpath, renderTargetSizeX, renderTargetSizeY, rawImage,
                        config);
            // printf("Saving img to: %s\n", outpath);
            std::cout << outpath << std::endl;
            delete[] rawImage;

            // save info
            csvLogger.log(
                frameIndex, 
                lossValue, 
                curGPUTimer.frame.report(), 
                curGPUTimer.performInitialAndTemporalRIS.report() + curGPUTimer.performSpatialRIS.report(),
                curGPUTimer.pathTrace.report(),
                curGPUTimer.infer.report() + curGPUTimer.accumulateInferredRadiances.report(),
                curGPUTimer.propagateRadiances.report() + curGPUTimer.shuffleTrainingData.report() + curGPUTimer.train.report()
                );
        }

        ++frameIndex;
    }

    CUDADRV_CHECK(cuStreamSynchronize(cuStream));
    gpuTimers[1].finalize();
    gpuTimers[0].finalize();



    CUDADRV_CHECK(cuMemFree(plpOnDevice));

    pickInfos[1].finalize();
    pickInfos[0].finalize();

    CUDADRV_CHECK(cuMemFree(perFramePlpOnDevice));
    CUDADRV_CHECK(cuMemFree(staticPlpOnDevice));

    drawOptiXResultShader.finalize();
    vertexArrayForFullScreen.finalize();

    outputSampler.finalize();
    outputBufferSurfaceHolder.finalize();
    outputArray.finalize();
    outputTexture.finalize();


    
    CUDADRV_CHECK(cuMemFree(hdrNormalizer));
    CUDADRV_CHECK(cuModuleUnload(moduleCopyBuffers));    
    denoiserScratchBuffer.finalize();
    denoiserStateBuffer.finalize();
    denoiser.destroy();
    
    finalizeScreenRelatedBuffers();

    neuralRadianceCache.finalize();
    dataShufflerBuffer.finalize();
    trainSuffixTerminalInfoBuffer.finalize();
    trainVertexInfoBuffer.finalize();
    for (int i = 1; i >= 0; --i) {
        trainTargetBuffer[i].finalize();
        trainRadianceQueryBuffer[i].finalize();
        separateNrcBuffer[i].finalize();
    }
    CUDADRV_CHECK(cuMemFree(offsetToSelectTrainingPathOnDevice));
    CUDADRV_CHECK(cuMemFree(offsetToSelectUnbiasedTileOnDevice));
    for (int i = 1; i >= 0; --i) {
        targetAvg[i].finalize();
        targetMinMax[i].finalize();
        tileSize[i].finalize();
        numTrainingData[i].finalize();
    }



    CUDADRV_CHECK(cuMemFree(sceneAABBOnDevice));

    envLightImportanceMap.finalize(gpuEnv.cuContext);
    if (envLightTexture)
        cuTexObjectDestroy(envLightTexture);
    envLightArray.finalize();

    finalizeTextureCaches();

    CUDADRV_CHECK(cuStreamDestroy(cuStream));

    scene.finalize();
    
    gpuEnv.finalize();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);

    glfwTerminate();

    return 0;
}
catch (const std::exception &ex) {
    hpprintf("Error: %s\n", ex.what());
    return -1;
}
