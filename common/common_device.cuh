#pragma once

#include "common_shared.h"

#define pixelprintf(idx, px, py, fmt, ...) do { if (idx.x == px && idx.y == py) printf(fmt, ##__VA_ARGS__); } while (0)



static constexpr float Pi = 3.14159265358979323846f;
static constexpr float RayEpsilon = 1e-4;



CUDA_DEVICE_FUNCTION float pow2(float x) {
    return x * x;
}
CUDA_DEVICE_FUNCTION float pow3(float x) {
    return x * x * x;
}
CUDA_DEVICE_FUNCTION float pow4(float x) {
    return x * x * x * x;
}
CUDA_DEVICE_FUNCTION float pow5(float x) {
    return x * x * x * x * x;
}

template <typename T>
CUDA_DEVICE_FUNCTION T lerp(const T &v0, const T &v1, float t) {
    return (1 - t) * v0 + t * v1;
}



// ( 0, 0,  1) <=> phi:      0
// (-1, 0,  0) <=> phi: 1/2 pi
// ( 0, 0, -1) <=> phi:   1 pi
// ( 1, 0,  0) <=> phi: 3/2 pi
CUDA_DEVICE_FUNCTION float3 fromPolarYUp(float phi, float theta) {
    float sinPhi, cosPhi;
    float sinTheta, cosTheta;
    sincosf(phi, &sinPhi, &cosPhi);
    sincosf(theta, &sinTheta, &cosTheta);
    return make_float3(-sinPhi * sinTheta, cosTheta, cosPhi * sinTheta);
}
CUDA_DEVICE_FUNCTION void toPolarYUp(const float3 &v, float* phi, float* theta) {
    *theta = std::acos(min(max(v.y, -1.0f), 1.0f));
    *phi = std::fmod(std::atan2(-v.x, v.z) + 2 * Pi,
                     2 * Pi);
}

CUDA_DEVICE_FUNCTION float3 halfVector(const float3 &a, const float3 &b) {
    return normalize(a + b);
}

CUDA_DEVICE_FUNCTION float absDot(const float3 &a, const float3 &b) {
    return std::fabs(dot(a, b));
}

CUDA_DEVICE_FUNCTION void makeCoordinateSystem(const float3 &normal, float3* tangent, float3* bitangent) {
    float sign = normal.z >= 0 ? 1 : -1;
    const float a = -1 / (sign + normal.z);
    const float b = normal.x * normal.y * a;
    *tangent = make_float3(1 + sign * normal.x * normal.x * a, sign * b, -sign * normal.x);
    *bitangent = make_float3(b, sign + normal.y * normal.y * a, -normal.y);
}

struct ReferenceFrame {
    float3 tangent;
    float3 bitangent;
    float3 normal;

    CUDA_DEVICE_FUNCTION ReferenceFrame(const float3 &_tangent, const float3 &_bitangent, const float3 &_normal) :
        tangent(_tangent), bitangent(_bitangent), normal(_normal) {}
    CUDA_DEVICE_FUNCTION ReferenceFrame(const float3 &_normal) : normal(_normal) {
        makeCoordinateSystem(normal, &tangent, &bitangent);
    }

    CUDA_DEVICE_FUNCTION float3 toLocal(const float3 &v) const {
        return make_float3(dot(tangent, v), dot(bitangent, v), dot(normal, v));
    }
    CUDA_DEVICE_FUNCTION float3 fromLocal(const float3 &v) const {
        return make_float3(dot(make_float3(tangent.x, bitangent.x, normal.x), v),
                           dot(make_float3(tangent.y, bitangent.y, normal.y), v),
                           dot(make_float3(tangent.z, bitangent.z, normal.z), v));
    }
};



