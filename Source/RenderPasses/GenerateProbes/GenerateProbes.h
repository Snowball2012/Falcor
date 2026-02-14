#pragma once
#include "Falcor.h"
#include "RenderGraph/RenderPass.h"

#include <filesystem>

using namespace Falcor;

/**
 * GenerateProbes render pass.
 *
 * Generates a uniform 3D grid of probe positions within the scene bounding box
 * and visualizes them as small spheres with depth testing against the vbuffer.
 * Probes are generated once and cached until pass settings change.
 *
 * Can also generate per-probe distance images using inline ray tracing with
 * octahedral mapping and save them to disk as EXR files.
 */
class GenerateProbes : public RenderPass
{
public:
    FALCOR_PLUGIN_CLASS(GenerateProbes, "GenerateProbes", "Generate and visualize probes as spheres in the scene.");

    static ref<GenerateProbes> create(ref<Device> pDevice, const Properties& props)
    {
        return make_ref<GenerateProbes>(pDevice, props);
    }

    GenerateProbes(ref<Device> pDevice, const Properties& props);

    virtual Properties getProperties() const override;
    virtual RenderPassReflection reflect(const CompileData& compileData) override;
    virtual void compile(RenderContext* pRenderContext, const CompileData& compileData) override;
    virtual void setScene(RenderContext* pRenderContext, const ref<Scene>& pScene) override;
    virtual void execute(RenderContext* pRenderContext, const RenderData& renderData) override;
    virtual void renderUI(Gui::Widgets& widget) override;

    // Scripting accessors.
    float getProbeDensity() const { return mProbeDensity; }
    void setProbeDensity(float d);

    float getProbeRadius() const { return mProbeRadius; }
    void setProbeRadius(float r);

private:
    void generateProbes();
    void generateProbeImages(RenderContext* pRenderContext);

    // Internal state.
    ref<Scene> mpScene;                  ///< Current scene.
    ref<ComputePass> mpDrawProbesPass;   ///< Compute pass for drawing probe spheres.
    ref<ComputePass> mpRayTracePass;     ///< Compute pass for ray-traced probe image generation.
    ref<Texture> mpProbeImage;           ///< Temporary texture for a single probe image.

    // Probe data.
    ref<Buffer> mpProbeBuffer;           ///< GPU structured buffer of probe positions.
    std::vector<float4> mProbePositions; ///< CPU copy of probe positions (xyz = pos, w = unused).
    uint32_t mProbeCount = 0;            ///< Number of generated probes.
    bool mProbesDirty = true;            ///< Flag to regenerate probes when settings change.

    // Probe visualization settings.
    float mProbeDensity = 1.0f;          ///< Minimum distance between probes in world space.
    float mProbeRadius = 0.05f;          ///< Visual radius of probe spheres in world space.
    bool mEnabled = true;                ///< Enable/disable probe visualization.

    // Probe image generation settings.
    uint32_t mProbeImageResolution = 64; ///< Resolution (NxN) of each probe distance image.
    std::string mSavePath = "probe_images"; ///< Directory for saving probe images and CSV.
    bool mGenerateImagesRequested = false;  ///< Flag set by UI button, consumed in execute().

    // Frame dimensions.
    uint2 mFrameDim = {0, 0};

    /// Maximum number of probes before density is auto-clamped.
    static constexpr uint32_t kMaxProbeCount = 500000;
};
