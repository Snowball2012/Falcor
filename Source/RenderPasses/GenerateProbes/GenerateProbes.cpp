#include "GenerateProbes.h"
#include "Utils/Image/Bitmap.h"

#include <cmath>
#include <fstream>

namespace
{
// I/O channel names.
const char kColor[] = "color";
const char kVBuffer[] = "vbuffer";
const char kOutput[] = "output";

// Serialization keys.
const char kProbeDensity[] = "probeDensity";
const char kProbeRadius[] = "probeRadius";
const char kEnabled[] = "enabled";
const char kProbeImageResolution[] = "probeImageResolution";
const char kSavePath[] = "savePath";

const char kShaderFile[] = "RenderPasses/GenerateProbes/GenerateProbes.cs.slang";
const char kRayTraceShaderFile[] = "RenderPasses/GenerateProbes/GenerateProbeImages.cs.slang";
} // namespace

static void regGenerateProbes(pybind11::module& m)
{
    pybind11::class_<GenerateProbes, RenderPass, ref<GenerateProbes>> pass(m, "GenerateProbes");
    pass.def_property(kProbeDensity, &GenerateProbes::getProbeDensity, &GenerateProbes::setProbeDensity);
    pass.def_property(kProbeRadius, &GenerateProbes::getProbeRadius, &GenerateProbes::setProbeRadius);
}

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, GenerateProbes>();
    ScriptBindings::registerBinding(regGenerateProbes);
}

GenerateProbes::GenerateProbes(ref<Device> pDevice, const Properties& props) : RenderPass(pDevice)
{
    // Parse serialized properties.
    for (const auto& [key, value] : props)
    {
        if (key == kProbeDensity)
            mProbeDensity = value;
        else if (key == kProbeRadius)
            mProbeRadius = value;
        else if (key == kEnabled)
            mEnabled = value;
        else if (key == kProbeImageResolution)
            mProbeImageResolution = value;
        else if (key == kSavePath)
            mSavePath = (const std::string&)value;
        else
            logWarning("Unknown property '{}' in GenerateProbes properties.", key);
    }
}

Properties GenerateProbes::getProperties() const
{
    Properties props;
    props[kProbeDensity] = mProbeDensity;
    props[kProbeRadius] = mProbeRadius;
    props[kEnabled] = mEnabled;
    props[kProbeImageResolution] = mProbeImageResolution;
    props[kSavePath] = mSavePath;
    return props;
}

RenderPassReflection GenerateProbes::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;

    reflector.addInput(kColor, "Tonemapped input image").bindFlags(ResourceBindFlags::ShaderResource);
    reflector.addInput(kVBuffer, "Visibility buffer in packed format")
        .bindFlags(ResourceBindFlags::ShaderResource)
        .texture2D()
        .format(ResourceFormat::RGBA32Uint);
    reflector.addOutput(kOutput, "Output image with probe visualization")
        .bindFlags(ResourceBindFlags::RenderTarget | ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess)
        .format(ResourceFormat::RGBA32Float);

    return reflector;
}

void GenerateProbes::compile(RenderContext* pRenderContext, const CompileData& compileData)
{
    mFrameDim = compileData.defaultTexDims;
}

void GenerateProbes::setScene(RenderContext* pRenderContext, const ref<Scene>& pScene)
{
    mpScene = pScene;
    mpDrawProbesPass = nullptr;
    mpRayTracePass = nullptr;
    mpProbeBuffer = nullptr;
    mpProbeImage = nullptr;
    mProbeCount = 0;
    mProbePositions.clear();
    mProbesDirty = true;

    if (mpScene)
    {
        // Create the visualization compute pass.
        {
            ProgramDesc desc;
            desc.addShaderModules(mpScene->getShaderModules());
            desc.addShaderLibrary(kShaderFile).csEntry("main");
            desc.addTypeConformances(mpScene->getTypeConformances());

            DefineList defines = mpScene->getSceneDefines();
            mpDrawProbesPass = ComputePass::create(mpDevice, desc, defines);
        }

        // Create the ray tracing compute pass (for probe image generation).
        {
            ProgramDesc desc;
            desc.addShaderModules(mpScene->getShaderModules());
            desc.addShaderLibrary(kRayTraceShaderFile).csEntry("main");
            desc.addTypeConformances(mpScene->getTypeConformances());

            DefineList defines = mpScene->getSceneDefines();
            mpRayTracePass = ComputePass::create(mpDevice, desc, defines);
        }

        // Generate initial probe grid.
        generateProbes();
    }
}