CUDA_DEVICE_FUNCTION void concentricSampleDisk(float u0, float u1, float* dx, float* dy) {
    float r, theta;
    float sx = 2 * u0 - 1;
    float sy = 2 * u1 - 1;

    if (sx == 0 && sy == 0) {
        *dx = 0;
        *dy = 0;
        return;
    }
    if (sx >= -sy) { // region 1 or 2
        if (sx > sy) { // region 1
            r = sx;
            theta = sy / sx;
        }
        else { // region 2
            r = sy;
            theta = 2 - sx / sy;
        }
    }
    else { // region 3 or 4
        if (sx > sy) {/// region 4
            r = -sy;
            theta = 6 + sx / sy;
        }
        else {// region 3
            r = -sx;
            theta = 4 + sy / sx;
        }
    }
    theta *= Pi / 4;
    *dx = r * cos(theta);
    *dy = r * sin(theta);
}

CUDA_DEVICE_FUNCTION float3 cosineSampleHemisphere(float u0, float u1) {
    float x, y;
    concentricSampleDisk(u0, u1, &x, &y);
    return make_float3(x, y, std::sqrt(std::fmax(0.0f, 1.0f - x * x - y * y)));
}



class LambertBRDF {
    float3 m_reflectance;

public:
    CUDA_DEVICE_FUNCTION LambertBRDF(const float3 &reflectance) :
        m_reflectance(reflectance) {}

    CUDA_DEVICE_FUNCTION float3 sampleThroughput(const float3 &vGiven, float uDir0, float uDir1,
                                                 float3* vSampled, float* dirPDensity) const {
        *vSampled = cosineSampleHemisphere(uDir0, uDir1);
        *dirPDensity = vSampled->z / Pi;
        if (vGiven.z <= 0.0f)
            vSampled->z *= -1;
        return m_reflectance;
    }
    CUDA_DEVICE_FUNCTION float3 evaluate(const float3 &vGiven, const float3 &vSampled) const {
        if (vGiven.z * vSampled.z > 0)
            return m_reflectance / Pi;
        else
            return make_float3(0.0f, 0.0f, 0.0f);
    }
    CUDA_DEVICE_FUNCTION float evaluatePDF(const float3 &vGiven, const float3 &vSampled) const {
        if (vGiven.z * vSampled.z > 0)
            return fabs(vSampled.z) / Pi;
        else
            return 0.0f;
    }

    CUDA_DEVICE_FUNCTION float3 evaluateDHReflectanceEstimate(const float3 &vGiven) const {
        return m_reflectance;
    }
};



// DiffuseAndSpecularBRDF��Directional-Hemispherical Reflectance�����O�v�Z����
// �e�N�X�`���[���������ʂ��t�B�b�e�B���O����B
// Diffuse�ASpecular�����͂��ꂼ��
// - baseColor * diffusePreInt(cosV, roughness)
// - specularF0 * specularPreIntA(cosV, roughness) + (1 - specularF0) * specularPreIntB(cosV, roughness)
// �ŕ\�����B
// https://www.shadertoy.com/view/WtjfRD
CUDA_DEVICE_FUNCTION void calcFittedPreIntegratedTerms(
    float cosV, float roughness,
    float* diffusePreInt, float* specularPreIntA, float* specularPreIntB) {
        {
            float u = cosV;
            float v = roughness;
            float uu = u * u;
            float uv = u * v;
            float vv = v * v;

            *diffusePreInt = min(max(-0.417425f * uu +
                                     -0.958929f * uv +
                                     -0.096977f * vv +
                                     1.050356f * u +
                                     0.534528f * v +
                                     0.407112f * 1.0f,
                                     0.0f), 1.0f);
        }
        {
            float u = std::atan2(roughness, cosV);
            float v = std::sqrt(cosV * cosV + roughness * roughness);
            float uu = u * u;
            float uv = u * v;
            float vv = v * v;

            *specularPreIntA = min(max(0.133105f * uu +
                                       -0.278877f * uv +
                                       -0.417142f * vv +
                                       -0.192809f * u +
                                       0.426076f * v +
                                       0.996565f * 1.0f,
                                       0.0f), 1.0f);
            *specularPreIntB = min(max(0.055070f * uu +
                                       -0.163511f * uv +
                                       1.211598f * vv +
                                       0.089837f * u +
                                       -1.956888f * v +
                                       0.741397f * 1.0f,
                                       0.0f), 1.0f);
        }
}

