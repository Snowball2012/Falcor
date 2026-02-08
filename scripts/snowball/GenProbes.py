from falcor import *

def render_graph_GenProbes():
    g = RenderGraph("GenProbes")
    PathTracer = createPass("PathTracer", {'samplesPerPixel': 1})
    g.addPass(PathTracer, "PathTracer")
    VBufferRT = createPass("VBufferRT", {'samplePattern': 'Stratified', 'sampleCount': 16, 'useAlphaTest': True})
    g.addPass(VBufferRT, "VBufferRT")
    AccumulatePass = createPass("AccumulatePass", {'enabled': True, 'precisionMode': 'Single'})
    g.addPass(AccumulatePass, "AccumulatePass")
    ToneMapper = createPass("ToneMapper", {'autoExposure': False, 'exposureCompensation': 0.0})
    g.addPass(ToneMapper, "ToneMapper")
    GenerateProbes = createPass("GenerateProbes", {'probeDensity': 1.0, 'probeRadius': 0.1})
    g.addPass(GenerateProbes, "GenerateProbes")
    g.addEdge("VBufferRT.vbuffer", "PathTracer.vbuffer")
    g.addEdge("VBufferRT.viewW", "PathTracer.viewW")
    g.addEdge("VBufferRT.mvec", "PathTracer.mvec")
    g.addEdge("PathTracer.color", "AccumulatePass.input")
    g.addEdge("AccumulatePass.output", "ToneMapper.src")
    g.addEdge("ToneMapper.dst", "GenerateProbes.color")
    g.addEdge("VBufferRT.vbuffer", "GenerateProbes.vbuffer")
    g.markOutput("GenerateProbes.output")
    return g

GenProbes = render_graph_GenProbes()
try: m.addGraph(GenProbes)
except NameError: None