void GenerateProbes::generateProbes()
{
    if (!mpScene)
        return;

    const AABB& bounds = mpScene->getSceneBounds();
    if (!bounds.valid())
    {
        logWarning("GenerateProbes: Scene has invalid bounding box. No probes generated.");
        mProbeCount = 0;
        mProbePositions.clear();
        mpProbeBuffer = nullptr;
        mProbesDirty = false;
        return;
    }

    float3 sceneMin = bounds.minPoint;
    float3 sceneMax = bounds.maxPoint;
    float3 extent = sceneMax - sceneMin;
    float density = std::max(mProbeDensity, 0.001f);

    // Compute grid dimensions. At least 1 probe per axis.
    uint32_t nx = std::max(1u, (uint32_t)(extent.x / density) + 1);
    uint32_t ny = std::max(1u, (uint32_t)(extent.y / density) + 1);
    uint32_t nz = std::max(1u, (uint32_t)(extent.z / density) + 1);

    // Cap total probe count to avoid running out of memory / too slow rendering.
    uint64_t totalProbes = (uint64_t)nx * ny * nz;
    if (totalProbes > kMaxProbeCount)
    {
        float scale = (float)std::cbrt((double)totalProbes / (double)kMaxProbeCount);
        density *= scale;
        nx = std::max(1u, (uint32_t)(extent.x / density) + 1);
        ny = std::max(1u, (uint32_t)(extent.y / density) + 1);
        nz = std::max(1u, (uint32_t)(extent.z / density) + 1);
        totalProbes = (uint64_t)nx * ny * nz;
        logWarning(
            "GenerateProbes: Probe count exceeds maximum ({}). Density auto-adjusted to {:.4f}.", kMaxProbeCount, density
        );
    }

    mProbeCount = (uint32_t)totalProbes;

    // Center the grid within the scene bounding box.
    float gridExtX = (nx > 1) ? (nx - 1) * density : 0.f;
    float gridExtY = (ny > 1) ? (ny - 1) * density : 0.f;
    float gridExtZ = (nz > 1) ? (nz - 1) * density : 0.f;

    float3 gridOrigin;
    gridOrigin.x = sceneMin.x + (extent.x - gridExtX) * 0.5f;
    gridOrigin.y = sceneMin.y + (extent.y - gridExtY) * 0.5f;
    gridOrigin.z = sceneMin.z + (extent.z - gridExtZ) * 0.5f;

    // Generate probe positions on a uniform 3D grid.
    mProbePositions.clear();
    mProbePositions.reserve(mProbeCount);

    for (uint32_t iz = 0; iz < nz; iz++)
    {
        for (uint32_t iy = 0; iy < ny; iy++)
        {
            for (uint32_t ix = 0; ix < nx; ix++)
            {
                float3 pos;
                pos.x = gridOrigin.x + ix * density;
                pos.y = gridOrigin.y + iy * density;
                pos.z = gridOrigin.z + iz * density;
                mProbePositions.push_back(float4(pos, 0.0f));
            }
        }
    }

    // Upload probe positions to GPU.
    if (mProbeCount > 0)
    {
        mpProbeBuffer = mpDevice->createStructuredBuffer(
            sizeof(float4),
            mProbeCount,
            ResourceBindFlags::ShaderResource,
            MemoryType::DeviceLocal,
            mProbePositions.data(),
            false
        );
    }
    else
    {
        mpProbeBuffer = nullptr;
    }

    mProbesDirty = false;
    logInfo("GenerateProbes: Generated {} probes ({}x{}x{}) with density {:.4f}.", mProbeCount, nx, ny, nz, density);
}

void GenerateProbes::setProbeDensity(float d)
{
    if (d != mProbeDensity)
    {
        mProbeDensity = std::max(d, 0.001f);
        mProbesDirty = true;
    }
}