#define USE_HEIGHT_CORRELATED_SMITH
#define USE_FITTED_PRE_INTEGRATION_FOR_WEIGHTS
#define USE_FITTED_PRE_INTEGRATION_FOR_DH_REFLECTANCE

class DiffuseAndSpecularBRDF {
    struct GGXMicrofacetDistribution {
        float alpha_g;

        CUDA_DEVICE_FUNCTION float evaluate(const float3 &m) const {
            if (m.z <= 0.0f)
                return 0.0f;
            float temp = pow2(m.x) + pow2(m.y) + pow2(m.z * alpha_g);
            return pow2(alpha_g) / (Pi * pow2(temp));
        }
        CUDA_DEVICE_FUNCTION float evaluateSmithG1(const float3 &v, const float3 &m) const {
            if (dot(v, m) * v.z <= 0)
                return 0.0f;
            float temp = pow2(alpha_g) * (pow2(v.x) + pow2(v.y)) / pow2(v.z);
            return 2 / (1 + std::sqrt(1 + temp));
        }
        CUDA_DEVICE_FUNCTION float evaluateHeightCorrelatedSmithG(const float3 &v1, const float3 &v2, const float3 &m) {
            float alpha_g2_tanTheta2_1 = pow2(alpha_g) * (pow2(v1.x) + pow2(v1.y)) / pow2(v1.z);
            float alpha_g2_tanTheta2_2 = pow2(alpha_g) * (pow2(v2.x) + pow2(v2.y)) / pow2(v2.z);
            float Lambda1 = (-1 + std::sqrt(1 + alpha_g2_tanTheta2_1)) / 2;
            float Lambda2 = (-1 + std::sqrt(1 + alpha_g2_tanTheta2_2)) / 2;
            float chi1 = (dot(v1, m) / v1.z) > 0 ? 1 : 0;
            float chi2 = (dot(v2, m) / v2.z) > 0 ? 1 : 0;
            return chi1 * chi2 / (1 + Lambda1 + Lambda2);
        }
        CUDA_DEVICE_FUNCTION float sample(const float3 &v, float u0, float u1,
                                          float3* m, float* mPDensity) const {
            // stretch view
            float3 sv = normalize(make_float3(alpha_g * v.x, alpha_g * v.y, v.z));

            // orthonormal basis
            float distIn2D = std::sqrt(sv.x * sv.x + sv.y * sv.y);
            float recDistIn2D = 1.0f / distIn2D;
            float3 T1 = (sv.z < 0.9999f) ? make_float3(sv.y * recDistIn2D, -sv.x * recDistIn2D, 0) : make_float3(1, 0, 0);
            float3 T2 = make_float3(T1.y * sv.z, -T1.x * sv.z, distIn2D);

            // sample point with polar coordinates (r, phi)
            float a = 1.0f / (1.0f + sv.z);
            float r = std::sqrt(u0);
            float phi = Pi * ((u1 < a) ? u1 / a : 1 + (u1 - a) / (1.0f - a));
            float sinPhi, cosPhi;
            sincosf(phi, &sinPhi, &cosPhi);
            float P1 = r * cosPhi;
            float P2 = r * sinPhi * ((u1 < a) ? 1.0f : sv.z);

            // compute normal
            *m = P1 * T1 + P2 * T2 + std::sqrt(1.0f - P1 * P1 - P2 * P2) * sv;

            // unstretch
            *m = normalize(make_float3(alpha_g * m->x, alpha_g * m->y, m->z));

            float D = evaluate(*m);
            *mPDensity = evaluateSmithG1(v, *m) * absDot(v, *m) * D / std::fabs(v.z);

            return D;
        }
        CUDA_DEVICE_FUNCTION float evaluatePDF(const float3 &v, const float3 &m) {
            return evaluateSmithG1(v, m) * absDot(v, m) * evaluate(m) / std::fabs(v.z);
        }
    };

    float3 m_diffuseColor;
    float3 m_specularF0Color;
    float m_roughness;

public:
    CUDA_DEVICE_FUNCTION DiffuseAndSpecularBRDF(const float3 &diffuseColor, const float3 &specularF0Color, float smoothness) {
        m_diffuseColor = diffuseColor;
        m_specularF0Color = specularF0Color;
        m_roughness = 1 - smoothness;
    }

