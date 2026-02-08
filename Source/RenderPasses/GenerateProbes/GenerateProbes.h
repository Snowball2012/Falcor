#pragma once
#include "Falcor.h"
#include "RenderGraph/RenderPass.h"

using namespace Falcor;

/**
 * GenerateProbes render pass.
 *
 * Generates a uniform 3D grid of probe positions within the scene bounding box
 * and visualizes them as small spheres with depth testing against the vbuffer.
 * Probes are generated once and cached until pass settings change.
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

    // Internal state.
    ref<Scene> mpScene;                  ///< Current scene.
    ref<ComputePass> mpDrawProbesPass;   ///< Compute pass for drawing probe spheres.

    // Probe data.
    ref<Buffer> mpProbeBuffer;           ///< Structured buffer of probe positions (float4: xyz = pos, w = unused).
    uint32_t mProbeCount = 0;            ///< Number of generated probes.
    bool mProbesDirty = true;            ///< Flag to regenerate probes when settings change.

    // Settings.
    float mProbeDensity = 1.0f;          ///< Minimum distance between probes in world space.
    float mProbeRadius = 0.05f;          ///< Visual radius of probe spheres in world space.
    bool mEnabled = true;                ///< Enable/disable probe visualization.

    // Frame dimensions.
    uint2 mFrameDim = {0, 0};

    /// Maximum number of probes before density is auto-clamped.
    static constexpr uint32_t kMaxProbeCount = 500000;
};