void GenerateProbes::setProbeRadius(float r)
{
    if (r != mProbeRadius)
    {
        mProbeRadius = std::max(r, 0.0001f);
    }
}

// ---------------------------------------------------------------------------
// Probe image generation
// ---------------------------------------------------------------------------

void GenerateProbes::generateProbeImages(RenderContext* pRenderContext)
{
    if (!mpScene || !mpRayTracePass)
    {
        logWarning("GenerateProbes: Cannot generate probe images — no scene loaded.");
        return;
    }

    if (mProbeCount == 0 || mProbePositions.empty())
    {
        logWarning("GenerateProbes: No probes to generate images for.");
        return;
    }

    uint32_t res = mProbeImageResolution;

    // Create / resize the single-probe image texture (R32Float for computation,
    // saved to disk as EXR float16).
    if (!mpProbeImage || mpProbeImage->getWidth() != res || mpProbeImage->getHeight() != res)
    {
        mpProbeImage = mpDevice->createTexture2D(
            res,
            res,
            ResourceFormat::R32Float,
            1,
            1,
            nullptr,
            ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource
        );
    }

    // Ensure the output directory exists.
    std::filesystem::path savePath(mSavePath);
    std::filesystem::create_directories(savePath);

    // Open CSV file.
    std::filesystem::path csvPath = savePath / "probes.csv";
    std::ofstream csv(csvPath);
    if (!csv.is_open())
    {
        logError("GenerateProbes: Failed to open CSV file '{}'.", csvPath.string());
        return;
    }
    csv << "index,x,y,z,filename\n";

    // Bind scene data for ray tracing (builds/updates TLAS).
    mpScene->bindShaderDataForRaytracing(pRenderContext, mpRayTracePass->getRootVar()["gScene"]);

    auto var = mpRayTracePass->getRootVar();
    var["CB"]["gImageResolution"] = res;
    var["gOutput"] = mpProbeImage;

    // EXR export flags: uncompressed float16.
    Bitmap::ExportFlags exportFlags = Bitmap::ExportFlags::Uncompressed | Bitmap::ExportFlags::ExrFloat16;

    logInfo("GenerateProbes: Generating {} probe images ({}x{}) ...", mProbeCount, res, res);

    for (uint32_t i = 0; i < mProbeCount; i++)
    {
        float3 probePos = float3(mProbePositions[i].x, mProbePositions[i].y, mProbePositions[i].z);

        // Set per-probe constant.
        var["CB"]["gProbePosition"] = probePos;

        // Dispatch ray tracing shader.
        mpRayTracePass->execute(pRenderContext, uint3(res, res, 1));

        // Read back the single-channel texture data (handles GPU flush/sync internally).
        uint32_t subresource = mpProbeImage->getSubresourceIndex(0, 0);
        std::vector<uint8_t> data = pRenderContext->readTextureSubresource(mpProbeImage.get(), subresource);

        // Convert R32Float (1 channel) to RGB32Float (3 channels) for Bitmap::saveImage.
        const float* srcPixels = reinterpret_cast<const float*>(data.data());
        uint32_t pixelCount = res * res;
        std::vector<float> rgbData(pixelCount * 3);
        for (uint32_t p = 0; p < pixelCount; p++)
        {
            rgbData[p * 3 + 0] = srcPixels[p];
            rgbData[p * 3 + 1] = srcPixels[p];
            rgbData[p * 3 + 2] = srcPixels[p];
        }

        // Save as EXR with half-float precision.
        std::string filename = fmt::format("probe_{:06d}.exr", i);
        std::filesystem::path filePath = savePath / filename;

        Bitmap::saveImage(
            filePath,
            res,
            res,
            Bitmap::FileFormat::ExrFile,
            exportFlags,
            ResourceFormat::RGB32Float,
            true, // isTopDown
            rgbData.data()
        );

        // Write CSV row.
        csv << fmt::format("{},{},{},{},{}\n", i, probePos.x, probePos.y, probePos.z, filename);

        // Log progress every 10 %.
        if (mProbeCount >= 10 && (i + 1) % std::max(1u, mProbeCount / 10) == 0)
        {
            logInfo(
                "GenerateProbes: Progress {}/{} ({:.0f}%).",
                i + 1,
                mProbeCount,
                100.0f * (i + 1) / mProbeCount
            );
        }
    }

    csv.close();
    logInfo("GenerateProbes: Saved {} probe images and CSV to '{}'.", mProbeCount, savePath.string());
}