    CUDA_DEVICE_FUNCTION DiffuseAndSpecularBRDF(const float3 &diffuseColor, float reflectance, float smoothness, float metallic) {
        m_diffuseColor = diffuseColor * (1 - metallic);
        m_specularF0Color = make_float3(0.16f * pow2(reflectance) * (1 - metallic)) + diffuseColor * metallic;
        m_roughness = 1 - smoothness;
    }

    CUDA_DEVICE_FUNCTION float3 sampleThroughput(const float3 &vGiven, float uDir0, float uDir1,
                                                 float3* vSampled, float* dirPDensity) const {
        GGXMicrofacetDistribution ggx;
        ggx.alpha_g = m_roughness * m_roughness;

        bool entering = vGiven.z >= 0.0f;
        float3 dirL;
        float3 dirV = entering ? vGiven : -vGiven;

        float oneMinusDotVN5 = pow5(1 - dirV.z);

#if defined(USE_FITTED_PRE_INTEGRATION_FOR_WEIGHTS)
        float diffusePreInt;
        float specularPreIntA, specularPreIntB;
        calcFittedPreIntegratedTerms(dirV.z, m_roughness, &diffusePreInt, &specularPreIntA, &specularPreIntB);

        float diffuseWeight = sRGB_calcLuminance(m_diffuseColor * diffusePreInt);
        float specularWeight = sRGB_calcLuminance(m_specularF0Color * specularPreIntA + (make_float3(1.0f) - m_specularF0Color) * specularPreIntB);
#else
        float expectedF_D90 = 0.5f * m_roughness + 2 * m_roughness * vGiven.z * vGiven.z;
        float expectedDiffuseFresnel = lerp(1.0f, expectedF_D90, oneMinusDotVN5);
        float iBaseColor = sRGB_calcLuminance(m_diffuseColor) * pow2(expectedDiffuseFresnel) * lerp(1.0f, 1.0f / 1.51f, m_roughness);

        float expectedOneMinusDotVH5 = pow5(1 - dirV.z);
        float iSpecularF0 = sRGB_calcLuminance(m_specularF0Color);

        float diffuseWeight = iBaseColor;
        float specularWeight = lerp(iSpecularF0, 1.0f, expectedOneMinusDotVH5);
#endif
        float sumWeights = diffuseWeight + specularWeight;

        float uComponent = uDir1;

        float diffuseDirPDF, specularDirPDF;
        float3 m;
        float dotLH;
        float D;
        if (sumWeights * uComponent < diffuseWeight) {
            uDir1 = (sumWeights * uComponent - 0) / diffuseWeight;

            // JP: �R�T�C�����z����T���v������B
            // EN: sample based on cosine distribution.
            dirL = cosineSampleHemisphere(uDir0, uDir1);
            diffuseDirPDF = dirL.z / Pi;

            // JP: ���������T���v�����X�y�L�����[�w����T���v������m�����x�����߂�B
            // EN: calculate PDF to generate the sampled direction from the specular layer.
            m = halfVector(dirL, dirV);
            dotLH = dot(dirL, m);
            float commonPDFTerm = 1.0f / (4 * dotLH);
            specularDirPDF = commonPDFTerm * ggx.evaluatePDF(dirV, m);

            D = ggx.evaluate(m);
        }
        else {
            uDir1 = (sumWeights * uComponent - diffuseWeight) / specularWeight;

            // JP: �X�y�L�����[�w�̃}�C�N���t�@�Z�b�g���z����T���v������B
            // EN: sample based on the specular microfacet distribution.
            float mPDF;
            D = ggx.sample(dirV, uDir0, uDir1, &m, &mPDF);
            float dotVH = dot(dirV, m);
            dotLH = dotVH;
            dirL = 2 * dotVH * m - dirV;
            if (dirL.z * dirV.z <= 0) {
                *dirPDensity = 0.0f;
                return make_float3(0.0f);
            }
            float commonPDFTerm = 1.0f / (4 * dotLH);
            specularDirPDF = commonPDFTerm * mPDF;

            // JP: ���������T���v�����R�T�C�����z����T���v������m�����x�����߂�B
            // EN: calculate PDF to generate the sampled direction from the cosine distribution.
            diffuseDirPDF = dirL.z / Pi;
        }

        float oneMinusDotLH5 = pow5(1 - dotLH);

#if defined(USE_HEIGHT_CORRELATED_SMITH)
        float G = ggx.evaluateHeightCorrelatedSmithG(dirL, dirV, m);
#else
        float G = ggx.evaluateSmithG1(dirL, m) * ggx.evaluateSmithG1(dirV, m);
#endif
        constexpr float F90 = 1.0f;
        float3 F = lerp(m_specularF0Color, make_float3(F90), oneMinusDotLH5);

        float microfacetDenom = 4 * dirL.z * dirV.z;
        float3 specularValue = F * ((D * G) / microfacetDenom);
        if (G == 0)
            specularValue = make_float3(0.0f);

        float F_D90 = 0.5f * m_roughness + 2 * m_roughness * dotLH * dotLH;
        float oneMinusDotLN5 = pow5(1 - dirL.z);
        float diffuseFresnelOut = lerp(1.0f, F_D90, oneMinusDotVN5);
        float diffuseFresnelIn = lerp(1.0f, F_D90, oneMinusDotLN5);
        float3 diffuseValue = m_diffuseColor * (diffuseFresnelOut * diffuseFresnelIn * lerp(1.0f, 1.0f / 1.51f, m_roughness) / Pi);

        float3 ret = diffuseValue + specularValue;

        *vSampled = entering ? dirL : -dirL;

        // PDF based on one-sample model MIS.
        *dirPDensity = (diffuseDirPDF * diffuseWeight + specularDirPDF * specularWeight) / sumWeights;

        ret *= vSampled->z / *dirPDensity;

        return ret;
    }
    CUDA_DEVICE_FUNCTION float3 evaluate(const float3 &vGiven, const float3 &vSampled) const {
        GGXMicrofacetDistribution ggx;
        ggx.alpha_g = m_roughness * m_roughness;

        if (vSampled.z * vGiven.z <= 0)
            return make_float3(0.0f, 0.0f, 0.0f);

        bool entering = vGiven.z >= 0.0f;
        float3 dirV = entering ? vGiven : -vGiven;
        float3 dirL = entering ? vSampled : -vSampled;

        float3 m = halfVector(dirL, dirV);
        float dotLH = dot(dirL, m);

        float oneMinusDotLH5 = pow5(1 - dotLH);

        float D = ggx.evaluate(m);
#if defined(USE_HEIGHT_CORRELATED_SMITH)
        float G = ggx.evaluateHeightCorrelatedSmithG(dirL, dirV, m);
#else
        float G = ggx.evaluateSmithG1(dirL, m) * ggx.evaluateSmithG1(dirV, m);
#endif
        constexpr float F90 = 1.0f;
        float3 F = lerp(m_specularF0Color, make_float3(F90), oneMinusDotLH5);

        float microfacetDenom = 4 * dirL.z * dirV.z;
        float3 specularValue = F * ((D * G) / microfacetDenom);
        if (G == 0)
            specularValue = make_float3(0.0f);

        float F_D90 = 0.5f * m_roughness + 2 * m_roughness * dotLH * dotLH;
        float oneMinusDotVN5 = pow5(1 - dirV.z);
        float oneMinusDotLN5 = pow5(1 - dirL.z);
        float diffuseFresnelOut = lerp(1.0f, F_D90, oneMinusDotVN5);
        float diffuseFresnelIn = lerp(1.0f, F_D90, oneMinusDotLN5);

        float3 diffuseValue = m_diffuseColor * (diffuseFresnelOut * diffuseFresnelIn * lerp(1.0f, 1.0f / 1.51f, m_roughness) / Pi);

        float3 ret = diffuseValue + specularValue;

        return ret;
    }
    CUDA_DEVICE_FUNCTION float evaluatePDF(const float3 &vGiven, const float3 &vSampled) const {
        GGXMicrofacetDistribution ggx;
        ggx.alpha_g = m_roughness * m_roughness;

        bool entering = vGiven.z >= 0.0f;
        float3 dirV = entering ? vGiven : -vGiven;
        float3 dirL = entering ? vSampled : -vSampled;

        float3 m = halfVector(dirL, dirV);
        float dotLH = dot(dirL, m);
        float commonPDFTerm = 1.0f / (4 * dotLH);

#if defined(USE_FITTED_PRE_INTEGRATION_FOR_WEIGHTS)
        float diffusePreInt;
        float specularPreIntA, specularPreIntB;
        calcFittedPreIntegratedTerms(dirV.z, m_roughness, &diffusePreInt, &specularPreIntA, &specularPreIntB);

        float diffuseWeight = sRGB_calcLuminance(m_diffuseColor * diffusePreInt);
        float specularWeight = sRGB_calcLuminance(m_specularF0Color * specularPreIntA + (make_float3(1.0f) - m_specularF0Color) * specularPreIntB);
#else
        float expectedF_D90 = 0.5f * m_roughness + 2 * m_roughness * vGiven.z * vGiven.z;
        float oneMinusDotVN5 = pow5(1 - dirV.z);
        float expectedDiffuseFresnel = lerp(1.0f, expectedF_D90, oneMinusDotVN5);
        float iBaseColor = calcLuminance(m_diffuseColor) * expectedDiffuseFresnel * expectedDiffuseFresnel * lerp(1.0f, 1.0f / 1.51f, m_roughness);

        float expectedOneMinusDotVH5 = pow5(1 - dirV.z);
        float iSpecularF0 = calcLuminance(m_specularF0Color);

        float diffuseWeight = iBaseColor;
        float specularWeight = lerp(iSpecularF0, 1.0f, expectedOneMinusDotVH5);
#endif

        float sumWeights = diffuseWeight + specularWeight;

        float diffuseDirPDF = dirL.z / Pi;
        float specularDirPDF = commonPDFTerm * ggx.evaluatePDF(dirV, m);

        float ret = (diffuseDirPDF * diffuseWeight + specularDirPDF * specularWeight) / sumWeights;

        return ret;
    }

