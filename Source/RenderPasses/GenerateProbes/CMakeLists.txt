add_plugin(GenerateProbes)

target_sources(GenerateProbes PRIVATE
    GenerateProbes.cpp
    GenerateProbes.cs.slang
    GenerateProbes.h
)

target_copy_shaders(GenerateProbes RenderPasses/GenerateProbes)

target_source_group(GenerateProbes "RenderPasses")