// ---------------------------------------------------------------------------
// execute
// ---------------------------------------------------------------------------

void GenerateProbes::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    auto pColor = renderData.getTexture(kColor);
    auto pVBuffer = renderData.getTexture(kVBuffer);
    auto pOutput = renderData.getTexture(kOutput);

    // Handle deferred probe image generation (triggered by UI button).
    if (mGenerateImagesRequested)
    {
        mGenerateImagesRequested = false;
        generateProbeImages(pRenderContext);
    }

    // Passthrough if disabled or scene is not ready.
    if (!mEnabled || !mpScene || !mpDrawProbesPass)
    {
        if (pColor && pOutput)
            pRenderContext->blit(pColor->getSRV(), pOutput->getRTV());
        else if (pOutput)
            pRenderContext->clearUAV(pOutput->getUAV().get(), float4(0.f));
        return;
    }

    // Regenerate probes if settings changed.
    if (mProbesDirty)
        generateProbes();

    // If no probes, just pass through.
    if (mProbeCount == 0 || !mpProbeBuffer)
    {
        if (pColor && pOutput)
            pRenderContext->blit(pColor->getSRV(), pOutput->getRTV());
        return;
    }

    // Bind scene data (needed for vbuffer unpacking and camera access).
    mpScene->bindShaderData(mpDrawProbesPass->getRootVar()["gScene"]);

    // Bind shader resources.
    auto var = mpDrawProbesPass->getRootVar();
    var["gColor"] = pColor;
    var["gVBuffer"] = pVBuffer;
    var["gOutput"] = pOutput;
    var["gProbes"] = mpProbeBuffer;

    var["CB"]["gFrameDim"] = mFrameDim;
    var["CB"]["gProbeCount"] = mProbeCount;
    var["CB"]["gProbeRadius"] = mProbeRadius;

    // Dispatch compute shader (one thread per pixel).
    mpDrawProbesPass->execute(pRenderContext, uint3(mFrameDim, 1));
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

void GenerateProbes::renderUI(Gui::Widgets& widget)
{
    widget.checkbox("Enable", mEnabled);

    bool dirty = false;
    dirty |= widget.var("Probe Density", mProbeDensity, 0.01f, 100.0f, 0.01f);
    widget.tooltip("Minimum distance between probes in world-space units.");

    widget.var("Probe Visual Radius", mProbeRadius, 0.001f, 10.0f, 0.001f);
    widget.tooltip("Radius of the visualized probe spheres in world-space units.");

    if (dirty)
        mProbesDirty = true;

    widget.text("Probe count: " + std::to_string(mProbeCount));

    if (mpScene)
    {
        const AABB& bounds = mpScene->getSceneBounds();
        if (bounds.valid())
        {
            widget.text(fmt::format(
                "Scene bounds: ({:.2f}, {:.2f}, {:.2f}) - ({:.2f}, {:.2f}, {:.2f})",
                bounds.minPoint.x,
                bounds.minPoint.y,
                bounds.minPoint.z,
                bounds.maxPoint.x,
                bounds.maxPoint.y,
                bounds.maxPoint.z
            ));
        }
    }

    // --- Probe image generation controls ---
    if (auto group = widget.group("Probe Image Generation", true))
    {
        group.var("Image Resolution", mProbeImageResolution, 4u, 1024u, 1u);
        group.tooltip("Resolution (NxN) of each probe distance image.");

        group.textbox("Save Path", mSavePath);
        group.tooltip("Directory where probe images and CSV will be saved.");

        if (group.button("Generate Probe Images"))
        {
            mGenerateImagesRequested = true;
        }
        group.tooltip("Ray-trace a distance image for every probe and save to disk.");
    }
}