    CUDA_DEVICE_FUNCTION float3 evaluateDHReflectanceEstimate(const float3 &vGiven) const {
        bool entering = vGiven.z >= 0.0f;
        float3 dirV = entering ? vGiven : -vGiven;

#if defined(USE_FITTED_PRE_INTEGRATION_FOR_DH_REFLECTANCE)
        float diffusePreInt;
        float specularPreIntA, specularPreIntB;
        calcFittedPreIntegratedTerms(dirV.z, m_roughness, &diffusePreInt, &specularPreIntA, &specularPreIntB);

        float3 diffuseDHR = m_diffuseColor * diffusePreInt;
        float3 specularDHR = m_specularF0Color * specularPreIntA + (make_float3(1.0f) - m_specularF0Color) * specularPreIntB;
#else
        float expectedCosTheta_d = dirV.z;
        float expectedF_D90 = 0.5f * m_roughness + 2 * m_roughness * pow2(expectedCosTheta_d);
        float oneMinusDotVN5 = pow5(1 - dirV.z);
        float expectedDiffFGiven = lerp(1.0f, expectedF_D90, oneMinusDotVN5);
        float expectedDiffFSampled = 1.0f; // ad-hoc
        float3 diffuseDHR = m_diffuseColor * expectedDiffFGiven * expectedDiffFSampled * lerp(1.0f, 1.0f / 1.51f, m_roughness);

        //float expectedOneMinusDotVH5 = oneMinusDotVN5;
        // (1 - m_roughness) is an ad-hoc adjustment.
        float expectedOneMinusDotVH5 = pow5(1 - dirV.z) * (1 - m_roughness);

        float3 specularDHR = lerp(m_specularF0Color, make_float3(1.0f), expectedOneMinusDotVH5);
#endif

        return min(diffuseDHR + specularDHR, make_float3(1.0f));
    }
};
