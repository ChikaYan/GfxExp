#pragma once

#include <cuda.h>

enum class PositionEncoding {
    TriangleWave,
    HashGrid,
};

struct NeuralRadianceCacheConfig {
    PositionEncoding posEnc = PositionEncoding::HashGrid;
    uint32_t numHiddenLayers = 5;
    uint32_t hiddenLayerWidth = 64;
    float learningRate = 1e-2f;
    uint32_t hashNLevels = 16;
    uint32_t triNFrequency = 12;
};

// JP: サンプルプログラム全体をnvcc経由でコンパイルしないといけない状況を避けるため、
//     pimplイディオムによってtiny-cuda-nnをcpp側に隔離する。
// EN: Isolate the tiny-cuda-nn into the cpp side by pimpl idiom to avoid the situation where
//     the entire sample program needs to be compiled via nvcc.
class NeuralRadianceCache {
    class Priv;
    Priv* m = nullptr;

public:
    NeuralRadianceCache();
    ~NeuralRadianceCache();

    void initialize(NeuralRadianceCacheConfig config);
    void finalize();

    void infer(CUstream stream, float* inputData, uint32_t numData, float* predictionData);
    void train(CUstream stream, float* inputData, float* targetData, uint32_t numData,
               float* lossOnCPU = nullptr);
};
